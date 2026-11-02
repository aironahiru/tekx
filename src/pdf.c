/*
 * pdf.c -- the device back end: page description in, tagged PDF 2.0 out.
 *
 * Produces PDF/UA-2 and WTPDF conformant files: subset CID fonts with
 * ToUnicode maps, marked content, the logical structure tree with PDF 2.0,
 * PDF 1.7, MathML and tekx namespaces (plus a fallback RoleMap for readers
 * that ignore namespaces), links with structure destinations, bookmarks,
 * MathML associated files and XMP metadata.
 *
 * Input lines (one command per line, see doc/machinery.md):
 *	x tekx-pdl 1
 *	m KEY TEXT		metadata: title author language subject keywords date
 *	f ID SOURCE		font: builtin:NAME or a file path
 *	e ID PARENT NS TAG	structure element (PARENT may be -)
 *	a ID KEY TEXT		element property: alt actual lang title ident mathml source,
 *				or ref (TEXT: ids of the elements referred to)
 *	A ID OWNER KEY TYPE VALUE	attribute, TYPE n i r s
 *	o LEVEL PAGE Y ELEM TITLE	bookmark
 *	p W H			new page
 *	t F SIZE X Y GLYPH...	glyph run, GLYPH = gid:advance:text (text %-escaped)
 *	T ...			invisible glyph run
 *	r X Y W H		filled rectangle
 *	I X Y W H PATH		image (PNG or JPEG), PATH %-escaped
 *	b ELEM | B KIND | s TEXT | E	begin tag / artifact / actual text, end
 *	l X Y W H ELEM u URL TEXT	link to a URL
 *	l X Y W H ELEM d TARGET PAGE Y TEXT	link inside the document
 */
#include "font.h"
#include "img.h"

#include <math.h>

#define TEKX_NS "https://github.com/aironahiru/tekx/ns/1"

enum { NS_PDF2, NS_PDF, NS_MATHML, NS_TEKX };
enum { I_TEXT, I_RULE, I_TAG, I_ART, I_SPAN, I_END, I_LINK, I_IMAGE };

typedef struct {
	u16 gid;
	double adv;
	char *text;
} Glyph;

typedef struct {
	int kind, font, elem, target, tpage;
	double size, x, y, w, h, ty;
	bool invisible;
	Glyph *g;
	int ng;
	char *text, *url;
} Item;

typedef struct {
	double w, h;
	VEC(Item) items;
} Page;

typedef struct {
	char *owner, *key, *value;
	int type;
} Attr;

typedef struct {
	char *tag;
	int ns, parent;
	char *alt, *actual, *lang, *title, *ident, *mathml, *source;
	VEC(int) refs;	/* /Ref: elements this one refers to */
	VEC(Attr) attrs;
	VEC(int) kids;	/* encoded: >=0 element, <0 content (index into cont) */
	bool attached, content;
} Elem;

typedef struct {
	int page, mcid, annot;	/* annot > 0: OBJR */
} Cont;

typedef struct {
	int level, page, elem;
	double y;
	char *title;
} Outl;

typedef struct {
	Font *font;
	char **text;	/* per glyph id */
	u8 *used;
} FontUse;

static VEC(Page) pages;
static VEC(Elem) elems;
static VEC(Cont) conts;
static VEC(Outl) outls;
static VEC(FontUse) fonts;
static VEC(char *) authors;

typedef struct {
	char *path;
	Image *im;
	int ref;
} PImage;

static VEC(PImage) images;

static int
image_index(const char *path, int line)
{
	for (size_t i = 0; i < images.n; i++)
		if (streq(images.v[i].path, path))
			return (int)i;
	char *err = NULL;
	Image *im = image_load(path, &err);
	if (im == NULL) {
		error_at(line, 0, "%s", err);
		return -1;
	}
	PImage pi = {xstrdup(path), im, 0};
	VPUSH(images, pi);
	return (int)images.n - 1;
}
static char *title = "", *language = "en", *subject, *keywords;
static i64 when;

/* ---- object writer ---- */

static VEC(Buf) objs;
static bool compress = true;

static int
alloc_obj(void)
{
	Buf b = {0};

	VPUSH(objs, b);
	return (int)objs.n;
}

static Buf *
obj(int r)
{
	return &objs.v[r - 1];
}

static void
stream_obj(int r, const char *dict, const u8 *data, size_t n, bool deflate)
{
	Buf *b = obj(r);
	size_t zn = n;
	u8 *z = NULL;

	if (deflate && compress)
		z = zlib_compress(data, n, &zn);
	bprintf(b, "<< %s%s /Length %zu >>\nstream\n", dict, z ? " /Filter /FlateDecode" : "", z ? zn : n);
	bput(b, z ? z : data, z ? zn : n);
	bputs(b, "\nendstream");
	free(z);
}

static void
textstr(Buf *b, const char *s)
{
	const char *p;
	bool ascii = true;

	for (p = s; *p; p++)
		if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x7E)
			ascii = false;
	if (ascii) {
		bputc(b, '(');
		for (p = s; *p; p++) {
			if (*p == '(' || *p == ')' || *p == '\\')
				bputc(b, '\\');
			bputc(b, *p);
		}
		bputc(b, ')');
		return;
	}
	bputs(b, "<FEFF");
	size_t n = strlen(s), i = 0;
	while (i < n) {
		u32 c;
		i += (size_t)utf8_dec(s + i, n - i, &c);
		if (c >= 0x10000) {
			c -= 0x10000;
			bprintf(b, "%04X%04X", 0xD800 + (c >> 10), 0xDC00 + (c & 0x3FF));
		} else {
			bprintf(b, "%04X", c);
		}
	}
	bputc(b, '>');
}

static void
utf16hex(Buf *b, const char *s)
{
	size_t n = strlen(s), i = 0;

	while (i < n) {
		u32 c;
		i += (size_t)utf8_dec(s + i, n - i, &c);
		if (c >= 0x10000) {
			c -= 0x10000;
			bprintf(b, "%04X%04X", 0xD800 + (c >> 10), 0xDC00 + (c & 0x3FF));
		} else {
			bprintf(b, "%04X", c);
		}
	}
}

static void
pname(Buf *b, const char *s)
{
	bputc(b, '/');
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("-_.+", c))
			bputc(b, c);
		else
			bprintf(b, "#%02X", c);
	}
}

/* ---- roles ---- */

static const struct {
	const char *role, *std;
	int ns;
} roles[] = {
	{"title", "H1", NS_PDF2},
	{"subtitle", "P", NS_PDF2}, {"author", "P", NS_PDF2}, {"date", "P", NS_PDF2},
	{"text-block", "P", NS_PDF2}, {"code-block", "P", NS_PDF2}, {"bib-entry", "P", NS_PDF2},
	{"title-block", "Div", NS_PDF2}, {"equation", "Div", NS_PDF2}, {"proof", "Div", NS_PDF2},
	{"theorem", "Div", NS_PDF2}, {"block", "Div", NS_PDF2},
	{"abstract", "Sect", NS_PDF2}, {"section", "Sect", NS_PDF2}, {"subsection", "Sect", NS_PDF2},
	{"subsubsection", "Sect", NS_PDF2}, {"bibliography", "Sect", NS_PDF2}, {"contents", "Sect", NS_PDF2},
	{"section-heading", "H2", NS_PDF2}, {"subsection-heading", "H3", NS_PDF2},
	{"subsubsection-heading", "H4", NS_PDF2}, {"abstract-heading", "H2", NS_PDF2},
	{"bibliography-heading", "H2", NS_PDF2}, {"contents-heading", "H2", NS_PDF2},
	{"heading-number", "Lbl", NS_PDF2}, {"item-label", "Lbl", NS_PDF2},
	{"equation-number", "Lbl", NS_PDF2}, {"bib-label", "Lbl", NS_PDF2},
	{"theorem-head", "Strong", NS_PDF2}, {"proof-head", "Em", NS_PDF2},
	{"qed", "Span", NS_PDF2}, {"text-fragment", "Span", NS_PDF2},
	{"quote", "BlockQuote", NS_PDF}, {"code", "Code", NS_PDF},
	{"contents-entry", "P", NS_PDF2},
	{"affiliation", "P", NS_PDF2}, {"contact", "P", NS_PDF2},
	{"msc", "P", NS_PDF2}, {"keywords", "P", NS_PDF2},
};

static const char *
role_std(const char *role, int *ns)
{
	for (size_t i = 0; i < NELEM(roles); i++)
		if (streq(roles[i].role, role)) {
			*ns = roles[i].ns;
			return roles[i].std;
		}
	*ns = NS_PDF2;
	return "Div";
}

/* PDF 1.7 equivalents of PDF 2.0-only types, for the fallback RoleMap */
static const char *
pdf17(const char *t)
{
	static const char *map[][2] = {
		{"Title", "P"}, {"FENote", "Note"}, {"Aside", "Note"}, {"Em", "Span"},
		{"Strong", "Span"}, {"Sub", "Span"}, {"DocumentFragment", "Art"},
	};
	for (size_t i = 0; i < NELEM(map); i++)
		if (streq(map[i][0], t))
			return map[i][1];
	return NULL;
}

/* ---- input ---- */

static int
parse_ns(const char *s)
{
	if (streq(s, "pdf2"))
		return NS_PDF2;
	if (streq(s, "pdf"))
		return NS_PDF;
	if (streq(s, "mathml"))
		return NS_MATHML;
	return NS_TEKX;
}

static Elem *
elem_at(int id, int line)
{
	if (id < 0 || (size_t)id >= elems.n) {
		error_at(line, 0, "unknown structure element %d", id);
		return NULL;
	}
	return &elems.v[id];
}

static void
read_glyphs(Item *it, char *rest)
{
	VEC(Glyph) gs = {0};
	char *tok;

	while (*(tok = next_field(&rest))) {
		char *a = strchr(tok, ':'), *b2 = a ? strchr(a + 1, ':') : NULL;
		if (!a || !b2)
			continue;
		*a = *b2 = 0;
		Glyph g = {(u16)atoi(tok), atof(a + 1), pct_unescape(b2 + 1)};
		VPUSH(gs, g);
	}
	it->g = gs.v;
	it->ng = (int)gs.n;
}

static Font *
load_font(const char *src)
{
	char *err = NULL;
	size_t n;
	Font *f;

	if (startswith(src, "builtin:"))
		return font_builtin(src + 8);
	u8 *data = (u8 *)read_file(src, &n);
	if ((f = font_load(data, n, &err)) == NULL)
		die("%s: %s", src, err);
	return f;
}

static void
read_input(FILE *in)
{
	char *line;
	int lineno = 0;

	while ((line = read_line(in)) != NULL) {
		char *rest = line, *cmd;
		lineno++;
		cmd = next_field(&rest);
		if (*cmd == 0 || *cmd == '#')
			continue;
		Page *pg = pages.n ? &pages.v[pages.n - 1] : NULL;
		Item it;
		memset(&it, 0, sizeof it);
		switch (cmd[0]) {
		case 'x':
			break;
		case 'm': {
			char *key = next_field(&rest);
			if (streq(key, "title"))
				title = xstrdup(rest);
			else if (streq(key, "author"))
				VPUSH(authors, xstrdup(rest));
			else if (streq(key, "language"))
				language = xstrdup(rest);
			else if (streq(key, "subject"))
				subject = xstrdup(rest);
			else if (streq(key, "keywords"))
				keywords = xstrdup(rest);
			else if (streq(key, "date"))
				when = strtoll(rest, NULL, 10);
			break;
		}
		case 'f': {
			int id = atoi(next_field(&rest));
			FontUse fu = {0};
			if (id != (int)fonts.n)
				die("fonts must be declared in order");
			fu.font = load_font(rest);
			fu.text = xcalloc((size_t)fu.font->nglyphs + 1, sizeof *fu.text);
			fu.used = xcalloc((size_t)fu.font->nglyphs + 1, 1);
			VPUSH(fonts, fu);
			break;
		}
		case 'e': {
			Elem e;
			memset(&e, 0, sizeof e);
			int id = atoi(next_field(&rest));
			char *par = next_field(&rest);
			e.parent = streq(par, "-") ? -1 : atoi(par);
			e.ns = parse_ns(next_field(&rest));
			e.tag = xstrdup(next_field(&rest));
			if (id != (int)elems.n)
				die("structure elements must be declared in order");
			VPUSH(elems, e);
			break;
		}
		case 'a': {
			Elem *e = elem_at(atoi(next_field(&rest)), lineno);
			char *key = next_field(&rest), *v = xstrdup(rest);
			if (!e)
				break;
			if (streq(key, "alt")) e->alt = v;
			else if (streq(key, "actual")) e->actual = v;
			else if (streq(key, "lang")) e->lang = v;
			else if (streq(key, "title")) e->title = v;
			else if (streq(key, "ident")) e->ident = v;
			else if (streq(key, "mathml")) e->mathml = v;
			else if (streq(key, "source")) e->source = v;
			else if (streq(key, "ref")) {
				char *r = v, *f;
				while (*(f = next_field(&r)))
					VPUSH(e->refs, atoi(f));
			}
			break;
		}
		case 'A': {
			Elem *e = elem_at(atoi(next_field(&rest)), lineno);
			Attr a;
			a.owner = xstrdup(next_field(&rest));
			a.key = xstrdup(next_field(&rest));
			a.type = next_field(&rest)[0];
			a.value = xstrdup(rest);
			if (e)
				VPUSH(e->attrs, a);
			break;
		}
		case 'o': {
			Outl o;
			o.level = atoi(next_field(&rest));
			o.page = atoi(next_field(&rest));
			o.y = atof(next_field(&rest));
			o.elem = atoi(next_field(&rest));
			o.title = xstrdup(rest);
			VPUSH(outls, o);
			break;
		}
		case 'p': {
			Page p;
			memset(&p, 0, sizeof p);
			p.w = atof(next_field(&rest));
			p.h = atof(next_field(&rest));
			VPUSH(pages, p);
			break;
		}
		case 't': case 'T':
			it.kind = I_TEXT;
			it.invisible = cmd[0] == 'T';
			it.font = atoi(next_field(&rest));
			it.size = atof(next_field(&rest));
			it.x = atof(next_field(&rest));
			it.y = atof(next_field(&rest));
			if (it.font < 0 || (size_t)it.font >= fonts.n) {
				error_at(lineno, 0, "unknown font %d", it.font);
				break;
			}
			read_glyphs(&it, rest);
			goto push;
		case 'r':
			it.kind = I_RULE;
			it.x = atof(next_field(&rest));
			it.y = atof(next_field(&rest));
			it.w = atof(next_field(&rest));
			it.h = atof(next_field(&rest));
			goto push;
		case 'I':
			it.kind = I_IMAGE;
			it.x = atof(next_field(&rest));
			it.y = atof(next_field(&rest));
			it.w = atof(next_field(&rest));
			it.h = atof(next_field(&rest));
			if ((it.font = image_index(pct_unescape(next_field(&rest)), lineno)) < 0)
				break;
			goto push;
		case 'b':
			it.kind = I_TAG;
			it.elem = atoi(next_field(&rest));
			if (!elem_at(it.elem, lineno))
				break;
			goto push;
		case 'B':
			it.kind = I_ART;
			it.text = xstrdup(next_field(&rest));
			goto push;
		case 's':
			it.kind = I_SPAN;
			it.text = xstrdup(rest);
			goto push;
		case 'E':
			it.kind = I_END;
			goto push;
		case 'l': {
			it.kind = I_LINK;
			it.x = atof(next_field(&rest));
			it.y = atof(next_field(&rest));
			it.w = atof(next_field(&rest));
			it.h = atof(next_field(&rest));
			it.elem = atoi(next_field(&rest));
			char *kind = next_field(&rest);
			if (kind[0] == 'u') {
				it.url = pct_unescape(next_field(&rest));
				it.target = -1;
			} else {
				it.target = atoi(next_field(&rest));
				it.tpage = atoi(next_field(&rest));
				it.ty = atof(next_field(&rest));
			}
			it.text = xstrdup(rest);
			if (!elem_at(it.elem, lineno))
				break;
			goto push;
		}
		default:
			error_at(lineno, 0, "unknown page description command '%s'", cmd);
		}
		free(line);
		continue;
	push:
		if (pg == NULL)
			die("page description: content before the first page");
		VPUSH(pg->items, it);
		free(line);
	}
}

/* ---- structure ---- */

typedef VEC(char *) Tags;

/* a set of tag names, kept small: one entry per distinct tag */
static void
push_unique(Tags *t, char *tag)
{
	for (size_t i = 0; i < t->n; i++)
		if (streq(t->v[i], tag))
			return;
	VPUSH(*t, tag);
}

static void
attach(int e)
{
	Elem *el = &elems.v[e];

	if (el->attached)
		return;
	el->attached = true;
	if (el->parent >= 0) {
		attach(el->parent);
		VPUSH(elems.v[el->parent].kids, e);
	}
}

static bool
mark_content(int e)
{
	Elem *el = &elems.v[e];
	bool any = false;

	for (size_t i = 0; i < el->kids.n; i++) {
		int k = el->kids.v[i];
		any |= k >= 0 ? mark_content(k) : true;
	}
	el->content = any;
	return any;
}

/* ---- dates and XMP ---- */

static void
civil(i64 ts, int *y, int *mo, int *d, int *h, int *mi, int *s)
{
	i64 days = ts >= 0 ? ts / 86400 : (ts - 86399) / 86400;
	i64 secs = ts - days * 86400;
	i64 z = days + 719468;
	i64 era = (z >= 0 ? z : z - 146096) / 146097;
	i64 doe = z - era * 146097;
	i64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	i64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	i64 mp = (5 * doy + 2) / 153;
	*d = (int)(doy - (153 * mp + 2) / 5 + 1);
	*mo = (int)(mp < 10 ? mp + 3 : mp - 9);
	*y = (int)(yoe + era * 400 + (*mo <= 2));
	*h = (int)(secs / 3600);
	*mi = (int)(secs / 60 % 60);
	*s = (int)(secs % 60);
}

static void
xesc(Buf *b, const char *s)
{
	for (; *s; s++) {
		switch (*s) {
		case '&': bputs(b, "&amp;"); break;
		case '<': bputs(b, "&lt;"); break;
		case '>': bputs(b, "&gt;"); break;
		case '"': bputs(b, "&quot;"); break;
		default: bputc(b, *s);
		}
	}
}

/*
 * Every embedded file (each formula's MathML and source), for the
 * catalog's EmbeddedFiles name tree.  A document that carries files is
 * PDF/A-4f, which asks that they be listed there as well as attached to
 * the structure elements they describe; one that carries none is PDF/A-4.
 */
static VEC(int) embedded;

/* ---- colour: an ICC profile for sRGB, the output intent PDF/A asks for ---- */

static void
be32(Buf *b, u32 v)
{
	u8 x[4] = {(u8)(v >> 24), (u8)(v >> 16), (u8)(v >> 8), (u8)v};
	bput(b, x, 4);
}

static void
xyz_tag(Buf *b, double x, double y, double z)
{
	bput(b, "XYZ \0\0\0\0", 8);
	be32(b, (u32)lround(x * 65536));
	be32(b, (u32)lround(y * 65536));
	be32(b, (u32)lround(z * 65536));
}

/*
 * An ICC version 2 display profile for sRGB (IEC 61966-2.1), written out
 * rather than shipped as a binary: colorants adapted to D50 by the
 * Bradford transform, D65 media white, and the sRGB tone curve sampled at
 * 1024 points.  This is the profile every PDF viewer already assumes for
 * DeviceRGB; declaring it makes the assumption a fact of the file.
 */
static void
srgb_profile(Buf *out)
{
	enum { NTAG = 9, CURVE = 1024 };
	static const char *const sig[NTAG] = {"desc", "cprt", "wtpt", "rXYZ", "gXYZ", "bXYZ", "rTRC", "gTRC", "bTRC"};
	static const char name[] = "sRGB IEC61966-2.1";
	size_t off[NTAG], len[NTAG];
	Buf d = {0};
	size_t base = 128 + 4 + 12 * NTAG;

	for (int t = 0; t < NTAG; t++) {
		while (d.n % 4)
			bputc(&d, 0);
		off[t] = base + d.n;
		size_t start = d.n;
		switch (t) {
		case 0:	/* textDescriptionType: ASCII, then empty Unicode and ScriptCode parts */
			bput(&d, "desc\0\0\0\0", 8);
			be32(&d, sizeof name);
			bput(&d, name, sizeof name);
			be32(&d, 0);
			be32(&d, 0);
			for (int i = 0; i < 2 + 1 + 67; i++)
				bputc(&d, 0);
			break;
		case 1:
			bput(&d, "text\0\0\0\0", 8);
			bput(&d, "Public domain", 14);
			break;
		case 2: xyz_tag(&d, 0.9505, 1.0, 1.0891); break;
		case 3: xyz_tag(&d, 0.436066, 0.222488, 0.013916); break;
		case 4: xyz_tag(&d, 0.385147, 0.716873, 0.097076); break;
		case 5: xyz_tag(&d, 0.143066, 0.060608, 0.714096); break;
		case 6:
			bput(&d, "curv\0\0\0\0", 8);
			be32(&d, CURVE);
			for (int i = 0; i < CURVE; i++) {
				double v = (double)i / (CURVE - 1);
				v = v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
				u8 x[2] = {(u8)(lround(v * 65535) >> 8), (u8)lround(v * 65535)};
				bput(&d, x, 2);
			}
			break;
		default:	/* the green and blue curves share the red one's data */
			off[t] = off[6];
			len[t] = len[6];
			continue;
		}
		len[t] = d.n - start;
	}
	while (d.n % 4)
		bputc(&d, 0);
	be32(out, (u32)(base + d.n));
	be32(out, 0);				/* preferred CMM */
	be32(out, 0x02100000);			/* version 2.1 */
	bput(out, "mntrRGB XYZ ", 12);		/* display class, RGB data, XYZ connection space */
	static const u8 date[12] = {0x07, 0xEA, 0, 1, 0, 1};	/* 2026-01-01: fixed, so builds reproduce */
	bput(out, date, 12);
	bput(out, "acsp", 4);
	for (int i = 0; i < 24; i++)		/* platform, flags, manufacturer, model, attributes */
		bputc(out, 0);
	be32(out, 0);				/* perceptual intent */
	be32(out, 0x0000F6D6);			/* the D50 illuminant, exactly as the ICC writes it */
	be32(out, 0x00010000);
	be32(out, 0x0000D32D);
	for (int i = 0; i < 48; i++)		/* creator, profile ID, reserved */
		bputc(out, 0);
	be32(out, NTAG);
	for (int t = 0; t < NTAG; t++) {
		bput(out, sig[t], 4);
		be32(out, (u32)off[t]);
		be32(out, (u32)len[t]);
	}
	bput(out, d.s, d.n);
}

static void
xmp(Buf *b)
{
	int y, mo, d, h, mi, s;
	char now[32], day[16];

	civil(when, &y, &mo, &d, &h, &mi, &s);
	snprintf(now, sizeof now, "%04d-%02d-%02dT%02d:%02d:%02dZ", y, mo, d, h, mi, s);
	snprintf(day, sizeof day, "%04d-%02d-%02d", y, mo, d);
	bputs(b, "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
	    "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
	    " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
	    "  <rdf:Description rdf:about=\"\"\n"
	    "    xmlns:dc=\"http://purl.org/dc/elements/1.1/\"\n"
	    "    xmlns:pdf=\"http://ns.adobe.com/pdf/1.3/\"\n"
	    "    xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"\n"
	    "    xmlns:pdfuaid=\"http://www.aiim.org/pdfua/ns/id/\"\n"
	    "    xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\"\n"
	    "    xmlns:pdfd=\"http://pdfa.org/declarations/\">\n"
	    "   <dc:format>application/pdf</dc:format>\n"
	    "   <dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">");
	xesc(b, title);
	bputs(b, "</rdf:li></rdf:Alt></dc:title>\n");
	if (authors.n) {
		bputs(b, "   <dc:creator><rdf:Seq>");
		for (size_t i = 0; i < authors.n; i++) {
			bputs(b, "<rdf:li>");
			xesc(b, authors.v[i]);
			bputs(b, "</rdf:li>");
		}
		bputs(b, "</rdf:Seq></dc:creator>\n");
	}
	if (subject) {
		bputs(b, "   <dc:description><rdf:Alt><rdf:li xml:lang=\"x-default\">");
		xesc(b, subject);
		bputs(b, "</rdf:li></rdf:Alt></dc:description>\n");
	}
	bputs(b, "   <dc:language><rdf:Bag><rdf:li>");
	xesc(b, language);
	bputs(b, "</rdf:li></rdf:Bag></dc:language>\n");
	if (keywords) {
		bputs(b, "   <pdf:Keywords>");
		xesc(b, keywords);
		bputs(b, "</pdf:Keywords>\n");
	}
	bprintf(b, "   <pdf:Producer>tekx %s</pdf:Producer>\n"
	    "   <xmp:CreatorTool>tekx %s</xmp:CreatorTool>\n"
	    "   <xmp:CreateDate>%s</xmp:CreateDate>\n"
	    "   <xmp:ModifyDate>%s</xmp:ModifyDate>\n"
	    "   <xmp:MetadataDate>%s</xmp:MetadataDate>\n"
	    "   <pdfuaid:part>2</pdfuaid:part>\n"
	    "   <pdfuaid:rev>2024</pdfuaid:rev>\n"
	    "   <pdfaid:part>4</pdfaid:part>\n"
	    "   <pdfaid:rev>2020</pdfaid:rev>\n%s"
	    "   <pdfd:declarations><rdf:Bag>\n", TEKX_VERSION, TEKX_VERSION, now, now, now,
	    embedded.n ? "   <pdfaid:conformance>F</pdfaid:conformance>\n" : "");
	const char *kinds[2] = {"accessibility", "reuse"};
	for (int k = 0; k < 2; k++)
		bprintf(b, "    <rdf:li rdf:parseType=\"Resource\">"
		    "<pdfd:conformsTo>http://pdfa.org/declarations/wtpdf#%s1.0</pdfd:conformsTo>"
		    "<pdfd:claimData><rdf:Bag><rdf:li rdf:parseType=\"Resource\">"
		    "<pdfd:claimBy>tekx %s</pdfd:claimBy><pdfd:claimDate>%s</pdfd:claimDate>"
		    "</rdf:li></rdf:Bag></pdfd:claimData></rdf:li>\n", kinds[k], TEKX_VERSION, day);
	bputs(b, "   </rdf:Bag></pdfd:declarations>\n  </rdf:Description>\n </rdf:RDF>\n</x:xmpmeta>\n<?xpacket end=\"w\"?>");
}

/* ---- fonts ---- */

static void
write_font(int ref, FontUse *fu)
{
	Font *f = fu->font;
	Buf seed = {0}, b = {0}, w = {0}, cm = {0};
	u8 h[16];
	char tag[7], *base;
	double k = 1000.0 / f->upem;
	size_t plen;
	int kind;

	bputs(&seed, f->psname);
	for (int g = 0; g < f->nglyphs; g++)
		if (fu->used[g])
			bprintf(&seed, ",%d", g);
	md5((u8 *)seed.s, seed.n, h);
	for (int i = 0; i < 6; i++)
		tag[i] = (char)('A' + h[i] % 26);
	tag[6] = 0;
	base = fmt("%s+%s", tag, f->psname);
	fu->used[0] = 1;
	u8 *prog = font_subset(f, fu->used, base, &plen, &kind);

	int file = alloc_obj(), desc = alloc_obj(), cid = alloc_obj(), tu = alloc_obj();
	stream_obj(file, kind == PROGRAM_CFF ? "/Subtype /CIDFontType0C" : fmt("/Length1 %zu", plen), prog, plen, true);

	Buf *d = obj(desc);
	bputs(d, "<< /Type /FontDescriptor /FontName ");
	pname(d, base);
	bprintf(d, " /Flags %u /FontBBox [", font_pdf_flags(f));
	for (int i = 0; i < 4; i++) {
		bnum(d, f->bbox[i] * k);
		bputc(d, i < 3 ? ' ' : ']');
	}
	bputs(d, " /ItalicAngle ");
	bnum(d, f->italic_angle);
	bputs(d, " /Ascent ");
	bnum(d, f->ascender * k);
	bputs(d, " /Descent ");
	bnum(d, f->descender * k);
	bputs(d, " /CapHeight ");
	bnum(d, f->capheight * k);
	bputs(d, " /XHeight ");
	bnum(d, f->xheight * k);
	bprintf(d, " /StemV %d /%s %d 0 R >>", (int)(50 + (f->weight / 65.0) * (f->weight / 65.0)),
	    kind == PROGRAM_CFF ? "FontFile3" : "FontFile2", file);

	for (int g = 0; g < f->nglyphs;) {
		if (!fu->used[g]) {
			g++;
			continue;
		}
		bprintf(&w, "%d [", g);
		for (; g < f->nglyphs && fu->used[g]; g++) {
			bnum(&w, font_adv(f, g) * k);
			bputc(&w, ' ');
		}
		w.s[w.n - 1] = ']';
		bputc(&w, ' ');
	}
	Buf *c = obj(cid);
	bprintf(c, "<< /Type /Font /Subtype /%s /BaseFont ", kind == PROGRAM_CFF ? "CIDFontType0" : "CIDFontType2");
	pname(c, base);
	bprintf(c, " /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /FontDescriptor %d 0 R /DW 0 /W [%s]%s >>",
	    desc, w.s ? w.s : "", kind == PROGRAM_TRUETYPE ? " /CIDToGIDMap /Identity" : "");

	bputs(&cm, "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
	    "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
	    "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n"
	    "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n");
	int pend = 0;
	Buf chunk = {0};
	for (int g = 0; g < f->nglyphs; g++) {
		if (!fu->used[g] || !fu->text[g] || !*fu->text[g])
			continue;
		bprintf(&chunk, "<%04X> <", g);
		utf16hex(&chunk, fu->text[g]);
		bputs(&chunk, ">\n");
		if (++pend == 100) {
			bprintf(&cm, "%d beginbfchar\n%sendbfchar\n", pend, chunk.s);
			breset(&chunk);
			pend = 0;
		}
	}
	if (pend)
		bprintf(&cm, "%d beginbfchar\n%sendbfchar\n", pend, chunk.s);
	bputs(&cm, "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n");
	stream_obj(tu, "", (u8 *)cm.s, cm.n, true);

	bputs(&b, "<< /Type /Font /Subtype /Type0 /BaseFont ");
	pname(&b, base);
	bprintf(&b, " /Encoding /Identity-H /DescendantFonts [%d 0 R] /ToUnicode %d 0 R >>", cid, tu);
	*obj(ref) = b;
}

static int
embed_file(const char *file, const char *mime, const char *rel, const char *desc, const char *data, const char *date)
{
	int st = alloc_obj(), spec = alloc_obj();
	Buf d = {0};

	VPUSH(embedded, spec);
	bputs(&d, "/Type /EmbeddedFile /Subtype ");
	pname(&d, mime);
	bputs(&d, " /Params << /ModDate ");
	textstr(&d, date);
	bprintf(&d, " /Size %zu >>", strlen(data));
	stream_obj(st, d.s, (const u8 *)data, strlen(data), true);
	Buf *s = obj(spec);
	bputs(s, "<< /Type /Filespec /F ");
	textstr(s, file);
	bputs(s, " /UF ");
	textstr(s, file);
	bputs(s, " /Desc ");
	textstr(s, desc);
	bprintf(s, " /AFRelationship /%s /EF << /F %d 0 R /UF %d 0 R >> >>", rel, st, st);
	return spec;
}

/*
 * The EmbeddedFiles name tree, keyed by zero-padded sequence numbers so
 * that the keys sort as strings: one leaf, or leaves of 1024 files under
 * one root.  Returns the root, or 0 when the document carries no files.
 */
static int
files_tree(void)
{
	enum { LEAF = 1024 };
	size_t nleaf = (embedded.n + LEAF - 1) / LEAF;
	Buf kids = {0};

	if (embedded.n == 0)
		return 0;
	int root = alloc_obj();
	for (size_t l = 0; l < nleaf; l++) {
		size_t lo = l * LEAF, hi = lo + LEAF < embedded.n ? lo + LEAF : embedded.n;
		int leaf = nleaf == 1 ? root : alloc_obj();
		Buf *b = obj(leaf);	/* fetched after alloc_obj(), which may move it */
		bputs(b, "<<");
		if (nleaf > 1)
			bprintf(b, " /Limits [(%07zu) (%07zu)]", lo + 1, hi);
		bputs(b, " /Names [");
		for (size_t i = lo; i < hi; i++)
			bprintf(b, "(%07zu) %d 0 R ", i + 1, embedded.v[i]);
		b->s[b->n - 1] = ']';
		bputs(b, " >>");
		bprintf(&kids, "%d 0 R ", leaf);
	}
	if (nleaf > 1) {
		kids.s[kids.n - 1] = ']';
		bprintf(obj(root), "<< /Kids [%s >>", kids.s);
	}
	return root;
}

int
pdf_main(FILE *in, FILE *out, const char *file)
{
	/* errors here are about the page description, not the source */
	diag_file = fmt("%s (page description)", file);
	when = doc_time();
	read_input(in);
	if (nerrors)
		return 1;
	if (pages.n == 0)
		die("page description has no pages");
	if (elems.n == 0)
		die("page description has no structure");

	int catalog = alloc_obj(), pages_ref = alloc_obj(), sroot = alloc_obj();
	int *page_ref = xmalloc(pages.n * sizeof *page_ref);
	int *font_ref = xmalloc((fonts.n + 1) * sizeof *font_ref);
	int *elem_ref = xmalloc(elems.n * sizeof *elem_ref);
	for (size_t i = 0; i < pages.n; i++)
		page_ref[i] = alloc_obj();
	for (size_t i = 0; i < fonts.n; i++)
		font_ref[i] = alloc_obj();
	for (size_t i = 0; i < elems.n; i++)
		elem_ref[i] = alloc_obj();
	elems.v[0].attached = true;

	VEC(int) annot_parent = {0};	/* StructParent key - pages.n -> element */
	Buf *contents = xcalloc(pages.n, sizeof *contents);
	VEC(int) *mcid_elem = xcalloc(pages.n, sizeof *mcid_elem);
	VEC(int) *page_annots = xcalloc(pages.n, sizeof *page_annots);
	Buf *page_props = xcalloc(pages.n, sizeof *page_props);	/* each page's /Properties entries */

	for (size_t pi = 0; pi < pages.n; pi++) {
		Page *pg = &pages.v[pi];
		Buf *c = &contents[pi];
		int stack[256], sp = 0, mcid = 0;	/* stack: 1 structural/artifact, 0 span */
		int span_prop[256], nprop = 0;		/* a span's named property, /ATn */
		for (size_t ii = 0; ii < pg->items.n; ii++) {
			Item *it = &pg->items.v[ii];
			bool marked = false;
			for (int s = 0; s < sp; s++)
				marked |= stack[s] == 1;
			switch (it->kind) {
			case I_TEXT: {
				FontUse *fu = &fonts.v[it->font];
				double scale = it->size / fu->font->upem, adj = 0;
				if (it->ng == 0)
					break;
				if (!marked)
					bputs(c, "/Artifact BMC\n");	/* ISO 14289-2 8.2.2 safety net */
				/*
				 * Inside an ActualText span, each text object is also wrapped
				 * in /Copy /ATn, a named property holding the same text, so
				 * that a formula or a hyphenated word pastes once and whole in
				 * every viewer.  Readers disagree about nested marked content:
				 * poppler takes ActualText only from a Span and lets the
				 * span apply to everything inside, so it reads the outer Span
				 * and ignores /Copy; PDFium takes it from any tag but only from
				 * the innermost dictionary, so it reads /Copy, and skips each
				 * further text object whose innermost dictionary is the same
				 * object, as a named property is.
				 */
				int inner = -1;
				for (int s = sp - 1; s >= 0 && inner < 0 && marked; s--)
					if (stack[s] == 0)
						inner = span_prop[s];
				if (inner >= 0)
					bprintf(c, "/Copy /AT%d BDC\n", inner);
				bprintf(c, "BT\n/F%d ", it->font);
				bnum(c, it->size);
				bputs(c, " Tf\n");
				/* invisible helpers (the "(" and "^" of copy text) are drawn
				 * at 2% width: each is a sliver at its place, so no viewer
				 * thinks it overlaps the next word or widens the formula */
				if (it->invisible)
					bputs(c, "3 Tr 2 Tz\n");
				bputs(c, "1 0 0 1 ");
				bnum(c, it->x);
				bputc(c, ' ');
				bnum(c, pg->h - it->y);
				bputs(c, " Tm\n[");
				for (int k = 0; k < it->ng; k++) {
					Glyph *g = &it->g[k];
					if (adj > 0.0005 || adj < -0.0005)
						bnum(c, adj);
					bprintf(c, "<%04X>", g->gid);
					adj = -(g->adv - font_adv(fu->font, g->gid) * scale) * 1000.0 / it->size;
					if (g->gid < fu->font->nglyphs) {
						fu->used[g->gid] = 1;
						if (!fu->text[g->gid] && *g->text)
							fu->text[g->gid] = g->text;
					}
				}
				bputs(c, "] TJ\n");
				if (it->invisible)
					bputs(c, "0 Tr 100 Tz\n");
				bputs(c, "ET\n");
				if (inner >= 0)
					bputs(c, "EMC\n");
				if (!marked)
					bputs(c, "EMC\n");
				break;
			}
			case I_RULE:
				if (!marked)
					bputs(c, "/Artifact BMC\n");
				bnum(c, it->x);
				bputc(c, ' ');
				bnum(c, pg->h - it->y - it->h);
				bputc(c, ' ');
				bnum(c, it->w);
				bputc(c, ' ');
				bnum(c, it->h);
				bputs(c, " re f\n");
				if (!marked)
					bputs(c, "EMC\n");
				break;
			case I_IMAGE:
				if (!marked)
					bputs(c, "/Artifact BMC\n");
				bputs(c, "q ");
				bnum(c, it->w);
				bputs(c, " 0 0 ");
				bnum(c, it->h);
				bputc(c, ' ');
				bnum(c, it->x);
				bputc(c, ' ');
				bnum(c, pg->h - it->y - it->h);
				bprintf(c, " cm /Im%d Do Q\n", it->font);
				if (!marked)
					bputs(c, "EMC\n");
				break;
			case I_TAG: {
				Elem *e = &elems.v[it->elem];
				Cont ct = {(int)pi, mcid, 0};
				pname(c, e->tag);
				bprintf(c, " <</MCID %d>> BDC\n", mcid);
				attach(it->elem);
				VPUSH(conts, ct);
				VPUSH(e->kids, -(int)conts.n);
				VPUSH(mcid_elem[pi], it->elem);
				mcid++;
				if (sp < 256)
					stack[sp++] = 1;
				break;
			}
			case I_ART:
				if (streq(it->text, "pagination"))
					bputs(c, "/Artifact <</Type /Pagination>> BDC\n");
				else if (streq(it->text, "layout"))
					bputs(c, "/Artifact <</Type /Layout>> BDC\n");
				else
					bputs(c, "/Artifact BMC\n");
				if (sp < 256)
					stack[sp++] = 1;
				break;
			case I_SPAN:
				bputs(c, "/Span <</ActualText <FEFF");
				utf16hex(c, it->text);
				bputs(c, ">>> BDC\n");
				bprintf(&page_props[pi], "/AT%d <</ActualText <FEFF", nprop);
				utf16hex(&page_props[pi], it->text);
				bputs(&page_props[pi], ">>> ");
				if (sp < 256) {
					span_prop[sp] = nprop;
					stack[sp++] = 0;
				}
				nprop++;
				break;
			case I_END:
				if (sp > 0) {
					sp--;
					bputs(c, "EMC\n");
				}
				break;
			case I_LINK: {
				int a = alloc_obj();
				int key = (int)(pages.n + annot_parent.n);
				Buf *ab = obj(a);
				bputs(ab, "<< /Type /Annot /Subtype /Link /Rect [");
				bnum(ab, it->x);
				bputc(ab, ' ');
				bnum(ab, pg->h - it->y - it->h);
				bputc(ab, ' ');
				bnum(ab, it->x + it->w);
				bputc(ab, ' ');
				bnum(ab, pg->h - it->y);
				bprintf(ab, "] /Border [0 0 0] /F 4 /StructParent %d /Contents ", key);
				textstr(ab, it->text);
				if (it->target < 0) {
					bputs(ab, " /A << /S /URI /URI ");
					textstr(ab, it->url);
					bputs(ab, " >> >>");
				} else {
					int tp = MIN(MAX(it->tpage, 0), (int)pages.n - 1);
					bprintf(ab, " /A << /S /GoTo /D [%d 0 R /XYZ null ", page_ref[tp]);
					bnum(ab, pages.v[tp].h - it->ty);
					bprintf(ab, " null] /SD [%d 0 R /XYZ null ", elem_ref[it->target]);
					bnum(ab, pages.v[tp].h - it->ty);
					bputs(ab, " null] >> >>");
				}
				attach(it->elem);
				Cont ct = {(int)pi, 0, a};
				VPUSH(conts, ct);
				VPUSH(elems.v[it->elem].kids, -(int)conts.n);
				VPUSH(annot_parent, it->elem);
				VPUSH(page_annots[pi], a);
				break;
			}
			}
		}
		while (sp-- > 0)
			bputs(c, "EMC\n");
	}

	for (size_t i = 0; i < fonts.n; i++)
		write_font(font_ref[i], &fonts.v[i]);

	Buf fres = {0};
	for (size_t i = 0; i < fonts.n; i++)
		bprintf(&fres, "/F%zu %d 0 R ", i, font_ref[i]);
	/* images: 8-bit samples (alpha as a soft mask), or JPEG passed through */
	Buf xres = {0};
	for (size_t i = 0; i < images.n; i++) {
		Image *im = images.v[i].im;
		const char *cs = im->comps == 1 ? "/DeviceGray" : "/DeviceRGB";
		int smask = 0;
		if (im->alpha) {
			smask = alloc_obj();
			stream_obj(smask, fmt("/Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceGray "
			    "/BitsPerComponent 8", im->w, im->h), im->alpha, im->alen, true);
		}
		int r = alloc_obj();
		char *dict = fmt("/Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace %s /BitsPerComponent 8%s%s",
		    im->w, im->h, cs, im->jpeg ? " /Filter /DCTDecode" : "", smask ? fmt(" /SMask %d 0 R", smask) : "");
		stream_obj(r, dict, im->data, im->len, !im->jpeg);
		images.v[i].ref = r;
		bprintf(&xres, "/Im%zu %d 0 R ", i, r);
	}
	for (size_t pi = 0; pi < pages.n; pi++) {
		int cref = alloc_obj();
		stream_obj(cref, "", (u8 *)contents[pi].s, contents[pi].n, true);
		Buf *p = obj(page_ref[pi]);
		bprintf(p, "<< /Type /Page /Parent %d 0 R /MediaBox [0 0 ", pages_ref);
		bnum(p, pages.v[pi].w);
		bputc(p, ' ');
		bnum(p, pages.v[pi].h);
		bprintf(p, "] /Resources << /Font << %s>>%s%s%s%s%s%s >> /Contents %d 0 R /StructParents %zu /Tabs /S",
		    fres.s ? fres.s : "", xres.s ? " /XObject << " : "", xres.s ? xres.s : "", xres.s ? ">>" : "",
		    page_props[pi].n ? " /Properties << " : "", page_props[pi].n ? bstr(&page_props[pi]) : "", page_props[pi].n ? ">>" : "",
		    cref, pi);
		if (page_annots[pi].n) {
			bputs(p, " /Annots [");
			for (size_t k = 0; k < page_annots[pi].n; k++)
				bprintf(p, "%d 0 R ", page_annots[pi].v[k]);
			p->s[p->n - 1] = ']';
		}
		bputs(p, " >>");
	}
	Buf *pp = obj(pages_ref);
	bputs(pp, "<< /Type /Pages /Kids [");
	for (size_t pi = 0; pi < pages.n; pi++)
		bprintf(pp, "%d 0 R ", page_ref[pi]);
	bprintf(pp, "] /Count %zu >>", pages.n);

	/* structure tree */
	int ns[4];
	for (int i = 0; i < 4; i++)
		ns[i] = alloc_obj();
	bputs(obj(ns[NS_PDF2]), "<< /Type /Namespace /NS (http://iso.org/pdf2/ssn) >>");
	bputs(obj(ns[NS_PDF]), "<< /Type /Namespace /NS (http://iso.org/pdf/ssn) >>");
	bputs(obj(ns[NS_MATHML]), "<< /Type /Namespace /NS (http://www.w3.org/1998/Math/MathML) >>");
	mark_content(0);

	char date[24];
	{
		int y, mo, d, h, mi, s;
		civil(when, &y, &mo, &d, &h, &mi, &s);
		snprintf(date, sizeof date, "D:%04d%02d%02d%02d%02d%02dZ", y, mo, d, h, mi, s);
	}
	Tags troles = {0};
	Tags stdtags = {0};
	int naf = 0;
	for (size_t i = 0; i < elems.n; i++) {
		Elem *e = &elems.v[i];
		Buf eb = {0}, *b = &eb;	/* local: embed_file() may grow objs */
		if (!e->content && i != 0) {
			bputs(obj(elem_ref[i]), "null");
			continue;
		}
		if (e->ns == NS_TEKX)
			push_unique(&troles, e->tag);
		else
			push_unique(&stdtags, e->tag);
		bputs(b, "<< /Type /StructElem /S ");
		pname(b, e->tag);
		bprintf(b, " /NS %d 0 R /P %d 0 R /K [", ns[e->ns], e->parent >= 0 ? elem_ref[e->parent] : sroot);
		for (size_t k = 0; k < e->kids.n; k++) {
			int kid = e->kids.v[k];
			if (kid >= 0) {
				if (elems.v[kid].content)
					bprintf(b, "%d 0 R ", elem_ref[kid]);
			} else {
				Cont *ct = &conts.v[-kid - 1];
				if (ct->annot)
					bprintf(b, "<< /Type /OBJR /Pg %d 0 R /Obj %d 0 R >> ", page_ref[ct->page], ct->annot);
				else
					bprintf(b, "<< /Type /MCR /Pg %d 0 R /MCID %d >> ", page_ref[ct->page], ct->mcid);
			}
		}
		if (b->s[b->n - 1] == ' ')
			b->n--;
		bputc(b, ']');
		struct { const char *key; char *v; } props[] = {
			{"Alt", e->alt}, {"ActualText", e->actual}, {"Lang", e->lang}, {"T", e->title}, {"ID", e->ident},
		};
		for (size_t k = 0; k < NELEM(props); k++) {
			if (!props[k].v)
				continue;
			bprintf(b, " /%s ", props[k].key);
			textstr(b, props[k].v);
		}
		if (e->refs.n) {
			bputs(b, " /Ref [");
			for (size_t k = 0; k < e->refs.n; k++)
				if (e->refs.v[k] >= 0 && (size_t)e->refs.v[k] < elems.n)
					bprintf(b, "%s%d 0 R", k ? " " : "", elem_ref[e->refs.v[k]]);
			bputc(b, ']');
		}
		if (e->attrs.n) {
			bputs(b, " /A [");
			for (size_t k = 0; k < e->attrs.n; k++) {
				Attr *a = &e->attrs.v[k];
				bool same = k > 0 && streq(e->attrs.v[k - 1].owner, a->owner);
				if (!same) {
					if (k > 0)
						bputs(b, " >> ");
					bputs(b, "<< /O ");
					pname(b, a->owner);
				}
				bputc(b, ' ');
				pname(b, a->key);
				bputc(b, ' ');
				if (a->type == 'n')
					pname(b, a->value);
				else if (a->type == 's')
					textstr(b, a->value);
				else
					bputs(b, a->value);
			}
			bputs(b, " >>]");
		}
		int af[2], naf_here = 0;
		if (e->mathml) {
			naf++;
			af[naf_here++] = embed_file(fmt("mathml-%d.xml", naf), "application/mathml+xml", "Supplement",
			    fmt("MathML of formula %d", naf), e->mathml, date);
		}
		if (e->source)
			af[naf_here++] = embed_file(fmt("formula-%d.tekx", naf), "text/plain", "Source",
			    fmt("tekx source of formula %d", naf), e->source, date);
		if (naf_here) {
			bputs(b, " /AF [");
			for (int k = 0; k < naf_here; k++)
				bprintf(b, "%d 0 R%s", af[k], k + 1 < naf_here ? " " : "");
			bputc(b, ']');
		}
		bputs(b, " >>");
		*obj(elem_ref[i]) = eb;
	}

	Buf rmns = {0}, rm = {0};
	for (size_t i = 0; i < troles.n; i++) {
		bool dup = false;
		for (size_t k = 0; k < i; k++)
			dup |= streq(troles.v[k], troles.v[i]);
		if (dup)
			continue;
		int sns;
		const char *std = role_std(troles.v[i], &sns);
		bputc(&rmns, ' ');
		pname(&rmns, troles.v[i]);
		bputs(&rmns, " [");
		pname(&rmns, std);
		bprintf(&rmns, " %d 0 R]", ns[sns]);
		bputc(&rm, ' ');
		pname(&rm, troles.v[i]);
		bputc(&rm, ' ');
		pname(&rm, pdf17(std) ? pdf17(std) : std);
	}
	for (size_t i = 0; i < stdtags.n; i++) {
		bool dup = false;
		for (size_t k = 0; k < i; k++)
			dup |= streq(stdtags.v[k], stdtags.v[i]);
		if (dup || !pdf17(stdtags.v[i]))
			continue;
		bputc(&rm, ' ');
		pname(&rm, stdtags.v[i]);
		bputc(&rm, ' ');
		pname(&rm, pdf17(stdtags.v[i]));
	}
	Buf *nt = obj(ns[NS_TEKX]);
	bputs(nt, "<< /Type /Namespace /NS ");
	textstr(nt, TEKX_NS);
	bprintf(nt, " /RoleMapNS <<%s >> >>", rmns.s ? rmns.s : "");

	int ptree = alloc_obj();
	Buf *pt = obj(ptree);
	bputs(pt, "<< /Nums [");
	for (size_t pi = 0; pi < pages.n; pi++) {
		bprintf(pt, "%zu [", pi);
		for (size_t k = 0; k < mcid_elem[pi].n; k++)
			bprintf(pt, "%d 0 R%s", elem_ref[mcid_elem[pi].v[k]], k + 1 < mcid_elem[pi].n ? " " : "");
		bputs(pt, "] ");
	}
	for (size_t k = 0; k < annot_parent.n; k++)
		bprintf(pt, "%zu %d 0 R ", pages.n + k, elem_ref[annot_parent.v[k]]);
	pt->n--;
	bputs(pt, "] >>");
	/* element identifiers, as a name tree sorted by key */
	VEC(int) ids = {0};
	for (size_t i = 0; i < elems.n; i++)
		if (elems.v[i].ident) {
			size_t k = ids.n;
			VPUSH(ids, (int)i);
			while (k > 0 && strcmp(elems.v[ids.v[k - 1]].ident, elems.v[i].ident) > 0) {
				ids.v[k] = ids.v[k - 1];
				k--;
			}
			ids.v[k] = (int)i;
		}
	Buf idt = {0};
	if (ids.n) {
		int t = alloc_obj();
		Buf *tb = obj(t);
		bputs(tb, "<< /Limits [");
		textstr(tb, elems.v[ids.v[0]].ident);
		bputc(tb, ' ');
		textstr(tb, elems.v[ids.v[ids.n - 1]].ident);
		bputs(tb, "] /Names [");
		for (size_t k = 0; k < ids.n; k++) {
			textstr(tb, elems.v[ids.v[k]].ident);
			bprintf(tb, " %d 0 R ", elem_ref[ids.v[k]]);
		}
		bputs(tb, "] >>");
		bprintf(&idt, " /IDTree << /Kids [%d 0 R] >>", t);
	}
	bprintf(obj(sroot), "<< /Type /StructTreeRoot /K [%d 0 R] /ParentTree %d 0 R /ParentTreeNextKey %zu "
	    "/Namespaces [%d 0 R %d 0 R %d 0 R %d 0 R] /RoleMap <<%s >>%s >>",
	    elem_ref[0], ptree, pages.n + annot_parent.n, ns[0], ns[1], ns[2], ns[3], rm.s ? rm.s : "",
	    idt.s ? idt.s : "");

	/* bookmarks */
	int outlines = 0;
	if (outls.n) {
		size_t n = outls.n;
		int *ref = xmalloc(n * sizeof *ref), *par = xmalloc(n * sizeof *par);
		int stk[64], ssp = 0;
		outlines = alloc_obj();
		for (size_t i = 0; i < n; i++)
			ref[i] = alloc_obj();
		for (size_t i = 0; i < n; i++) {
			while (ssp > 0 && outls.v[stk[ssp - 1]].level >= outls.v[i].level)
				ssp--;
			par[i] = ssp ? stk[ssp - 1] : -1;
			if (ssp < 64)
				stk[ssp++] = (int)i;
		}
		for (size_t i = 0; i < n; i++) {
			Outl *o = &outls.v[i];
			int prev = -1, next = -1, first = -1, last = -1, count = 0;
			for (size_t k = 0; k < n; k++) {
				if (par[k] == par[i] && k < i)
					prev = (int)k;
				if (par[k] == par[i] && k > i && next < 0)
					next = (int)k;
				if (par[k] == (int)i) {
					if (first < 0)
						first = (int)k;
					last = (int)k;
				}
				for (int q = par[k]; q >= 0; q = par[q])
					if (q == (int)i)
						count++;
			}
			int tp = MIN(MAX(o->page, 0), (int)pages.n - 1);
			Buf *b = obj(ref[i]);
			bputs(b, "<< /Title ");
			textstr(b, o->title);
			bprintf(b, " /Parent %d 0 R /A << /S /GoTo /D [%d 0 R /XYZ null ", par[i] >= 0 ? ref[par[i]] : outlines, page_ref[tp]);
			bnum(b, pages.v[tp].h - o->y);
			bprintf(b, " null] /SD [%d 0 R /XYZ null ", elem_ref[o->elem]);
			bnum(b, pages.v[tp].h - o->y);
			bputs(b, " null] >>");
			if (prev >= 0)
				bprintf(b, " /Prev %d 0 R", ref[prev]);
			if (next >= 0)
				bprintf(b, " /Next %d 0 R", ref[next]);
			if (first >= 0)
				bprintf(b, " /First %d 0 R /Last %d 0 R /Count %d", ref[first], ref[last], count);
			bputs(b, " >>");
		}
		int first = -1, last = -1;
		for (size_t i = 0; i < n; i++)
			if (par[i] < 0) {
				if (first < 0)
					first = (int)i;
				last = (int)i;
			}
		bprintf(obj(outlines), "<< /Type /Outlines /First %d 0 R /Last %d 0 R /Count %zu >>", ref[first], ref[last], n);
	}

	int meta = alloc_obj();
	Buf x = {0};
	xmp(&x);
	stream_obj(meta, "/Type /Metadata /Subtype /XML", (u8 *)x.s, x.n, false);
	int icc = alloc_obj();
	Buf prof = {0};
	srgb_profile(&prof);
	stream_obj(icc, "/N 3", (u8 *)prof.s, prof.n, true);
	int files = files_tree();	/* before obj(catalog): allocating may move objs */
	Buf *cat = obj(catalog);
	bprintf(cat, "<< /Type /Catalog /Pages %d 0 R /StructTreeRoot %d 0 R /MarkInfo << /Marked true >> /Lang ", pages_ref, sroot);
	textstr(cat, language);
	bprintf(cat, " /Metadata %d 0 R /ViewerPreferences << /DisplayDocTitle true >>", meta);
	bprintf(cat, " /OutputIntents [<< /Type /OutputIntent /S /GTS_PDFA1 /OutputConditionIdentifier (sRGB IEC61966-2.1)"
	    " /RegistryName (http://www.color.org) /Info (sRGB IEC61966-2.1) /DestOutputProfile %d 0 R >>]", icc);
	if (files)
		bprintf(cat, " /Names << /EmbeddedFiles %d 0 R >>", files);
	if (outlines)
		bprintf(cat, " /Outlines %d 0 R /PageMode /UseOutlines", outlines);
	bputs(cat, " >>");

	/* assemble: every object that is not itself a stream goes into an
	 * object stream, and a compressed cross-reference stream indexes them */
	Buf file_out = {0}, idseed = {0};
	u8 id[16];
	bput(&idseed, x.s, x.n);
	for (size_t pi = 0; pi < pages.n; pi++)
		bput(&idseed, contents[pi].s ? contents[pi].s : "", contents[pi].n);
	md5((u8 *)idseed.s, idseed.n, id);

	size_t norig = objs.n;
	int *in_stream = xcalloc(norig + 1, sizeof *in_stream);	/* object stream number, 0: none */
	int *index = xcalloc(norig + 1, sizeof *index);
	VEC(int) batch = {0};
	for (size_t i = 0; i <= norig; i++) {
		bool flush = i == norig;
		if (i < norig) {
			Buf *o = &objs.v[i];
			bool is_stream = o->n >= 9 && memcmp(o->s + o->n - 9, "endstream", 9) == 0;
			if (!is_stream)
				VPUSH(batch, (int)i);
			flush = batch.n == 100;
		}
		if (!flush || batch.n == 0)
			continue;
		Buf hdr = {0}, body = {0};
		for (size_t k = 0; k < batch.n; k++) {
			Buf *o = &objs.v[batch.v[k]];
			bprintf(&hdr, "%d %zu ", batch.v[k] + 1, body.n);
			if (o->n)
				bput(&body, o->s, o->n);
			else
				bputs(&body, "null");
			bputc(&body, '\n');
		}
		size_t first = hdr.n;
		bput(&hdr, body.s, body.n);
		int os = alloc_obj();
		for (size_t k = 0; k < batch.n; k++) {
			in_stream[batch.v[k]] = os;
			index[batch.v[k]] = (int)k;
		}
		stream_obj(os, fmt("/Type /ObjStm /N %zu /First %zu", batch.n, first), (u8 *)hdr.s, hdr.n, true);
		batch.n = 0;
	}

	bputs(&file_out, "%PDF-2.0\n%\xE2\xE3\xCF\xD3\n");
	int xref_obj = alloc_obj();
	size_t *offs = xcalloc(objs.n + 1, sizeof *offs);
	for (size_t i = 0; i < objs.n; i++) {
		if ((i < norig && in_stream[i]) || (int)i + 1 == xref_obj)
			continue;
		offs[i] = file_out.n;
		bprintf(&file_out, "%zu 0 obj\n", i + 1);
		bput(&file_out, objs.v[i].s, objs.v[i].n);
		bputs(&file_out, "\nendobj\n");
	}
	size_t xref = file_out.n;
	offs[xref_obj - 1] = xref;
	Buf xs = {0};
	bput(&xs, "\0\0\0\0\0\xff\xff", 7);	/* object 0: free */
	for (size_t i = 0; i < objs.n; i++) {
		u8 e[7];
		bool packed = i < norig && in_stream[i];
		u32 f2 = packed ? (u32)in_stream[i] : (u32)offs[i];
		u32 f3 = packed ? (u32)index[i] : 0;
		e[0] = packed ? 2 : 1;
		e[1] = (u8)(f2 >> 24);
		e[2] = (u8)(f2 >> 16);
		e[3] = (u8)(f2 >> 8);
		e[4] = (u8)f2;
		e[5] = (u8)(f3 >> 8);
		e[6] = (u8)f3;
		bput(&xs, e, 7);
	}
	Buf dict = {0};
	bprintf(&dict, "/Type /XRef /Size %zu /W [1 4 2] /Root %d 0 R /ID [<", objs.n + 1, catalog);
	for (int k = 0; k < 2; k++) {
		for (int i = 0; i < 16; i++)
			bprintf(&dict, "%02X", id[i]);
		bputs(&dict, k ? ">]" : "> <");
	}
	stream_obj(xref_obj, bstr(&dict), (u8 *)xs.s, xs.n, true);
	bprintf(&file_out, "%d 0 obj\n", xref_obj);
	bput(&file_out, objs.v[xref_obj - 1].s, objs.v[xref_obj - 1].n);
	bputs(&file_out, "\nendobj\n");
	bprintf(&file_out, "startxref\n%zu\n%%%%EOF\n", xref);
	fwrite(file_out.s, 1, file_out.n, out);
	return ferror(out) ? 1 : 0;
}
