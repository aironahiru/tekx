/*
 * set.c -- the typesetter: document tree in, page description out.
 *
 * Paragraphs are broken into lines by the Knuth-Plass algorithm, with
 * Liang's hyphenation; lines are gathered into pages; sections, theorems
 * and equations are numbered and cross-referenced; and every glyph is
 * tied to the logical structure (PDF/UA), with real space characters,
 * ActualText for hyphenated words and formulas, and artifacts for
 * everything that is not content.
 *
 * Units are PostScript points; y grows downwards from the top of the page.
 */
#include "font.h"
#include "img.h"
#include "number.h"
#include "tree.h"

#include <math.h>
#include <time.h>

/* ---- page geometry and type sizes ---- */

static double PW = 595.276, PH = 841.89;	/* A4 */
static double BS = 11;				/* body size */
static double TW, LM, TOP, BOT;
static char *language = "en";

static double
lead_for(double size)
{
	return size * 1.24;
}

/* ---- fonts ---- */

enum { FACE_REG, FACE_IT, FACE_BF, FACE_BI, FACE_MONO, FACE_MATH, NFACE };
static const char *face_name[NFACE] = {"text", "italic", "bold", "bolditalic", "mono", "math"};
static int face_id[NFACE] = {-1, -1, -1, -1, -1, -1};
static VEC(int) font_order;

static int
pdl_font(int face)
{
	if (face_id[face] < 0) {
		face_id[face] = (int)font_order.n;
		VPUSH(font_order, face);
	}
	return face_id[face];
}

static Font *
face_font(int face)
{
	return logical_font(face_name[face]);
}

/* ---- structure ---- */

typedef struct {
	int parent;
	const char *ns;
	char *tag;
} El;

typedef struct {
	int el;
	char *key, *val;
} Prop;

typedef struct {
	int el;
	char *owner, *key, *val;
	char type;
} Attr;

static VEC(El) els;
static VEC(Prop) props;
static VEC(Attr) attrs;

static int
el(int parent, const char *ns, const char *tag)
{
	El e = {parent, ns, xstrdup(tag)};

	VPUSH(els, e);
	return (int)els.n - 1;
}

static void
prop(int e, const char *key, const char *val)
{
	Prop p = {e, xstrdup(key), xstrdup(val)};

	VPUSH(props, p);
}

static void
attr(int e, const char *owner, const char *key, char type, const char *val)
{
	Attr a = {e, xstrdup(owner), xstrdup(key), xstrdup(val), type};

	VPUSH(attrs, a);
}

/* ---- ActualText spans and links ---- */

typedef struct {
	char *text;
	bool active;
} Span;

static VEC(Span) spans;

static int
span(const char *text, bool active)
{
	Span s = {xstrdup(text), active};

	VPUSH(spans, s);
	return (int)spans.n - 1;
}

typedef struct {
	int el;
	char *url;	/* external, or NULL */
	char *label;	/* internal target label */
	char *contents;
} Link;

static VEC(Link) links;

/* ---- drawing operations ---- */

enum { O_GLYPH, O_INVIS, O_RULE, O_PAGEREF, O_IMAGE };
enum { ART_LAYOUT = -2, ART_PAGE = -3 };

typedef struct {
	int kind, face, gid;
	double size, x, y, adv, w, h;
	char *text;
	int el, span, link;
	char *label;	/* O_PAGEREF: target; O_IMAGE: file */
} Op;

typedef VEC(Op) Ops;

static void
op_glyph(Ops *ops, int face, double size, double x, double y, int gid, double adv, const char *text,
    int e, int sp, int link)
{
	Op o;

	memset(&o, 0, sizeof o);
	o.kind = O_GLYPH;
	o.face = face;
	o.size = size;
	o.x = x;
	o.y = y;
	o.gid = gid;
	o.adv = adv;
	o.text = xstrdup(text);
	o.el = e;
	o.span = sp;
	o.link = link;
	VPUSH(*ops, o);
}

static void
op_rule(Ops *ops, double x, double y, double w, double h, int sp)
{
	Op o;

	memset(&o, 0, sizeof o);
	o.kind = O_RULE;
	o.x = x;
	o.y = y;
	o.w = w;
	o.h = h;
	o.el = ART_LAYOUT;
	o.span = sp;
	o.link = -1;
	VPUSH(*ops, o);
}

static int cur_line;	/* source line of the block being set, for warnings */

/* ---- metadata ---- */

static char *meta_title, *meta_subtitle, *meta_date, *meta_keywords, *meta_subject, *meta_msc;
static VEC(char *) authors;
static VEC(Node *) author_nodes;	/* parallel to authors: the meta node, with details */

static char *
format_date(i64 t)
{
	static const char *month[] = {"January", "February", "March", "April", "May", "June", "July",
	    "August", "September", "October", "November", "December"};
	time_t tt = (time_t)t;
	struct tm tm;

	gmtime_r(&tt, &tm);
	return fmt("%d %s %d", tm.tm_mday, month[tm.tm_mon], tm.tm_year + 1900);
}

/* ---- horizontal lists ---- */

enum { H_BOX, H_GLUE, H_PEN };

typedef struct {
	int kind;
	double w, st, sh;
	bool fil;
	double h, d;
	int pen;
	bool flag;
	Ops ops;	/* box contents, or the hyphen drawn when breaking at a penalty */
	int span;	/* penalty: hyphenated word whose ActualText it activates */
	/* word spaces are drawn as real space glyphs */
	bool space;
	int sface, sgid, sel, sspan, slink;
	double ssize;
	int note;	/* footnote whose mark this box holds, 1-based; 0 none */
} H;

typedef VEC(H) HList;

typedef struct {
	int face;
	double size;
	int el;
	int link;
	bool nohyph;
} Ctx;

static void
hbox(HList *hl, Ops *ops, double w, double h, double d)
{
	H x;

	memset(&x, 0, sizeof x);
	x.kind = H_BOX;
	x.w = w;
	x.h = h;
	x.d = d;
	if (ops)
		x.ops = *ops;
	x.span = -1;
	VPUSH(*hl, x);
}

static void
hglue(HList *hl, double w, double st, double sh, bool fil)
{
	H x;

	memset(&x, 0, sizeof x);
	x.kind = H_GLUE;
	x.w = w;
	x.st = st;
	x.sh = sh;
	x.fil = fil;
	x.span = -1;
	VPUSH(*hl, x);
}

static void
hpen(HList *hl, int pen, bool flag, double w, Ops *ops, int sp)
{
	H x;

	memset(&x, 0, sizeof x);
	x.kind = H_PEN;
	x.pen = pen;
	x.flag = flag;
	x.w = w;
	if (ops)
		x.ops = *ops;
	x.span = sp;
	VPUSH(*hl, x);
}

static void
space_glue(HList *hl, Ctx c)
{
	Font *f = face_font(c.face);
	int g = font_glyph(f, ' ');
	double w = (g ? font_adv(f, g) : f->upem / 4) * c.size / f->upem;
	H x;

	if (c.face != FACE_MONO && hl->n && hl->v[hl->n - 1].kind == H_GLUE && hl->v[hl->n - 1].space)
		return;	/* collapse runs of spaces, except in code */
	memset(&x, 0, sizeof x);
	x.kind = H_GLUE;
	x.w = w;
	x.st = c.face == FACE_MONO ? 0 : w / 2;
	x.sh = c.face == FACE_MONO ? 0 : w / 3;
	x.space = true;
	x.sface = c.face;
	x.sgid = g;
	x.ssize = c.size;
	x.sel = c.el;
	x.sspan = -1;
	x.slink = c.link;
	x.span = -1;
	VPUSH(*hl, x);
}

/* shape text: glyphs, clusters, kerning; characters missing from the face
 * fall back to the text font, then the math font, never to .notdef */
typedef struct {
	VEC(u16) g;
	VEC(int) cl;	/* characters per glyph */
	VEC(double) adv;
	VEC(char *) text;
	VEC(int) start;	/* index of each glyph's first character */
	VEC(int) face;
} Shaped;

static int
face_for(u32 cp, int face)
{
	static const int order[] = {-1, FACE_REG, FACE_MATH};

	for (size_t i = 0; i < NELEM(order); i++) {
		int f = order[i] < 0 ? face : order[i];
		if (font_glyph(face_font(f), cp))
			return f;
	}
	return -1;
}

static void
shape_run(Shaped *r, const char *s, size_t a, size_t b, int ci0, int face, double size)
{
	Font *f = face_font(face);
	VEC(u16) g = {0};
	VEC(int) cl = {0};
	VEC(int) offs = {0};

	for (size_t i = a; i < b;) {
		u32 cp;
		int k = utf8_dec(s + i, b - i, &cp);
		VPUSH(g, (u16)font_glyph(f, cp == 0xa0 ? ' ' : cp));
		VPUSH(cl, 1);
		VPUSH(offs, (int)i);
		i += (size_t)k;
	}
	VPUSH(offs, (int)b);
	int ng = (int)g.n;
	if (ng && face != FACE_MONO && face != FACE_MATH)
		ng = gsub_apply(f, "liga", g.v, cl.v, ng);
	int ci = 0;
	for (int i = 0; i < ng; i++) {
		int x = offs.v[ci], y = offs.v[MIN(ci + cl.v[i], (int)offs.n - 1)];
		char *t = xstrndup(s + x, (size_t)(y - x));
		if (streq(t, "\u00a0"))
			t = " ";
		double adv = font_adv(f, g.v[i]);
		if (i + 1 < ng)
			adv += font_kern(f, g.v[i], g.v[i + 1]);
		VPUSH(r->g, g.v[i]);
		VPUSH(r->cl, cl.v[i]);
		VPUSH(r->adv, adv * size / f->upem);
		VPUSH(r->text, t);
		VPUSH(r->start, ci0 + ci);
		VPUSH(r->face, face);
		ci += cl.v[i];
	}
}

static Shaped
shape(const char *s, Ctx c)
{
	Shaped r;
	size_t n = strlen(s), a = 0;
	int ci = 0, run_face = -2, run_ci = 0;

	memset(&r, 0, sizeof r);
	for (size_t i = 0; i <= n;) {
		u32 cp = 0;
		int k = i < n ? utf8_dec(s + i, n - i, &cp) : 0;
		int f = i < n ? face_for(cp == 0xa0 ? ' ' : cp, c.face) : -3;
		if (i < n && f < 0) {
			warn_at(cur_line, 0, "no font has a glyph for U+%04X; it is left out", (unsigned)cp);
		}
		if (f != run_face) {
			if (run_face >= 0)
				shape_run(&r, s, a, i, run_ci, run_face, c.size);
			run_face = f;
			a = i;
			run_ci = ci;
		}
		if (i == n)
			break;
		if (f < 0) {
			/* skip the character entirely */
			a = i + (size_t)k;
			run_face = -2;
		}
		i += (size_t)k;
		ci++;
	}
	return r;
}

static void
add_word(HList *hl, const char *w, Ctx c)
{
	Shaped s = shape(w, c);
	int ng = (int)s.g.n;
	double h = c.size * 0.72, d = c.size * 0.22;

	/* hyphenation points, by character index */
	int nchar = (int)utf8_len(w);
	unsigned char *pts = xcalloc((size_t)nchar + 2, 1);
	int sp = -1;
	if (!c.nohyph && nchar >= 5 && nchar < 250) {
		/* strip leading and trailing punctuation */
		size_t a = 0, b = strlen(w);
		while (a < b && !((w[a] | 32) >= 'a' && (w[a] | 32) <= 'z'))
			a++;
		while (b > a && !((w[b - 1] | 32) >= 'a' && (w[b - 1] | 32) <= 'z'))
			b--;
		char *core = xstrndup(w + a, b - a);
		bool ascii = b - a >= 5;
		for (char *p = core; *p; p++) {
			if (*p >= 'A' && *p <= 'Z')
				*p = (char)(*p + 32);
			if (*p < 'a' || *p > 'z')
				ascii = false;
		}
		/* character index = byte index for ASCII cores; a counts leading bytes */
		bool ascii_prefix = true;
		for (size_t k = 0; k < a; k++)
			if ((unsigned char)w[k] >= 0x80)
				ascii_prefix = false;
		if (ascii && ascii_prefix && !(w[a] >= 'A' && w[a] <= 'Z' && w[a + 1] >= 'A' && w[a + 1] <= 'Z')) {
			unsigned char *cp = xcalloc(b - a + 2, 1);
			if (hyph_points(core, cp) > 0) {
				for (size_t k = 0; k < b - a; k++)
					if (cp[k])
						pts[a + k] = 1;
				sp = span(w, false);
			}
		}
	}
	Font *f = face_font(c.face);
	int hy = font_glyph(f, '-');
	double hyw = hy ? font_adv(f, hy) * c.size / f->upem : c.size / 3;
	Ops cur = {0};
	double x = 0;
	for (int i = 0; i < ng; i++) {
		if (i > 0 && pts[s.start.v[i]] && sp >= 0) {
			hbox(hl, &cur, x, h, d);
			Ops hops = {0};
			op_glyph(&hops, c.face, c.size, 0, 0, hy, hyw, "-", c.el, sp, c.link);
			hpen(hl, 50, true, hyw, &hops, sp);
			memset(&cur, 0, sizeof cur);
			x = 0;
		}
		op_glyph(&cur, s.face.v[i], c.size, x, 0, s.g.v[i], s.adv.v[i], s.text.v[i], c.el, sp, c.link);
		x += s.adv.v[i];
	}
	hbox(hl, &cur, x, h, d);
}

static void
add_text(HList *hl, const char *s, Ctx c)
{
	const char *p = s;

	while (*p) {
		if (*p == ' ') {
			space_glue(hl, c);
			p++;
			continue;
		}
		const char *e = p;
		while (*e && *e != ' ')
			e++;
		char *w = xstrndup(p, (size_t)(e - p));
		/* allow breaks after em and en dashes and slashes */
		char *q = w, *br;
		while ((br = strstr(q, "—")) != NULL || (br = strstr(q, "–")) != NULL) {
			size_t k = (size_t)(br - q) + 3;
			add_word(hl, xstrndup(q, k), c);
			if (br[3])
				hpen(hl, 50, false, 0, NULL, -1);
			q += k;
		}
		if (*q)
			add_word(hl, q, c);
		p = e;
	}
}

/* ---- formulas ---- */

static int formula_count;

/* convert a laid-out formula (children of a math, line or let node) to ops */
typedef struct {
	Ops ops;
	double w, h, d, align;
	int formula;	/* Formula element */
	char *linear, *speech;
} Formula;

static Formula
formula(Node *n, int parent, double size)
{
	Formula f;
	VEC(int) nodes = {0};
	double k = size / 1000;

	memset(&f, 0, sizeof f);
	f.align = NAN;
	f.formula = el(parent, "pdf2", "Formula");
	formula_count++;
	Node *lin = child(n, "linear"), *sp = child(n, "speech"), *mm = child(n, "mathml");
	f.linear = lin ? lin->rest : n->rest;
	f.speech = sp ? sp->rest : f.linear;
	prop(f.formula, "alt", f.speech);
	if (mm)
		prop(f.formula, "mathml", mm->rest);
	prop(f.formula, "source", streq(n->kind, "line") ? field_rest(n, 1) : n->rest);
	int s = span(f.linear, true);
	for (size_t i = 0; i < n->kids.n; i++) {
		Node *c = n->kids.v[i];
		char *r = xstrdup(c->rest);	/* next_field cuts its string: keep the node intact */
		if (streq(c->kind, "box")) {
			f.w = atof(next_field(&r)) * k;
			f.h = atof(next_field(&r)) * k;
			f.d = atof(next_field(&r)) * k;
			char *a = next_field(&r);
			f.align = streq(a, "-") ? NAN : atof(a) * k;
		} else if (streq(c->kind, "n")) {
			next_field(&r);
			char *par = next_field(&r);
			char *tag = next_field(&r);
			int pe = streq(par, "-") ? f.formula : nodes.v[atoi(par)];
			int e = el(pe, "mathml", tag);
			if (*r)
				prop(e, "actual", pct_unescape(r));
			VPUSH(nodes, e);
		} else if (streq(c->kind, "g") || streq(c->kind, "i")) {
			char *font = next_field(&r);
			double sc = atof(next_field(&r)) / 1000;
			double x = atof(next_field(&r)) * k, y = atof(next_field(&r)) * k;
			int face = streq(font, "text") ? FACE_REG : FACE_MATH;
			if (c->kind[0] == 'g') {
				int gid = atoi(next_field(&r));
				double adv = atof(next_field(&r)) * k;
				char *nd = next_field(&r);
				int e = streq(nd, "-") ? ART_LAYOUT : nodes.v[atoi(nd)];
				op_glyph(&f.ops, face, size * sc, x, y, gid, adv, pct_unescape(r), e, s, -1);
			} else {
				char *t = pct_unescape(r);
				Font *ft = face_font(face);
				double xx = x;
				for (const char *q = t; *q;) {
					u32 cp;
					int len = utf8_dec(q, strlen(q), &cp);
					int g = font_glyph(ft, cp);
					double adv = g ? font_adv(ft, g) * size * sc / ft->upem : 0;
					if (g) {
						op_glyph(&f.ops, face, size * sc, xx, y, g, adv, xstrndup(q, (size_t)len), ART_LAYOUT, s, -1);
						f.ops.v[f.ops.n - 1].kind = O_INVIS;
					}
					xx += adv;
					q += len;
				}
			}
		} else if (streq(c->kind, "r")) {
			double x = atof(next_field(&r)) * k, y = atof(next_field(&r)) * k;
			double w = atof(next_field(&r)) * k, h = atof(next_field(&r)) * k;
			op_rule(&f.ops, x, y, w, h, s);
		}
	}
	return f;
}

static void
add_math(HList *hl, Node *n, Ctx c)
{
	Formula f = formula(n, c.el, c.size);
	VEC(double) brk = {0};
	double k = c.size / 1000;

	for (size_t i = 0; i < f.ops.n; i++)
		f.ops.v[i].link = c.link;
	for (size_t i = 0; i < n->kids.n; i++)
		if (streq(n->kids.v[i]->kind, "brk")) {
			char *r = xstrdup(n->kids.v[i]->rest);
			VPUSH(brk, atof(next_field(&r)) * k);
			VPUSH(brk, atof(next_field(&r)) * k);
			VPUSH(brk, atof(next_field(&r)));
		}
	/* split the formula where it may break across lines */
	double start = 0;
	size_t next = 0;
	for (size_t b = 0; b <= brk.n; b += 3) {
		double end = b < brk.n ? brk.v[b] : f.w;
		Ops piece = {0};
		for (; next < f.ops.n; next++) {
			Op o = f.ops.v[next];
			if (b < brk.n && o.x >= end - 0.001)
				break;
			o.x -= start;
			VPUSH(piece, o);
		}
		hbox(hl, &piece, end - start, f.h, f.d);
		if (b < brk.n) {
			hpen(hl, (int)brk.v[b + 2], false, 0, NULL, -1);
			hglue(hl, brk.v[b + 1] - end, 0, 0, false);
			start = brk.v[b + 1];
		}
	}
}

static void add_inlines(HList *hl, Node *n, Ctx c);
static void add_note(HList *hl, Node *k, Ctx c);
static void add_cite(HList *hl, Node *k, Ctx c);

static int
new_link(int parent, const char *url, const char *label, const char *contents)
{
	Link l;

	l.el = el(parent, "pdf2", "Link");
	l.url = url ? xstrdup(url) : NULL;
	l.label = label ? xstrdup(label) : NULL;
	l.contents = xstrdup(contents);
	VPUSH(links, l);
	return (int)links.n - 1;
}

static void
add_inline(HList *hl, Node *k, Ctx c)
{
	if (streq(k->kind, "text")) {
		add_text(hl, k->rest, c);
	} else if (streq(k->kind, "emph")) {
		Ctx e = c;
		e.face = c.face == FACE_IT ? FACE_REG : c.face == FACE_BF ? FACE_BI : c.face == FACE_BI ? FACE_BF : FACE_IT;
		e.el = el(c.el, "pdf2", "Em");
		add_inlines(hl, k, e);
	} else if (streq(k->kind, "strong")) {
		Ctx e = c;
		e.face = c.face == FACE_IT || c.face == FACE_BI ? FACE_BI : FACE_BF;
		e.el = el(c.el, "pdf2", "Strong");
		add_inlines(hl, k, e);
	} else if (streq(k->kind, "code")) {
		Ctx e = c;
		e.face = FACE_MONO;
		e.size = c.size * 0.95;
		e.el = el(c.el, "pdf", "Code");
		e.nohyph = true;
		add_text(hl, k->rest, e);
	} else if (streq(k->kind, "math")) {
		add_math(hl, k, c);
	} else if (streq(k->kind, "link")) {
		Ctx e = c;
		e.link = new_link(c.el, k->rest, NULL, plain_text(k));
		e.el = links.v[e.link].el;
		add_inlines(hl, k, e);
	} else if (streq(k->kind, "url")) {
		Ctx e = c;
		e.link = new_link(c.el, k->rest, NULL, k->rest);
		e.el = links.v[e.link].el;
		e.face = FACE_MONO;
		e.size = c.size * 0.95;
		e.nohyph = true;
		/* allow breaks after slashes in long addresses */
		const char *p = k->rest;
		while (*p) {
			const char *q = strchr(p, '/');
			size_t len = q ? (size_t)(q - p) + 1 : strlen(p);
			add_word(hl, xstrndup(p, len), e);
			p += len;
			if (*p)
				hpen(hl, 100, false, 0, NULL, -1);
		}
	} else if (streq(k->kind, "note")) {
		add_note(hl, k, c);
	} else if (streq(k->kind, "cite")) {
		add_cite(hl, k, c);
	} else if (streq(k->kind, "ref") || streq(k->kind, "refnum")) {
		Label *l = find_label(k->rest);
		if (l == NULL) {
			const char *best = NULL;
			int bd = 3;
			for (size_t i = 0; i < labels.n; i++) {
				int d = edit_distance(k->rest, labels.v[i].name);
				if (d < bd) {
					bd = d;
					best = labels.v[i].name;
				}
			}
			if (best)
				error_at(k->line, k->col, "unknown label @%s; did you mean @%s?", k->rest, best);
			else
				error_at(k->line, k->col, "unknown label @%s (mark the target with #%s)", k->rest, k->rest);
			add_text(hl, fmt("@%s", k->rest), c);
			return;
		}
		Ctx e = c;
		e.link = new_link(c.el, NULL, l->name, l->spoken);
		e.el = links.v[e.link].el;
		e.nohyph = true;
		/* keep "Theorem 3" together */
		const char *t = streq(k->kind, "refnum") ? l->number : l->text, *spc = strchr(t, ' ');
		if (spc) {
			add_word(hl, xstrndup(t, (size_t)(spc - t)), e);
			Font *f = face_font(e.face);
			int g = font_glyph(f, ' ');
			double w = font_adv(f, g) * e.size / f->upem;
			Ops o = {0};
			op_glyph(&o, e.face, e.size, 0, 0, g, w, " ", e.el, -1, e.link);
			hbox(hl, &o, w, 0, 0);
			add_word(hl, spc + 1, e);
		} else {
			add_word(hl, t, e);
		}
	}
}

/*
 * A citation: [3], [1, 2], [1–4], [2, Thm. 2], [1; 2, Thm. 2].  Without
 * notes the numbers are sorted and runs of three or more become ranges.
 * Each number is a link to its entry that says "Reference 2".
 */
typedef struct {
	Label *l;
	Node *item;
	int n;
} CiteItem;

static int
by_number(const void *a, const void *b)
{
	return ((const CiteItem *)a)->n - ((const CiteItem *)b)->n;
}

static void
cite_number(HList *hl, CiteItem *ci, Ctx c)
{
	Ctx e = c;
	e.link = new_link(c.el, NULL, ci->l->name, ci->l->spoken);
	e.el = links.v[e.link].el;
	e.nohyph = true;
	add_word(hl, ci->l->number, e);
}

static void
add_cite(HList *hl, Node *k, Ctx c)
{
	VEC(CiteItem) v = {0};
	bool notes = false;

	for (size_t i = 0; i < k->kids.n; i++) {
		Node *it = k->kids.v[i];
		if (!streq(it->kind, "item"))
			continue;
		Label *l = find_label(cite_label(it->rest));
		if (l == NULL)
			continue;	/* parse has said so */
		CiteItem ci = {l, it, atoi(l->number)};
		VPUSH(v, ci);
		notes |= it->kids.n > 0;
	}
	if (v.n == 0)
		return;
	if (!notes)
		qsort(v.v, v.n, sizeof *v.v, by_number);
	add_word(hl, "[", c);
	for (size_t i = 0; i < v.n; i++) {
		if (i > 0) {
			add_word(hl, notes ? ";" : ",", c);
			space_glue(hl, c);
		}
		size_t j = i;
		while (!notes && j + 1 < v.n && v.v[j + 1].n == v.v[j].n + 1)
			j++;
		cite_number(hl, &v.v[i], c);
		if (j >= i + 2) {
			add_word(hl, "–", c);
			cite_number(hl, &v.v[j], c);
			i = j;
			continue;
		}
		if (v.v[i].item->kids.n) {
			add_word(hl, ",", c);
			space_glue(hl, c);
			add_inlines(hl, v.v[i].item, c);
		}
	}
	add_word(hl, "]", c);
}

static void
add_inlines(HList *hl, Node *n, Ctx c)
{
	for (size_t i = 0; i < n->kids.n; i++)
		add_inline(hl, n->kids.v[i], c);
}

/* ---- Knuth-Plass line breaking ---- */

typedef struct {
	int pos, line, fit, prev;
	double dem;
} BNode;

static int
line_start(HList *hl, int brk)
{
	int s = brk + 1;

	while (s < (int)hl->n && hl->v[s].kind != H_BOX)
		s++;
	return s;
}

static bool
legal(HList *hl, int b)
{
	H *it = &hl->v[b];

	if (it->kind == H_GLUE)
		return b > 0 && hl->v[b - 1].kind == H_BOX;
	if (it->kind == H_PEN)
		return it->pen < 10000;
	return false;
}

/* returns break positions (indices of the breaking items); width per line from lw(i) */
static int *
knuth_plass(HList *hl, double width, double first_width, double tolerance, int *nbreaks)
{
	int n = (int)hl->n;
	double *sw = xcalloc((size_t)n + 1, sizeof *sw), *sy = xcalloc((size_t)n + 1, sizeof *sy),
	    *sz = xcalloc((size_t)n + 1, sizeof *sz);
	int *sf = xcalloc((size_t)n + 1, sizeof *sf);
	VEC(BNode) nodes = {0};
	VEC(int) active = {0};
	double rmax = cbrt(tolerance / 100.0);

	for (int i = 0; i < n; i++) {
		H *it = &hl->v[i];
		sw[i + 1] = sw[i] + (it->kind == H_PEN ? 0 : it->w);
		sy[i + 1] = sy[i] + (it->kind == H_GLUE ? it->st : 0);
		sz[i + 1] = sz[i] + (it->kind == H_GLUE ? it->sh : 0);
		sf[i + 1] = sf[i] + (it->kind == H_GLUE && it->fil);
	}
	BNode root = {-1, 0, 1, -1, 0};
	VPUSH(nodes, root);
	VPUSH(active, 0);
	for (int b = 0; b < n; b++) {
		if (!legal(hl, b))
			continue;
		H *it = &hl->v[b];
		double pw = it->kind == H_PEN ? it->w : 0;
		bool forced = it->kind == H_PEN && it->pen <= -10000;
		double best[4] = {INFINITY, INFINITY, INFINITY, INFINITY};
		int bestp[4] = {-1, -1, -1, -1};
		for (size_t k = 0; k < active.n;) {
			BNode *a = &nodes.v[active.v[k]];
			int start = line_start(hl, a->pos);
			if (start > b) {
				k++;
				continue;
			}
			double lw = a->line == 0 ? first_width : width;
			double L = sw[b] - sw[start] + pw;
			double Y = sy[b] - sy[start], Z = sz[b] - sz[start];
			int F = sf[b] - sf[start];
			double r;
			if (L < lw)
				r = F ? 0 : Y > 0 ? (lw - L) / Y : INFINITY;
			else if (L > lw)
				r = Z > 0 ? (lw - L) / Z : -INFINITY;
			else
				r = 0;
			if (r >= -1 && r <= rmax) {
				double bad = 100 * pow(fabs(r), 3);
				double d = (10 + bad) * (10 + bad);
				if (it->kind == H_PEN && it->pen > 0)
					d += (double)it->pen * it->pen;
				else if (it->kind == H_PEN && it->pen < 0 && !forced)
					d -= (double)it->pen * it->pen;
				if (it->flag && a->pos >= 0 && hl->v[a->pos].kind == H_PEN && hl->v[a->pos].flag)
					d += 3000;
				int fit = r < -0.5 ? 0 : r <= 0.5 ? 1 : r <= 1 ? 2 : 3;
				if (abs(fit - a->fit) > 1)
					d += 10000;
				d += a->dem;
				if (d < best[fit]) {
					best[fit] = d;
					bestp[fit] = active.v[k];
				}
			}
			if (r < -1 || forced) {
				memmove(&active.v[k], &active.v[k + 1], (active.n - k - 1) * sizeof *active.v);
				active.n--;
			} else {
				k++;
			}
		}
		for (int f = 0; f < 4; f++) {
			if (bestp[f] < 0)
				continue;
			BNode nn = {b, nodes.v[bestp[f]].line + 1, f, bestp[f], best[f]};
			VPUSH(nodes, nn);
			VPUSH(active, (int)nodes.n - 1);
		}
		if (active.n == 0)
			return NULL;
	}
	int bestn = -1;
	for (size_t k = 0; k < active.n; k++) {
		BNode *a = &nodes.v[active.v[k]];
		if (a->pos == n - 1 && (bestn < 0 || a->dem < nodes.v[bestn].dem))
			bestn = active.v[k];
	}
	if (bestn < 0)
		return NULL;
	int count = nodes.v[bestn].line;
	int *br = xcalloc((size_t)count + 1, sizeof *br);
	for (int x = bestn, i = count - 1; x > 0; x = nodes.v[x].prev, i--)
		br[i] = nodes.v[x].pos;
	*nbreaks = count;
	return br;
}

/* first fit, used when no acceptable set of breaks exists */
static int *
first_fit(HList *hl, double width, double first_width, int *nbreaks)
{
	VEC(int) br = {0};
	int n = (int)hl->n, start = 0;
	double L = 0;
	int last_legal = -1;

	for (int b = 0; b < n; b++) {
		H *it = &hl->v[b];
		double lw = br.n == 0 ? first_width : width;
		if (it->kind == H_PEN && it->pen <= -10000) {
			VPUSH(br, b);
			start = line_start(hl, b);
			b = start - 1;
			L = 0;
			last_legal = -1;
			continue;
		}
		if (legal(hl, b) && b > start)
			last_legal = b;
		if (it->kind != H_PEN)
			L += it->w;
		if (L > lw && last_legal >= 0) {
			VPUSH(br, last_legal);
			start = line_start(hl, last_legal);
			b = start - 1;
			L = 0;
			last_legal = -1;
		}
	}
	if (br.n == 0 || br.v[br.n - 1] != n - 1)
		VPUSH(br, n - 1);
	*nbreaks = (int)br.n;
	return br.v;
}

/* ---- vertical list ---- */

typedef struct {
	double before, h, d, lead, x;
	Ops ops;
	bool keep;	/* keep with the next line */
	bool newpage;
	VEC(int) dests;	/* elements whose destination is this line */
	VEC(int) outline;	/* indices into outlines starting here */
	VEC(int) notes;	/* footnotes marked on this line, 1-based */
	int float_id;	/* lines of one figure share an id; 0 for text */
	double y;	/* baseline, after pagination */
	int page;
} V;

typedef VEC(V) VList;
static VList vl;

/* footnotes: laid out where they are met, placed at the foot of the page
 * that carries their mark */
typedef struct {
	int el;		/* FENote */
	VList lines;
	double h;
} Note;

static VEC(Note) notes;
static double pending_space;
static VEC(int) pending_dests;
static VEC(int) pending_outlines;

typedef struct {
	int level, el;
	char *title;
	int vi;
} Outline;

static VEC(Outline) outlines;

static void
vspace(double s)
{
	pending_space = MAX(pending_space, s);
}

static void
dest(int e)
{
	VPUSH(pending_dests, e);
}

static V *
vline(double h, double d, double lead, double x, Ops *ops)
{
	V v;

	memset(&v, 0, sizeof v);
	v.before = pending_space;
	v.h = h;
	v.d = d;
	v.lead = lead;
	v.x = x;
	if (ops)
		v.ops = *ops;
	v.dests.v = pending_dests.v;
	v.dests.n = pending_dests.n;
	v.dests.cap = pending_dests.cap;
	memset(&pending_dests, 0, sizeof pending_dests);
	for (size_t i = 0; i < pending_outlines.n; i++)
		outlines.v[pending_outlines.v[i]].vi = (int)vl.n;
	pending_outlines.n = 0;
	pending_space = 0;
	VPUSH(vl, v);
	return &vl.v[vl.n - 1];
}

typedef struct {
	double x, w;	/* frame: left offset and width */
} Fr;

/* break a paragraph and append its lines; returns the number of lines */
static int
set_par(HList *hl, Fr fr, double size, double indent_first, bool justify, bool last_keep_qed)
{
	/* drop trailing spaces, then finish the paragraph */
	while (hl->n && hl->v[hl->n - 1].kind == H_GLUE)
		hl->n--;
	if (hl->n == 0)
		return 0;
	if (!last_keep_qed) {
		hpen(hl, 10000, false, 0, NULL, -1);
		hglue(hl, 0, 0, 0, true);
	}
	hpen(hl, -10000, false, 0, NULL, -1);
	if (!justify) {
		/* ragged right: every space may stretch without limit */
		for (size_t i = 0; i < hl->n; i++)
			if (hl->v[i].kind == H_GLUE && hl->v[i].space) {
				hl->v[i].st = 0;
				hl->v[i].sh = 0;
			}
	}
	int nb = 0;
	int *br = NULL;
	double fw = fr.w - indent_first;
	if (justify) {
		br = knuth_plass(hl, fr.w, fw, 200, &nb);
		if (!br)
			br = knuth_plass(hl, fr.w, fw, 2000, &nb);
		if (!br)
			br = knuth_plass(hl, fr.w, fw, 1e7, &nb);
	}
	if (!br)
		br = first_fit(hl, fr.w, fw, &nb);
	int prev = -1;
	double lead = lead_for(size);
	size_t first_v = vl.n;
	for (int li = 0; li < nb; li++) {
		int b = br[li], start = line_start(hl, prev);
		double lw = li == 0 ? fw : fr.w;
		double L = 0, Y = 0, Z = 0;
		bool fil = false;
		H *bi = &hl->v[b];
		for (int i = start; i < b; i++) {
			H *it = &hl->v[i];
			if (it->kind == H_PEN)
				continue;
			L += it->w;
			if (it->kind == H_GLUE) {
				Y += it->st;
				Z += it->sh;
				fil |= it->fil;
			}
		}
		if (bi->kind == H_PEN)
			L += bi->w;
		double r = 0, filw = 0;
		if (fil && L < lw) {
			int nfil = 0;
			for (int i = start; i < b; i++)
				nfil += hl->v[i].kind == H_GLUE && hl->v[i].fil;
			filw = (lw - L) / MAX(nfil, 1);
		}
		if (!fil && justify && !(bi->kind == H_PEN && bi->pen <= -10000)) {
			if (L < lw && Y > 0)
				r = (lw - L) / Y;
			else if (L > lw && Z > 0)
				r = MAX(-1, (lw - L) / Z);
		}
		if (L > lw + 0.5 && (L - lw) > 1 && Z * 1 < L - lw)
			warn_at(cur_line, 0, "line too long by %.1fpt (it could not be broken)", L - lw);
		Ops ops = {0};
		VEC(int) line_notes = {0};
		double x = li == 0 ? indent_first : 0, h = size * 0.72, d = size * 0.22;
		for (int i = start; i < b; i++) {
			H *it = &hl->v[i];
			if (it->kind == H_BOX) {
				for (size_t k = 0; k < it->ops.n; k++) {
					Op o = it->ops.v[k];
					o.x += x;
					VPUSH(ops, o);
				}
				if (it->note)
					VPUSH(line_notes, it->note);
				x += it->w;
				h = MAX(h, it->h);
				d = MAX(d, it->d);
			} else if (it->kind == H_GLUE) {
				double w = it->w + (fil ? (it->fil ? filw : 0) : r > 0 ? r * it->st : r * it->sh);
				if (it->space && it->sgid)
					op_glyph(&ops, it->sface, it->ssize, x, 0, it->sgid, w, " ", it->sel, it->sspan, it->slink);
				x += w;
			}
		}
		if (bi->kind == H_PEN && bi->w > 0) {
			for (size_t k = 0; k < bi->ops.n; k++) {
				Op o = bi->ops.v[k];
				o.x += x;
				VPUSH(ops, o);
			}
			if (bi->span >= 0)
				spans.v[bi->span].active = true;
		} else if (bi->kind == H_GLUE && bi->space && bi->sgid) {
			/* keep the real space at the end of the line for copy and paste */
			op_glyph(&ops, bi->sface, bi->ssize, x, 0, bi->sgid, bi->w, " ", bi->sel, bi->sspan, bi->slink);
		}
		V *v = vline(h, d, lead, fr.x, &ops);
		v->notes.v = line_notes.v;
		v->notes.n = line_notes.n;
		v->notes.cap = line_notes.cap;
		prev = b;
	}
	/* widows and orphans: keep the first two and the last two lines together */
	size_t nl = vl.n - first_v;
	if (nl >= 2) {
		vl.v[first_v].keep = true;
		vl.v[vl.n - 2].keep = true;
	}
	return nb;
}

/* ---- footnotes ---- */

#define NOTE_SIZE (BS * 0.82)
#define NOTE_GAP (BS * 0.15)
#define NOTE_RULE_SPACE (BS * 1.0)

/* A footnote: the mark is a link to the note; the note is an FENote placed
 * right after its mark in the logical order, laid out now and placed at the
 * foot of the page when the page is made. */
static void
add_note(HList *hl, Node *k, Ctx c)
{
	int n = (int)notes.n + 1;
	char *name = fmt("tekx-note-%d", n);
	char *num = fmt("%d", n);

	add_label(name, num, num, k->line);
	int link = new_link(c.el, NULL, name, fmt("Footnote %d", n));
	int fe = el(c.el, "pdf2", "FENote");
	attr(fe, "PDF", "NoteType", 'n', "Footnote");
	label_el(name, fe);

	/* the mark: a small raised number */
	Font *f = face_font(FACE_REG);
	double ms = c.size * 0.68, x = 0;
	Ops o = {0};
	for (const char *d = num; *d; d++) {
		int g = font_glyph(f, (u32)*d);
		double adv = font_adv(f, g) * ms / f->upem;
		op_glyph(&o, FACE_REG, ms, x, -c.size * 0.36, g, adv, xstrndup(d, 1), links.v[link].el, -1, link);
		x += adv;
	}
	hbox(hl, &o, x, c.size * 0.82, 0);
	hl->v[hl->n - 1].note = n;

	/* the note itself, set into a list of its own */
	VList saved = vl;
	double saved_space = pending_space;
	memset(&vl, 0, sizeof vl);
	pending_space = 0;
	int lbl = el(fe, "pdf2", "Lbl"), p = el(fe, "pdf2", "P");
	HList nh = {0};
	Ctx lc = {FACE_REG, NOTE_SIZE, lbl, -1, true};
	Ops lo = {0};
	double w = 0;
	for (const char *d = num; *d; d++) {
		int g = font_glyph(f, (u32)*d);
		double adv = font_adv(f, g) * NOTE_SIZE / f->upem;
		op_glyph(&lo, FACE_REG, NOTE_SIZE * 0.75, w, -NOTE_SIZE * 0.3, g, adv * 0.75, xstrndup(d, 1), lbl, -1, -1);
		w += adv * 0.75;
	}
	(void)lc;
	hbox(&nh, &lo, w + NOTE_SIZE * 0.3, NOTE_SIZE * 0.72, 0);
	Ctx pc = {FACE_REG, NOTE_SIZE, p, -1, false};
	add_inlines(&nh, k, pc);
	Fr fr = {0, TW};
	set_par(&nh, fr, NOTE_SIZE, 0, true, false);
	Note note = {fe, vl, 0};
	for (size_t i = 0; i < vl.n; i++)
		note.h += i ? vl.v[i].lead : vl.v[i].h + vl.v[i].d;
	VPUSH(notes, note);
	vl = saved;
	pending_space = saved_space;
}

/* ---- block layout ---- */

static bool prev_was_text;	/* the block before was running text */
static int doc_el;
static bool after_heading;

typedef struct {
	int el;		/* parent structure element */
	Fr fr;
	double size;
	int face;	/* body face (theorem bodies are italic) */
} Box_;

static void classification(Box_ bx);

static void blocks(Node *n, Box_ bx);

static Ctx
ctx(Box_ bx, int e)
{
	Ctx c = {bx.face, bx.size, e, -1, false};

	return c;
}

/* the end-of-proof mark at the right margin of the last line */
static void
add_qed(HList *hl, int p, double size)
{
	int qe = el(p, "tekx", "qed");
	Font *f = face_font(FACE_MATH);
	int g = font_glyph(f, 0x220e);
	double w = font_adv(f, g) * size / f->upem;
	Ops o = {0};

	while (hl->n && hl->v[hl->n - 1].kind == H_GLUE)
		hl->n--;
	hpen(hl, 10000, false, 0, NULL, -1);
	hglue(hl, size * 0.5, 0, 0, false);
	hpen(hl, 0, false, 0, NULL, -1);
	hglue(hl, 0, 0, 0, true);
	op_glyph(&o, FACE_MATH, size, 0, 0, g, w, "∎", qe, -1, -1);
	hbox(hl, &o, w, size * 0.7, 0);
}

static void
paragraph(Node *n, Box_ bx, HList *prefix, bool indent, bool qed)
{
	int p = el(bx.el, "pdf2", "P");
	HList hl = {0};
	Ctx c = ctx(bx, p);

	if (prefix)
		for (size_t i = 0; i < prefix->n; i++) {
			H h = prefix->v[i];
			for (size_t k = 0; k < h.ops.n; k++)
				if (h.ops.v[k].el == -1)
					h.ops.v[k].el = p;
			if (h.space && h.sel == -1)
				h.sel = p;
			VPUSH(hl, h);
		}
	if (n)
		add_inlines(&hl, n, c);
	if (qed)
		add_qed(&hl, p, bx.size);
	set_par(&hl, bx.fr, bx.size, indent ? 1.5 * bx.size : 0, true, qed);
}

/* a line of text set in one face, e.g. a heading; returns its width */
static double
text_line(const char *s, int face, double size, int e, Ops *ops, double x)
{
	HList hl = {0};
	Ctx c = {face, size, e, -1, true};

	add_text(&hl, s, c);
	for (size_t i = 0; i < hl.n; i++) {
		H *it = &hl.v[i];
		if (it->kind == H_BOX) {
			for (size_t k = 0; k < it->ops.n; k++) {
				Op o = it->ops.v[k];
				o.x += x;
				VPUSH(*ops, o);
			}
			x += it->w;
		} else if (it->kind == H_GLUE) {
			if (it->space)
				op_glyph(ops, it->sface, it->ssize, x, 0, it->sgid, it->w, " ", it->sel, -1, -1);
			x += it->w;
		}
	}
	return x;
}

static void
heading(Node *sec, Box_ bx, int level, bool numbered, const char *label)
{
	static const char *sect_role[] = {"", "section", "subsection", "subsubsection"};
	static const double sizes[] = {0, 14, 12, 11};
	double size = sizes[level] * BS / 11;
	Node *h = child(sec, "heading");
	int hel = el(bx.el, "tekx", fmt("%s-heading", sect_role[level]));
	const char *num = numbered ? heading_number(sec) : NULL;

	vspace(level == 1 ? 1.6 * BS : 1.2 * BS);
	HList hl = {0};
	Ctx c = {FACE_BF, size, hel, -1, true};
	if (num) {
		/* the number, then a real space one em wide */
		add_text(&hl, num, c);
		Font *f = face_font(FACE_BF);
		Ops o = {0};
		op_glyph(&o, FACE_BF, size, 0, 0, font_glyph(f, ' '), size, " ", hel, -1, -1);
		hbox(&hl, &o, size, 0, 0);
	}
	if (h)
		add_inlines(&hl, h, c);
	dest(bx.el);
	Outline ol = {level, bx.el, fmt("%s%s%s", num ? num : "", num ? " " : "", h ? plain_text(h) : ""), -1};
	VPUSH(outlines, ol);
	VPUSH(pending_outlines, (int)outlines.n - 1);
	size_t before = vl.n;
	Fr fr = bx.fr;
	set_par(&hl, fr, size, 0, false, false);
	for (size_t i = before; i < vl.n; i++)
		vl.v[i].keep = true;
	vspace(0.5 * BS);
	label_el(label, bx.el);
	after_heading = true;
}

static void
display(Node *d, Box_ bx)
{
	int div = el(bx.el, "tekx", "equation");
	VEC(Formula) fs = {0};
	VEC(char *) nums = {0};
	VEC(char *) labs = {0};
	double maxl = 0, maxr = 0;
	bool any_align = false;

	for (size_t i = 0; i < d->kids.n; i++) {
		Node *ln = d->kids.v[i];
		if (!streq(ln->kind, "line"))
			continue;
		char *label = field(ln, 0);
		Formula f = formula(ln, div, bx.size);
		if (!streq(label, "-")) {
			Label *l = find_label(label);
			VPUSH(nums, l ? l->text : "(?)");
			label_el(label, f.formula);
			prop(f.formula, "ident", label);
		} else {
			VPUSH(nums, (char *)NULL);
		}
		VPUSH(labs, label);
		VPUSH(fs, f);
		if (!isnan(f.align)) {
			any_align = true;
			maxl = MAX(maxl, f.align);
			maxr = MAX(maxr, f.w - f.align);
		}
	}
	if (fs.n == 0)
		return;
	vspace(0.75 * bx.size);
	if (vl.n && prev_was_text)
		vl.v[vl.n - 1].keep = true;	/* no page break between a display and its introduction */
	bool aligned = any_align && fs.n > 1;
	double left = (bx.fr.w - (maxl + maxr)) / 2;
	for (size_t i = 0; i < fs.n; i++) {
		Formula *f = &fs.v[i];
		double x;
		if (aligned && !isnan(f->align))
			x = left + maxl - f->align;
		else
			x = (bx.fr.w - f->w) / 2;
		if (x < 0)
			x = 0;
		Ops ops = {0};
		for (size_t k = 0; k < f->ops.n; k++) {
			Op o = f->ops.v[k];
			o.x += x;
			VPUSH(ops, o);
		}
		if (nums.v[i]) {
			int lbl = el(f->formula, "tekx", "equation-number");	/* a Lbl belongs to what it labels */
			Ops no = {0};
			double w = text_line(nums.v[i], FACE_REG, bx.size, lbl, &no, 0);
			for (size_t k = 0; k < no.n; k++) {
				Op o = no.v[k];
				o.x += bx.fr.w - w;
				VPUSH(ops, o);
			}
		}
		dest(f->formula);
		V *v = vline(MAX(f->h, bx.size * 0.72), MAX(f->d, bx.size * 0.22), lead_for(bx.size) * 1.15, bx.fr.x, &ops);
		if (i > 0)
			v->before = 0.25 * bx.size;
		if (i + 1 < fs.n)
			v->keep = true;
	}
	vspace(0.75 * bx.size);
}

static void
list_block(Node *l, Box_ bx, int depth)
{
	bool ordered = atoi(field(l, 0)) != 0;
	int num = atoi(field(l, 1));
	int L = el(bx.el, "pdf2", "L");
	attr(L, "List", "ListNumbering", 'n', ordered ? "Decimal" : depth % 2 ? "Circle" : "Disc");
	double indent = ordered ? 2.0 * bx.size : 1.5 * bx.size;

	vspace(0.3 * bx.size);
	for (size_t i = 0; i < l->kids.n; i++) {
		Node *it = l->kids.v[i];
		if (!streq(it->kind, "item"))
			continue;
		int li = el(L, "pdf2", "LI");
		int lbl = el(li, "pdf2", "Lbl");
		int body = el(li, "pdf2", "LBody");
		const char *label = ordered ? fmt("%d.", num++) : depth % 2 ? "◦" : "•";
		Ops lops = {0};
		double w = text_line(label, FACE_REG, bx.size, lbl, &lops, 0);
		Box_ in = bx;
		in.el = body;
		in.fr.x = bx.fr.x + indent;
		in.fr.w = bx.fr.w - indent;
		size_t first = vl.n;
		after_heading = true;	/* no paragraph indent in items */
		blocks(it, in);
		if (vl.n > first) {
			/* hang the label on the item's first line */
			V *v = &vl.v[first];
			Ops merged = {0};
			double lx = -0.5 * bx.size - w;
			for (size_t k = 0; k < lops.n; k++) {
				Op o = lops.v[k];
				o.x += lx;
				VPUSH(merged, o);
			}
			for (size_t k = 0; k < v->ops.n; k++)
				VPUSH(merged, v->ops.v[k]);
			v->ops = merged;
		}
	}
	vspace(0.3 * bx.size);
	after_heading = false;
}

static void
table_block(Node *t, Box_ bx)
{
	int T = el(bx.el, "pdf2", "Table");
	Node *al = child(t, "align");
	VEC(Node *) rows = {0};
	int cols = 0;

	for (size_t i = 0; i < t->kids.n; i++)
		if (streq(t->kids.v[i]->kind, "row")) {
			VPUSH(rows, t->kids.v[i]);
			cols = MAX(cols, (int)t->kids.v[i]->kids.n);
		}
	if (rows.n == 0)
		return;
	double pad = 0.5 * bx.size;
	double *cw = xcalloc((size_t)cols + 1, sizeof *cw);
	VEC(Ops) cells = {0};
	VEC(double) widths = {0};
	VEC(double) heights = {0}, depths = {0};
	for (size_t r = 0; r < rows.n; r++) {
		bool head = atoi(rows.v[r]->rest) != 0;
		int tr = el(T, "pdf2", "TR");
		double rh = bx.size * 0.72, rd = bx.size * 0.22;
		for (int c = 0; c < cols; c++) {
			Node *cell = c < (int)rows.v[r]->kids.n ? rows.v[r]->kids.v[c] : NULL;
			int ce = el(tr, "pdf2", head ? "TH" : "TD");
			if (head)
				attr(ce, "Table", "Scope", 'n', "Column");
			HList hl = {0};
			Ctx cx = {head ? FACE_BF : FACE_REG, bx.size, ce, -1, true};
			if (cell)
				add_inlines(&hl, cell, cx);
			Ops ops = {0};
			double x = 0;
			for (size_t i = 0; i < hl.n; i++) {
				H *it = &hl.v[i];
				if (it->kind == H_BOX) {
					for (size_t k = 0; k < it->ops.n; k++) {
						Op o = it->ops.v[k];
						o.x += x;
						VPUSH(ops, o);
					}
					x += it->w;
					rh = MAX(rh, it->h);
					rd = MAX(rd, it->d);
				} else if (it->kind == H_GLUE) {
					if (it->space)
						op_glyph(&ops, it->sface, it->ssize, x, 0, it->sgid, it->w, " ", it->sel, -1, it->slink);
					x += it->w;
				}
			}
			VPUSH(cells, ops);
			VPUSH(widths, x);
			cw[c] = MAX(cw[c], x);
		}
		VPUSH(heights, rh);
		VPUSH(depths, rd);
	}
	double tw = 0;
	for (int c = 0; c < cols; c++)
		tw += cw[c] + 2 * pad;
	double left = MAX(0, (bx.fr.w - tw) / 2);
	char aligns[64];
	memset(aligns, 'l', sizeof aligns);
	if (al) {
		char *a = xstrdup(al->rest);
		for (int c = 0; c < cols && c < 64; c++) {
			char *f = next_field(&a);
			if (*f)
				aligns[c] = f[0];
		}
	}
	vspace(0.6 * bx.size);
	double heavy = 0.08 * bx.size, light = 0.05 * bx.size;
	for (size_t r = 0; r < rows.n; r++) {
		bool head = atoi(rows.v[r]->rest) != 0;
		bool next_body = r + 1 < rows.n && !atoi(rows.v[r + 1]->rest);
		Ops ops = {0};
		double x = left;
		for (int c = 0; c < cols; c++) {
			Ops *o = &cells.v[r * (size_t)cols + (size_t)c];
			double w = widths.v[r * (size_t)cols + (size_t)c];
			double off = aligns[c] == 'r' ? cw[c] - w : aligns[c] == 'c' ? (cw[c] - w) / 2 : 0;
			for (size_t k = 0; k < o->n; k++) {
				Op q = o->v[k];
				q.x += x + pad + off;
				VPUSH(ops, q);
			}
			x += cw[c] + 2 * pad;
		}
		double h = heights.v[r], d = depths.v[r];
		/* rules: above the first row, below the header, below the last row */
		double gap = 0.3 * bx.size;
		if (r == 0)
			op_rule(&ops, left, -(h + gap + heavy), tw, heavy, -1);
		if (head && next_body)
			op_rule(&ops, left, d + gap, tw, light, -1);
		if (r + 1 == rows.n)
			op_rule(&ops, left, d + gap, tw, heavy, -1);
		V *v = vline(h + (r == 0 ? gap + heavy : 0), d + (r + 1 == rows.n || (head && next_body) ? gap + heavy : 0),
		    lead_for(bx.size) * 1.2, bx.fr.x, &ops);
		if (r + 1 < rows.n)
			v->keep = true;
	}
	vspace(0.6 * bx.size);
}

static void
code_block(Node *cnode, Box_ bx)
{
	int p = el(bx.el, "tekx", "code-block");
	int code = el(p, "pdf", "Code");
	double size = bx.size * 0.88;

	vspace(0.5 * bx.size);
	for (size_t i = 0; i < cnode->kids.n; i++) {
		Node *v = cnode->kids.v[i];
		Ops ops = {0};
		double w = text_line(v->rest, FACE_MONO, size, code, &ops, 1.5 * bx.size);
		if (w > bx.fr.w + 1)
			warn_at(cnode->line + (int)i + 1, 0, "code line is wider than the text by %.0fpt", w - bx.fr.w);
		/* a real newline between lines, for copy and paste */
		Font *f = face_font(FACE_MONO);
		int sg = font_glyph(f, ' ');
		if (sg)
			op_glyph(&ops, FACE_MONO, size, w, 0, sg, font_adv(f, sg) * size / f->upem, " ", code, -1, -1);
		V *vv = vline(size * 0.72, size * 0.22, size * 1.25, bx.fr.x, &ops);
		if (i + 1 < cnode->kids.n)
			vv->keep = true;
	}
	vspace(0.5 * bx.size);
}

/* the first child of a block that is not its heading */
static Node *
first_block(Node *t)
{
	for (size_t i = 0; i < t->kids.n; i++)
		if (!streq(t->kids.v[i]->kind, "heading"))
			return t->kids.v[i];
	return NULL;
}

/* a copy of t without its heading and without `skip` */
static Node
rest_of(Node *t, Node *skip)
{
	Node rest = *t;
	VEC(Node *) kids = {0};

	for (size_t i = 0; i < t->kids.n; i++)
		if (t->kids.v[i] != skip && !streq(t->kids.v[i]->kind, "heading"))
			VPUSH(kids, t->kids.v[i]);
	rest.kids.v = kids.v;
	rest.kids.n = kids.n;
	rest.kids.cap = kids.cap;
	return rest;
}

static void
theorem(Node *t, Box_ bx)
{
	char *kind = field(t, 0), *label = field(t, 1);
	Env *env = find_env(kind);
	int style = env ? env->style : 0;
	const char *title = env ? env->title : kind;
	int div = el(bx.el, "tekx", "theorem");
	Node *h = child(t, "heading"), *first = first_block(t);
	bool runin = first && streq(first->kind, "para");

	label_el(label, div);
	dest(div);
	vspace(0.6 * bx.size);
	/* the head, "Theorem 3 (Pythagoras).", runs into the first paragraph */
	int p = el(div, "pdf2", "P");
	int he = el(p, "tekx", "theorem-head");
	HList hl = {0};
	Ctx c = {style == 2 ? FACE_IT : FACE_BF, bx.size, he, -1, true};
	add_text(&hl, fmt("%s %s", title, t->num ? t->num : "?"), c);
	if (h) {
		Ctx n = c;
		n.face = FACE_REG;
		space_glue(&hl, n);
		add_text(&hl, "(", n);
		add_inlines(&hl, h, n);
		add_text(&hl, ")", n);
	}
	add_text(&hl, ".", c);
	space_glue(&hl, c);
	Box_ in = bx;
	in.el = div;
	in.face = style == 0 ? FACE_IT : FACE_REG;
	if (runin)
		add_inlines(&hl, first, ctx(in, p));
	set_par(&hl, in.fr, in.size, 0, runin, false);
	after_heading = !runin;
	Node rest = rest_of(t, runin ? first : NULL);
	blocks(&rest, in);
	vspace(0.6 * bx.size);
	after_heading = false;
}

static Node *
last_para(Node *n)
{
	for (size_t i = n->kids.n; i-- > 0;) {
		Node *k = n->kids.v[i];
		if (streq(k->kind, "para"))
			return k;
		if (!streq(k->kind, "heading"))
			return NULL;
	}
	return NULL;
}

static Node *qed_para;

static void
proof(Node *t, Box_ bx)
{
	int div = el(bx.el, "tekx", "proof");
	Box_ in = bx;
	Node *h = child(t, "heading"), *first = first_block(t);
	bool runin = first && streq(first->kind, "para");

	in.el = div;
	vspace(0.4 * bx.size);
	qed_para = last_para(t);
	int p = el(div, "pdf2", "P");
	int he = el(p, "tekx", "proof-head");
	HList hl = {0};
	Ctx c = {FACE_IT, bx.size, he, -1, true};
	if (h) {
		add_inlines(&hl, h, c);
		add_text(&hl, ".", c);
	} else {
		add_text(&hl, "Proof.", c);
	}
	space_glue(&hl, c);
	bool q = runin && first == qed_para;
	if (runin)
		add_inlines(&hl, first, ctx(in, p));
	if (q) {
		add_qed(&hl, p, bx.size);
		qed_para = NULL;
	}
	set_par(&hl, in.fr, in.size, 0, runin, q);
	after_heading = !runin;
	Node rest = rest_of(t, runin ? first : NULL);
	blocks(&rest, in);
	if (qed_para != NULL || last_para(t) == NULL) {
		/* the proof ends with a display or a list: the mark gets a line of its own */
		int pe = el(div, "pdf2", "P");
		int qe = el(pe, "tekx", "qed");
		Font *f = face_font(FACE_MATH);
		int g = font_glyph(f, 0x220e);
		double w = font_adv(f, g) * bx.size / f->upem;
		Ops o = {0};
		op_glyph(&o, FACE_MATH, bx.size, bx.fr.w - w, 0, g, w, "∎", qe, -1, -1);
		V *v = vline(bx.size * 0.7, 0, lead_for(bx.size), bx.fr.x, &o);
		v->before = -lead_for(bx.size) * 0.5;
		qed_para = NULL;
	}
	vspace(0.4 * bx.size);
	after_heading = false;
}

/* a length for a figure: 60%, 8cm, 80mm, 3in, 200pt */
static double
length(const char *s, double whole)
{
	char *end;
	double v = strtod(s, &end);

	if (streq(end, "%"))
		return v / 100 * whole;
	if (streq(end, "cm"))
		return v * 72 / 2.54;
	if (streq(end, "mm"))
		return v * 72 / 25.4;
	if (streq(end, "in"))
		return v * 72;
	if (streq(end, "pt") || *end == 0)
		return v;
	return -1;
}

/* resolve a file name against the directory of the source file */
static char *
beside_source(const char *path)
{
	const char *slash = diag_file ? strrchr(diag_file, '/') : NULL;

	if (path[0] == '/' || slash == NULL)
		return xstrdup(path);
	return fmt("%.*s/%s", (int)(slash - diag_file), diag_file, path);
}

static int fig_num;

static void
figure(Node *f, Box_ bx)
{
	char *path = beside_source(pct_unescape(field(f, 0))), *width = field(f, 1), *label = field(f, 2);
	Node *alt = child(f, "alt");
	char *err = NULL;
	Image *im = image_load(path, &err);

	fig_num++;
	if (im == NULL) {
		error_at(f->line, 0, "%s", err);
		return;
	}
	if (alt == NULL || *alt->rest == 0)
		error_at(f->line, 0, "a figure needs a text alternative for readers who cannot see it: "
		    "add a line  alt \"what the figure shows\"");
	double w = MIN(bx.fr.w, im->w * 72.0 / 96);
	if (!streq(width, "-")) {
		double v = length(width, bx.fr.w);
		if (v <= 0)
			error_at(f->line, 0, "unknown width '%s' (use 60%%, 8cm, 80mm, 3in or 200pt)", width);
		else
			w = MIN(v, bx.fr.w);
	}
	double h = w * im->h / im->w;
	int wrap = el(bx.el, "pdf2", "Aside");	/* Caption is not allowed in a Div */
	int fe = el(wrap, "pdf2", "Figure");
	prop(fe, "alt", alt ? alt->rest : "");
	label_el(label, wrap);
	dest(wrap);
	vspace(0.8 * bx.size);
	size_t first_line = vl.n;
	Ops ops = {0};
	Op o;
	memset(&o, 0, sizeof o);
	o.kind = O_IMAGE;
	o.x = (bx.fr.w - w) / 2;
	o.y = -h;
	o.w = w;
	o.h = h;
	o.el = fe;
	o.span = o.link = -1;
	o.label = path;
	VPUSH(ops, o);
	V *v = vline(h, 0, h, bx.fr.x, &ops);
	v->keep = true;
	/* the caption: "Figure 2. Text", centred when it fits on one line */
	int cap = el(wrap, "pdf2", "Caption");
	int p = el(cap, "pdf2", "P");
	double size = bx.size * 0.92;
	HList hl = {0};
	Ctx c = {FACE_BF, size, el(p, "pdf2", "Strong"), -1, true};
	add_text(&hl, fmt("Figure %s.", f->num ? f->num : "?"), c);
	Ctx body = {FACE_REG, size, p, -1, false};
	space_glue(&hl, body);
	for (size_t i = 0; i < f->kids.n; i++)
		if (streq(f->kids.v[i]->kind, "para"))
			add_inlines(&hl, f->kids.v[i], body);
	vspace(0.5 * bx.size);
	Fr fr = {bx.fr.x + 1.5 * bx.size, bx.fr.w - 3 * bx.size};
	size_t first = vl.n;
	set_par(&hl, fr, size, 0, true, false);
	if (vl.n == first + 1) {
		V *line = &vl.v[first];
		double right = 0;
		for (size_t k = 0; k < line->ops.n; k++)
			right = MAX(right, line->ops.v[k].x + line->ops.v[k].adv);
		for (size_t k = 0; k < line->ops.n; k++)
			line->ops.v[k].x += (fr.w - right) / 2;
	}
	/* the image and its caption float together */
	for (size_t k = first_line; k < vl.n; k++)
		vl.v[k].float_id = fig_num;
	vspace(0.8 * bx.size);
}

static void
quote_block(Node *q, Box_ bx)
{
	int bq = el(bx.el, "pdf", "BlockQuote");
	Box_ in = bx;

	in.el = bq;
	in.fr.x += 2 * bx.size;
	in.fr.w -= 4 * bx.size;
	vspace(0.5 * bx.size);
	after_heading = true;
	blocks(q, in);
	vspace(0.5 * bx.size);
	after_heading = false;
}

static void
abstract_block(Node *a, Box_ bx)
{
	int sect = el(bx.el, "tekx", "abstract");
	int h = el(sect, "tekx", "abstract-heading");
	double size = BS * 10 / 11;
	Ops ops = {0};
	double w = text_line("Abstract", FACE_BF, size, h, &ops, 0);

	for (size_t i = 0; i < ops.n; i++)
		ops.v[i].x += (bx.fr.w - w) / 2;
	vspace(1.2 * BS);
	vline(size * 0.72, size * 0.22, lead_for(size), bx.fr.x, &ops)->keep = true;
	vspace(0.3 * BS);
	Box_ in = bx;
	in.el = sect;
	in.size = size;
	in.fr.x += 2.5 * BS;
	in.fr.w -= 5 * BS;
	after_heading = true;
	blocks(a, in);
	vspace(0.8 * BS);
	after_heading = true;
}

static void
let_block(Node *n, Box_ bx, bool indent)
{
	int p = el(bx.el, "pdf2", "P");
	HList hl = {0};
	Ctx c = ctx(bx, p);
	Node *prose = child(n, "prose");

	add_text(&hl, "Let ", c);
	add_math(&hl, n, c);
	if (prose) {
		space_glue(&hl, c);
		add_inlines(&hl, prose, c);
	}
	char *pt = prose ? plain_text(prose) : "";
	size_t len = strlen(pt);
	if (len == 0 || !strchr(".!?", pt[len - 1]))
		add_text(&hl, ".", c);
	set_par(&hl, bx.fr, bx.size, indent ? 1.5 * bx.size : 0, true, false);
}

static void
references_block(Node *r, Box_ bx)
{
	int sect = el(bx.el, "tekx", "bibliography");
	int h = el(sect, "tekx", "bibliography-heading");
	Ops ops = {0};
	double size = 14 * BS / 11;

	vspace(1.6 * BS);
	text_line("References", FACE_BF, size, h, &ops, 0);
	dest(sect);
	Outline ol = {1, sect, "References", -1};
	VPUSH(outlines, ol);
	VPUSH(pending_outlines, (int)outlines.n - 1);
	vline(size * 0.72, size * 0.22, lead_for(size), bx.fr.x, &ops)->keep = true;
	vspace(0.5 * BS);
	int L = el(sect, "pdf2", "L");
	attr(L, "List", "ListNumbering", 'n', "Decimal");
	/* widest label */
	double lw = 0;
	for (size_t i = 0; i < r->kids.n; i++) {
		Label *l = find_label(cite_label(r->kids.v[i]->rest));
		Ops tmp = {0};
		lw = MAX(lw, text_line(l ? l->text : "[?]", FACE_REG, bx.size, -1, &tmp, 0));
	}
	for (size_t i = 0; i < r->kids.n; i++) {
		Node *e = r->kids.v[i];
		Label *l = find_label(cite_label(e->rest));
		int li = el(L, "pdf2", "LI");
		int lbl = el(li, "pdf2", "Lbl");
		int body = el(li, "pdf2", "LBody");
		label_el(cite_label(e->rest), li);
		dest(li);
		Ops lops = {0};
		double w = text_line(l ? l->text : "[?]", FACE_REG, bx.size, lbl, &lops, 0);
		Box_ in = bx;
		in.el = body;
		in.fr.x += lw + 0.6 * bx.size;
		in.fr.w -= lw + 0.6 * bx.size;
		size_t first = vl.n;
		int p = el(body, "pdf2", "P");
		HList hl = {0};
		add_inlines(&hl, e, ctx(in, p));
		set_par(&hl, in.fr, in.size, 0, true, false);
		if (vl.n > first) {
			V *v = &vl.v[first];
			Ops merged = {0};
			for (size_t k = 0; k < lops.n; k++) {
				Op o = lops.v[k];
				o.x += -(lw + 0.6 * bx.size) + (lw - w);
				VPUSH(merged, o);
			}
			for (size_t k = 0; k < v->ops.n; k++)
				VPUSH(merged, v->ops.v[k]);
			v->ops = merged;
		}
		vspace(0.25 * BS);
	}
}

static Node *doc_root;
static VEC(Node *) toc_sections;

static void
collect_sections(Node *n)
{
	for (size_t i = 0; i < n->kids.n; i++) {
		Node *k = n->kids.v[i];
		if (streq(k->kind, "section")) {
			VPUSH(toc_sections, k);
			collect_sections(k);
		}
	}
}

static void
contents_block(Box_ bx)
{
	int sect = el(bx.el, "tekx", "contents");
	int h = el(sect, "tekx", "contents-heading");
	double size = 14 * BS / 11;
	Ops ops = {0};

	vspace(1.6 * BS);
	text_line("Contents", FACE_BF, size, h, &ops, 0);
	vline(size * 0.72, size * 0.22, lead_for(size), bx.fr.x, &ops)->keep = true;
	vspace(0.4 * BS);
	int toc = el(sect, "pdf", "TOC");
	for (size_t i = 0; i < toc_sections.n; i++) {
		Node *s = toc_sections.v[i];
		int level = atoi(field(s, 0));
		bool numbered = atoi(field(s, 1)) != 0;
		Node *hd = child(s, "heading");
		const char *num = numbered ? heading_number(s) : NULL;
		int ti = el(toc, "pdf", "TOCI");
		int p = el(ti, "pdf2", "P");
		char *target = (char *)section_key(s);
		char *text = fmt("%s%s%s", num ? num : "", num ? " " : "", hd ? plain_text(hd) : "");
		int link = new_link(p, NULL, target, text);
		int le = links.v[link].el;
		Ops o = {0};
		double indent = (level - 1) * 1.5 * BS;
		int face = level == 1 ? FACE_BF : FACE_REG;
		Ctx c = {face, BS, le, link, true};
		HList hl = {0};
		if (num) {
			add_text(&hl, num, c);
			space_glue(&hl, c);
			hl.v[hl.n - 1].w = MAX(hl.v[hl.n - 1].w, 0.0);
		}
		if (hd)
			add_inlines(&hl, hd, c);
		double x = indent;
		for (size_t k = 0; k < hl.n; k++) {
			H *it = &hl.v[k];
			if (it->kind == H_BOX) {
				for (size_t q = 0; q < it->ops.n; q++) {
					Op op = it->ops.v[q];
					op.x += x;
					VPUSH(o, op);
				}
				x += it->w;
			} else if (it->kind == H_GLUE) {
				double w = it->w * (num && k < 2 ? 2 : 1);
				if (it->space)
					op_glyph(&o, it->sface, it->ssize, x, 0, it->sgid, w, " ", it->sel, -1, link);
				x += w;
			}
		}
		/* the page number, filled in after pagination */
		Op pr;
		memset(&pr, 0, sizeof pr);
		pr.kind = O_PAGEREF;
		pr.face = face;
		pr.size = BS;
		pr.x = bx.fr.w;
		pr.el = le;
		pr.span = -1;
		pr.link = link;
		pr.label = target;
		pr.adv = x + 0.5 * BS;	/* leaders start here */
		VPUSH(o, pr);
		vspace(level == 1 ? 0.35 * BS : 0);
		vline(BS * 0.72, BS * 0.22, lead_for(BS), bx.fr.x, &o);
	}
	vspace(0.8 * BS);
}

static void
blocks(Node *n, Box_ bx)
{
	for (size_t i = 0; i < n->kids.n; i++) {
		Node *k = n->kids.v[i];
		const char *kd = k->kind;
		bool text_now = streq(kd, "para") || streq(kd, "let");
		if (k->line > 0)
			cur_line = k->line;
		if (!text_now && !streq(kd, "display"))
			prev_was_text = false;
		if (streq(kd, "para")) {
			bool cont = atoi(k->rest) != 0;
			bool indent = !after_heading && !cont;
			bool q = k == qed_para;
			if (q) {
				paragraph(k, bx, NULL, indent, true);
				qed_para = NULL;
			} else {
				paragraph(k, bx, NULL, indent, false);
			}
			after_heading = false;
		} else if (streq(kd, "section")) {
			int level = atoi(field(k, 0));
			bool numbered = atoi(field(k, 1)) != 0;
			char *label = field(k, 2);
			static const char *role[] = {"", "section", "subsection", "subsubsection"};
			Box_ in = bx;
			in.el = el(bx.el, "tekx", role[level < 1 || level > 3 ? 1 : level]);
			label = (char *)section_key(k);
			heading(k, in, level, numbered, label);
			blocks(k, in);
		} else if (streq(kd, "display")) {
			display(k, bx);
			after_heading = false;
		} else if (streq(kd, "list")) {
			list_block(k, bx, 0);
		} else if (streq(kd, "table")) {
			table_block(k, bx);
			after_heading = true;
		} else if (streq(kd, "code")) {
			code_block(k, bx);
			after_heading = true;
		} else if (streq(kd, "theorem")) {
			theorem(k, bx);
		} else if (streq(kd, "proof")) {
			proof(k, bx);
		} else if (streq(kd, "quote")) {
			quote_block(k, bx);
		} else if (streq(kd, "abstract")) {
			abstract_block(k, bx);
			classification(bx);
		} else if (streq(kd, "let")) {
			let_block(k, bx, !after_heading);
			after_heading = false;
		} else if (streq(kd, "figure")) {
			figure(k, bx);
			after_heading = true;
		} else if (streq(kd, "refs")) {
			references_block(k, bx);
		} else if (streq(kd, "contents")) {
			contents_block(bx);
			after_heading = true;
		} else if (streq(kd, "custom")) {
			error_at(k->line, 0, "the %s block belongs to package %s, and its filter did not turn it "
			    "into document content", field(k, 0), field(k, 1));
		} else if (streq(kd, "newpage")) {
			V *v = vline(0, 0, 0, 0, NULL);
			v->newpage = true;
			after_heading = true;
		}
		if (text_now)
			prev_was_text = true;
	}
}

/* ---- metadata and title ---- */

static void
read_meta(Node *n)
{
	for (size_t i = 0; i < n->kids.n; i++) {
		Node *k = n->kids.v[i];
		if (!streq(k->kind, "meta"))
			continue;
		char *key = field(k, 0), *val = field_rest(k, 1);
		if (streq(key, "title"))
			meta_title = val;
		else if (streq(key, "subtitle"))
			meta_subtitle = val;
		else if (streq(key, "author")) {
			char *a = xstrdup(val), *s;
			while (k->kids.n == 0 && (s = strstr(a, " and ")) != NULL) {
				*s = 0;
				VPUSH(authors, trim(a));
				VPUSH(author_nodes, (Node *)NULL);
				a = s + 5;
			}
			VPUSH(authors, trim(a));
			VPUSH(author_nodes, k);
		} else if (streq(key, "msc"))
			meta_msc = val;
		else if (streq(key, "date"))
			meta_date = streq(val, "today") ? format_date(doc_time()) : val;
		else if (streq(key, "language"))
			language = val;
		else if (streq(key, "keywords"))
			meta_keywords = val;
		else if (streq(key, "subject"))
			meta_subject = val;
		else if (streq(key, "paper")) {
			if (streq(val, "letter"))
				PW = 612, PH = 792;
			else if (streq(val, "a4"))
				PW = 595.276, PH = 841.89;
			else if (streq(val, "a5"))
				PW = 419.528, PH = 595.276;
			else
				error_at(k->line, 7, "unknown paper '%s' (expected a4, a5 or letter)", val);
		} else if (streq(key, "size")) {
			double s = atof(val);
			if (s < 6 || s > 24)
				error_at(k->line, 6, "type size %s is out of range (6 to 24 points)", val);
			else
				BS = s;
		} else if (streq(key, "font")) {
			char *v2 = xstrdup(val);
			char *role = next_field(&v2);
			logical_font_override(role, trim(v2));
		}
	}
}

/* centre every line set since `first` in the frame */
static void
centre_lines(size_t first, Fr fr)
{
	for (size_t i = first; i < vl.n; i++) {
		V *line = &vl.v[i];
		double right = 0;
		for (size_t k = 0; k < line->ops.n; k++)
			right = MAX(right, line->ops.v[k].x + line->ops.v[k].adv);
		for (size_t k = 0; k < line->ops.n; k++)
			line->ops.v[k].x += MAX(0, (fr.w - right) / 2);
	}
}

static void
title_line(int tb, const char *text, const char *role, int face, double size, double before, Box_ bx)
{
	int e = el(tb, "tekx", role);
	Ops ops = {0};
	double w = text_line(text, face, size, e, &ops, 0);

	for (size_t k = 0; k < ops.n; k++)
		ops.v[k].x += MAX(0, (bx.fr.w - w) / 2);
	vspace(before);
	vline(size * 0.72, size * 0.22, lead_for(size), bx.fr.x, &ops)->keep = true;
}

/* a centred paragraph in the title block, wrapped if it is long */
static void
title_par(HList *hl, double size, Box_ bx)
{
	Fr fr = {bx.fr.x + 2 * BS, bx.fr.w - 4 * BS};
	size_t first = vl.n;

	set_par(hl, fr, size, 0, false, false);
	centre_lines(first, fr);
	for (size_t i = first; i < vl.n; i++)
		vl.v[i].keep = true;
}

/* under an author's name: affiliations, then email, ORCID and web page as links */
static void
author_details(int tb, Node *a, Box_ bx)
{
	double size = BS * 9 / 11;
	HList contact = {0};
	int ce = -1;

	for (size_t i = 0; i < a->kids.n; i++) {
		Node *k = a->kids.v[i];
		if (streq(k->kind, "affiliation")) {
			HList hl = {0};
			Ctx c = {FACE_IT, size, el(tb, "tekx", "affiliation"), -1, true};
			add_text(&hl, k->rest, c);
			vspace(0.1 * BS);
			title_par(&hl, size, bx);
			continue;
		}
		const char *url = streq(k->kind, "email") ? fmt("mailto:%s", k->rest) :
		    streq(k->kind, "orcid") ? fmt("https://orcid.org/%s", k->rest) : k->rest;
		const char *shown = streq(k->kind, "orcid") ? fmt("ORCID %s", k->rest) : k->rest;
		const char *spoken = streq(k->kind, "email") ? fmt("Email %s", k->rest) : shown;
		if (ce < 0)
			ce = el(tb, "tekx", "contact");
		Ctx c = {FACE_REG, size, ce, -1, true};
		if (contact.n) {
			space_glue(&contact, c);
			add_word(&contact, "·", c);
			space_glue(&contact, c);
		}
		Ctx e = c;
		e.link = new_link(ce, url, NULL, spoken);
		e.el = links.v[e.link].el;
		add_word(&contact, shown, e);
	}
	if (contact.n)
		title_par(&contact, size, bx);
}

static void
title_block(Box_ bx)
{
	if (!meta_title)
		return;
	int tb = el(bx.el, "tekx", "title-block");

	title_line(tb, meta_title, "title", FACE_BF, 17.28 * BS / 11, 0, bx);
	if (meta_subtitle)
		title_line(tb, meta_subtitle, "subtitle", FACE_IT, 12 * BS / 11, 0.5 * BS, bx);
	for (size_t i = 0; i < authors.n; i++) {
		Node *a = author_nodes.v[i];
		bool details = a && a->kids.n;
		bool prev_details = i > 0 && author_nodes.v[i - 1] && author_nodes.v[i - 1]->kids.n;
		title_line(tb, authors.v[i], "author", FACE_REG, 12 * BS / 11,
		    i == 0 ? 1.2 * BS : details || prev_details ? 0.8 * BS : 0.2 * BS, bx);
		if (details)
			author_details(tb, a, bx);
	}
	if (meta_date)
		title_line(tb, meta_date, "date", FACE_REG, BS, 0.6 * BS, bx);
	vl.v[vl.n - 1].keep = false;
	vspace(1.5 * BS);
	after_heading = true;
}

/*
 * The subject classification and key words, as the AMS sets them: after
 * the abstract (or the title, if there is no abstract), small, with an
 * italic label.
 */
static bool classification_done;

static void
classification(Box_ bx)
{
	double size = BS * 9 / 11;
	const char *what[2][3] = {
		{meta_msc, "msc", "2020 Mathematics Subject Classification."},
		{meta_keywords, "keywords", "Key words and phrases."},
	};

	if (classification_done)
		return;
	classification_done = true;
	for (int i = 0; i < 2; i++) {
		if (what[i][0] == NULL)
			continue;
		int p = el(bx.el, "tekx", what[i][1]);
		HList hl = {0};
		Ctx lab = {FACE_IT, size, el(p, "pdf2", "Em"), -1, true};
		Ctx c = {FACE_REG, size, p, -1, true};
		add_text(&hl, what[i][2], lab);
		space_glue(&hl, c);
		add_text(&hl, what[i][0], c);
		Fr fr = {bx.fr.x + 2.5 * BS, bx.fr.w - 5 * BS};
		vspace(0.15 * BS);
		set_par(&hl, fr, size, 0, false, false);
	}
	if (meta_msc || meta_keywords)
		vspace(0.8 * BS);
}

/* ---- pages ---- */

static double
notes_height(V *v)
{
	double h = 0;

	for (size_t k = 0; k < v->notes.n; k++)
		h += notes.v[v->notes.v[k] - 1].h + NOTE_GAP;
	return h;
}

typedef struct {
	VEC(int) lines;	/* indices into vl, in the order they are set */
} Page;

static VEC(Page) pages;

/* The page builder.  Lines are set top to bottom; a line that does not fit
 * starts a new page, taking with it the lines that must stay with it.  A
 * figure that does not fit floats to the top of the next page while the
 * text goes on, as in TeX. */
typedef struct {
	Page cur;
	double prev_base, prev_d, notes_h;
} Builder;

static bool
empty_page(Builder *pb)
{
	return pb->cur.lines.n == 0;
}

static double
baseline_for(Builder *pb, V *v)
{
	if (empty_page(pb))
		return TOP + v->h;
	return pb->prev_base + MAX(v->lead, pb->prev_d + v->h + 0.1 * BS) + v->before;
}

static void
place(Builder *pb, size_t i)
{
	V *v = &vl.v[i];
	double need = notes_height(v);
	double sep = need > 0 && pb->notes_h == 0 ? NOTE_RULE_SPACE : 0;

	v->y = baseline_for(pb, v);
	v->page = (int)pages.n;
	pb->notes_h += need + sep;
	pb->prev_base = v->y;
	pb->prev_d = v->d;
	VPUSH(pb->cur.lines, (int)i);
}

static bool
fits(Builder *pb, size_t i)
{
	V *v = &vl.v[i];
	double need = notes_height(v);
	double sep = need > 0 && pb->notes_h == 0 ? NOTE_RULE_SPACE : 0;

	return baseline_for(pb, v) + v->d <= BOT - pb->notes_h - need - sep + 0.5;
}

static void
new_page(Builder *pb)
{
	VPUSH(pages, pb->cur);
	memset(&pb->cur, 0, sizeof pb->cur);
	pb->notes_h = 0;
}

/* lines [i, end) of a float, or end == i when line i starts none */
static size_t
float_end(size_t i)
{
	size_t e = i;

	if (vl.v[i].float_id == 0)
		return i;
	while (e < vl.n && vl.v[e].float_id == vl.v[i].float_id)
		e++;
	return e;
}

/* would the whole float fit on the current page? */
static bool
float_fits(Builder *pb, size_t i, size_t e)
{
	Builder trial = *pb;

	memset(&trial.cur, 0, sizeof trial.cur);
	for (size_t k = 0; k < pb->cur.lines.n; k++)
		VPUSH(trial.cur.lines, pb->cur.lines.v[k]);
	for (size_t k = i; k < e; k++) {
		if (!fits(&trial, k))
			return false;
		V saved = vl.v[k];
		place(&trial, k);
		vl.v[k] = saved;
	}
	return true;
}

static void
paginate(void)
{
	Builder pb;
	VEC(size_t) pending = {0};	/* floats waiting for the next page */
	size_t i = 0;

	memset(&pb, 0, sizeof pb);
	while (i < vl.n || pending.n) {
		/* a fresh page takes the floats that waited for it */
		if (empty_page(&pb) && pending.n) {
			size_t f = pending.v[0], e = float_end(f);
			for (size_t k = f; k < e; k++)
				place(&pb, k);
			memmove(pending.v, pending.v + 1, (pending.n - 1) * sizeof *pending.v);
			pending.n--;
			while (pending.n && float_fits(&pb, pending.v[0], float_end(pending.v[0]))) {
				f = pending.v[0];
				e = float_end(f);
				for (size_t k = f; k < e; k++)
					place(&pb, k);
				memmove(pending.v, pending.v + 1, (pending.n - 1) * sizeof *pending.v);
				pending.n--;
			}
			continue;
		}
		if (i >= vl.n) {
			new_page(&pb);
			continue;
		}
		V *v = &vl.v[i];
		if (v->newpage) {
			if (!empty_page(&pb))
				new_page(&pb);
			i++;
			continue;
		}
		size_t e = float_end(i);
		if (e > i) {
			if (pending.n == 0 && (empty_page(&pb) || float_fits(&pb, i, e))) {
				for (size_t k = i; k < e; k++)
					place(&pb, k);
			} else {
				VPUSH(pending, i);
			}
			i = e;
			continue;
		}
		if (!empty_page(&pb) && !fits(&pb, i)) {
			/* back up over lines that must stay with their successors */
			size_t n = pb.cur.lines.n, b = n;
			while (b > 1 && vl.v[pb.cur.lines.v[b - 1]].keep && vl.v[pb.cur.lines.v[b - 1]].float_id == 0)
				b--;
			if (b == 1 && vl.v[pb.cur.lines.v[0]].keep)
				b = n;	/* everything is glued: break anyway */
			size_t resume = b < n ? (size_t)pb.cur.lines.v[b] : i;
			pb.cur.lines.n = b;
			new_page(&pb);
			i = resume;
			continue;
		}
		place(&pb, i);
		i++;
	}
	if (!empty_page(&pb) || pages.n == 0)
		new_page(&pb);
}

/* ---- output ---- */

static FILE *out;
static int el_page_cache_n;
static int *el_page;
static double *el_y;

/* glyph runs, merged while font, size, baseline and context agree */
typedef struct {
	bool open, invisible;
	int face;
	double size, x, y, next_x;
	Buf b;
} Run;

static Run run;
static int open_span = -1, open_mc = -1000;

static void
flush_run(void)
{
	if (!run.open)
		return;
	fprintf(out, "%c %d ", run.invisible ? 'T' : 't', pdl_font(run.face));
	Buf h = {0};
	bnum(&h, run.size);
	bputc(&h, ' ');
	bnum(&h, run.x);
	bputc(&h, ' ');
	bnum(&h, run.y);
	fprintf(out, "%s%s\n", bstr(&h), bstr(&run.b));
	breset(&run.b);
	run.open = false;
}

static void
close_mc(void)
{
	flush_run();
	if (open_mc != -1000) {
		fputs("E\n", out);
		open_mc = -1000;
	}
}

static void
close_span(void)
{
	close_mc();
	if (open_span >= 0) {
		fputs("E\n", out);
		open_span = -1;
	}
}

static void
context(int e, int sp)
{
	int want_span = sp >= 0 && spans.v[sp].active ? sp : -1;

	if (want_span != open_span) {
		close_span();
		if (want_span >= 0) {
			Buf t = {0};
			bputs(&t, spans.v[want_span].text);
			fprintf(out, "s %s\n", bstr(&t));
			open_span = want_span;
		}
	}
	if (e != open_mc) {
		close_mc();
		if (e >= 0)
			fprintf(out, "b %d\n", e);
		else
			fprintf(out, "B %s\n", e == ART_PAGE ? "pagination" : "layout");
		open_mc = e;
	}
}

static void
glyph_out(const Op *o, double x, double y)
{
	bool inv = o->kind == O_INVIS;

	if (run.open && (run.face != o->face || fabs(run.size - o->size) > 0.001 || fabs(run.y - y) > 0.001 ||
	    fabs(run.next_x - x) > 0.01 || run.invisible != inv))
		flush_run();
	if (!run.open) {
		run.open = true;
		run.face = o->face;
		run.size = o->size;
		run.x = x;
		run.y = y;
		run.invisible = inv;
		pdl_font(o->face);
	}
	bprintf(&run.b, " %d:", o->gid);
	bnum(&run.b, o->adv);
	bputc(&run.b, ':');
	pct_escape(&run.b, o->text);
	run.next_x = x + o->adv;
}

typedef struct {
	int link;
	double x0, x1, y0, y1;
} LinkRect;

typedef VEC(LinkRect) LinkRects;

static void
emit_link_rects(LinkRects *lr)
{
	for (size_t i = 0; i < lr->n; i++) {
		LinkRect *r = &lr->v[i];
		Link *l = &links.v[r->link];
		Buf b = {0};
		bputs(&b, "l ");
		bnum(&b, r->x0);
		bputc(&b, ' ');
		bnum(&b, r->y0);
		bputc(&b, ' ');
		bnum(&b, r->x1 - r->x0);
		bputc(&b, ' ');
		bnum(&b, r->y1 - r->y0);
		bprintf(&b, " %d ", l->el);
		if (l->url) {
			bputs(&b, "u ");
			pct_escape(&b, l->url);
		} else {
			Label *lab = find_label(l->label);
			int t = lab && lab->el >= 0 ? lab->el : 0;
			int pg = t < el_page_cache_n && el_page[t] >= 0 ? el_page[t] : 0;
			double y = t < el_page_cache_n ? el_y[t] : 0;
			bprintf(&b, "d %d %d ", t, pg);
			bnum(&b, y);
		}
		bprintf(&b, " %s", l->contents);
		fprintf(out, "%s\n", bstr(&b));
	}
	lr->n = 0;
}

static void
emit_page_number(int pi)
{
	Ops ops = {0};
	char *s = fmt("%d", pi + 1);
	double w = text_line(s, FACE_REG, BS * 10 / 11, ART_PAGE, &ops, 0);
	double y = PH - (PH - BOT) / 2 + BS * 0.3;
	for (size_t i = 0; i < ops.n; i++) {
		Op *o = &ops.v[i];
		context(ART_PAGE, -1);
		glyph_out(o, LM + (TW - w) / 2 + o->x, y);
	}
	close_span();
}

static void
emit_pageref(Op *o, double x0, double y, LinkRects *lr)
{
	Label *lab = find_label(o->label);
	int t = lab && lab->el >= 0 ? lab->el : -1;
	int pg = t >= 0 && t < el_page_cache_n ? el_page[t] : -1;
	char *s = pg >= 0 ? fmt("%d", pg + 1) : "?";
	Ops ops = {0};
	double w = text_line(s, o->face, o->size, o->el, &ops, 0);
	double right = x0 + o->x;
	/* dot leaders, as layout artifacts */
	Font *f = face_font(FACE_REG);
	int dot = font_glyph(f, '.');
	double step = 0.45 * o->size, start = x0 + o->adv;
	double first = ceil(start / step) * step;
	for (double x = first; x + step < right - w - 0.3 * o->size; x += step) {
		Op d;
		memset(&d, 0, sizeof d);
		d.kind = O_GLYPH;
		d.face = FACE_REG;
		d.size = o->size;
		d.gid = dot;
		d.adv = font_adv(f, dot) * o->size / f->upem;
		d.text = ".";
		context(ART_LAYOUT, -1);
		glyph_out(&d, x, y);
	}
	for (size_t i = 0; i < ops.n; i++) {
		context(ops.v[i].el, -1);
		glyph_out(&ops.v[i], right - w + ops.v[i].x, y);
	}
	if (o->link >= 0) {
		for (size_t i = 0; i < lr->n; i++)
			if (lr->v[i].link == o->link) {
				lr->v[i].x1 = MAX(lr->v[i].x1, right);
				return;
			}
		LinkRect r = {o->link, right - w, right, y - o->size * 0.8, y + o->size * 0.25};
		VPUSH(*lr, r);
	}
}

/* one drawing operation of a line whose origin is (x0, y) */
static void
emit_op(Op *o, double x0, double y, LinkRects *lr)
{
	if (o->kind == O_PAGEREF) {
		emit_pageref(o, x0, y, lr);
		return;
	}
	context(o->el, o->span);
	if (o->kind == O_IMAGE) {
		flush_run();
		Buf b = {0};
		bputs(&b, "I ");
		bnum(&b, x0 + o->x);
		bputc(&b, ' ');
		bnum(&b, y + o->y);
		bputc(&b, ' ');
		bnum(&b, o->w);
		bputc(&b, ' ');
		bnum(&b, o->h);
		bputc(&b, ' ');
		pct_escape(&b, o->label);
		fprintf(out, "%s\n", bstr(&b));
		return;
	}
	if (o->kind == O_RULE) {
		flush_run();
		Buf b = {0};
		bputs(&b, "r ");
		bnum(&b, x0 + o->x);
		bputc(&b, ' ');
		bnum(&b, y + o->y);
		bputc(&b, ' ');
		bnum(&b, o->w);
		bputc(&b, ' ');
		bnum(&b, o->h);
		fprintf(out, "%s\n", bstr(&b));
		return;
	}
	glyph_out(o, x0 + o->x, y + o->y);
	if (o->link >= 0 && o->kind == O_GLYPH) {
		double gx0 = x0 + o->x, gx1 = gx0 + o->adv;
		double gy0 = y - o->size * 0.8, gy1 = y + o->size * 0.25;
		LinkRect *last = NULL;
		for (size_t q = lr->n; q-- > 0;)
			if (lr->v[q].link == o->link) {
				last = &lr->v[q];
				break;
			}
		if (last && fabs(last->y1 - gy1) < 0.5 * o->size && gx0 >= last->x0 - 1) {
			last->x1 = MAX(last->x1, gx1);
			last->y0 = MIN(last->y0, gy0);
		} else {
			LinkRect r = {o->link, gx0, gx1, gy0, gy1};
			VPUSH(*lr, r);
		}
	}
}

/* the footnotes of a page, above its foot, under a short rule */
static void
emit_notes(size_t pi, LinkRects *lr)
{
	double total = 0;
	VEC(int) here = {0};

	for (size_t li = 0; li < pages.v[pi].lines.n; li++)
		for (size_t k = 0, i = (size_t)pages.v[pi].lines.v[li]; k < vl.v[i].notes.n; k++) {
			VPUSH(here, vl.v[i].notes.v[k]);
			total += notes.v[vl.v[i].notes.v[k] - 1].h + NOTE_GAP;
		}
	if (here.n == 0)
		return;
	double y = BOT - total;
	context(ART_LAYOUT, -1);
	flush_run();
	Buf r = {0};
	bputs(&r, "r ");
	bnum(&r, LM);
	bputc(&r, ' ');
	bnum(&r, y - NOTE_RULE_SPACE * 0.6);
	bputc(&r, ' ');
	bnum(&r, TW / 3);
	bputs(&r, " 0.4");
	fprintf(out, "%s\n", bstr(&r));
	for (size_t k = 0; k < here.n; k++) {
		Note *nt = &notes.v[here.v[k] - 1];
		if (nt->el < el_page_cache_n) {
			el_page[nt->el] = (int)pi;
			el_y[nt->el] = MAX(0, y - BS);
		}
		for (size_t i = 0; i < nt->lines.n; i++) {
			V *v = &nt->lines.v[i];
			y += i ? v->lead : v->h;
			for (size_t q = 0; q < v->ops.n; q++)
				emit_op(&v->ops.v[q], LM + v->x, y, lr);
		}
		y += (nt->lines.n ? nt->lines.v[nt->lines.n - 1].d : 0) + NOTE_GAP;
	}
	close_span();
}

static void
emit(void)
{
	fputs("x tekx-pdl 1\n", out);
	if (meta_title)
		fprintf(out, "m title %s\n", meta_title);
	for (size_t i = 0; i < authors.n; i++)
		fprintf(out, "m author %s\n", authors.v[i]);
	fprintf(out, "m language %s\n", language);
	if (meta_subject || meta_subtitle)
		fprintf(out, "m subject %s\n", meta_subject ? meta_subject : meta_subtitle);
	if (meta_keywords)
		fprintf(out, "m keywords %s\n", meta_keywords);
	fprintf(out, "m date %lld\n", (long long)doc_time());

	/* make sure every face that will be drawn, in the text or its notes, is declared */
	for (size_t n = 0; n <= notes.n; n++) {
		VList *l = n < notes.n ? &notes.v[n].lines : &vl;
		for (size_t i = 0; i < l->n; i++)
			for (size_t k = 0; k < l->v[i].ops.n; k++)
				if (l->v[i].ops.v[k].kind == O_GLYPH || l->v[i].ops.v[k].kind == O_INVIS ||
				    l->v[i].ops.v[k].kind == O_PAGEREF)
					pdl_font(l->v[i].ops.v[k].face);
	}
	pdl_font(FACE_REG);
	for (size_t i = 0; i < font_order.n; i++)
		fprintf(out, "f %zu %s\n", i, logical_font_source(face_name[font_order.v[i]]));

	for (size_t i = 0; i < els.n; i++) {
		El *e = &els.v[i];
		if (e->parent < 0)
			fprintf(out, "e %zu - %s %s\n", i, e->ns, e->tag);
		else
			fprintf(out, "e %zu %d %s %s\n", i, e->parent, e->ns, e->tag);
	}
	for (size_t i = 0; i < props.n; i++)
		fprintf(out, "a %d %s %s\n", props.v[i].el, props.v[i].key, props.v[i].val);
	/* each contents entry refers to its section (PDF/UA-2 8.2.5.8) */
	for (size_t i = 0; i < links.n; i++) {
		Link *l = &links.v[i];
		if (!l->label)
			continue;
		int p = els.v[l->el].parent, ti = p >= 0 ? els.v[p].parent : -1;
		Label *lab = find_label(l->label);
		if (ti >= 0 && streq(els.v[ti].tag, "TOCI") && lab && lab->el >= 0)
			fprintf(out, "a %d ref %d\n", ti, lab->el);
	}
	/* attributes grouped by element and owner */
	for (size_t i = 0; i < attrs.n; i++)
		fprintf(out, "A %d %s %s %c %s\n", attrs.v[i].el, attrs.v[i].owner, attrs.v[i].key, attrs.v[i].type,
		    attrs.v[i].val);

	/* destinations: page and top of the first line of each element */
	el_page_cache_n = (int)els.n;
	el_page = xcalloc(els.n + 1, sizeof *el_page);
	el_y = xcalloc(els.n + 1, sizeof *el_y);
	for (size_t i = 0; i < els.n; i++)
		el_page[i] = -1;
	for (size_t i = 0; i < vl.n; i++) {
		V *v = &vl.v[i];
		for (size_t k = 0; k < v->dests.n; k++) {
			int e = v->dests.v[k];
			if (el_page[e] < 0) {
				el_page[e] = v->page;
				el_y[e] = MAX(0, v->y - v->h - 0.5 * BS);
			}
		}
	}
	for (size_t i = 0; i < outlines.n; i++) {
		Outline *o = &outlines.v[i];
		if (o->vi < 0)
			continue;
		V *v = &vl.v[o->vi];
		Buf b = {0};
		bprintf(&b, "o %d %d ", o->level, v->page);
		bnum(&b, MAX(0, v->y - v->h - 0.5 * BS));
		fprintf(out, "%s %d %s\n", bstr(&b), o->el, o->title);
	}

	LinkRects lr = {0};
	for (size_t pi = 0; pi < pages.n; pi++) {
		Buf pb = {0};
		bputs(&pb, "p ");
		bnum(&pb, PW);
		bputc(&pb, ' ');
		bnum(&pb, PH);
		fprintf(out, "%s\n", bstr(&pb));
		for (size_t li = 0; li < pages.v[pi].lines.n; li++) {
			V *v = &vl.v[pages.v[pi].lines.v[li]];
			if (v->newpage)
				continue;
			double x0 = LM + v->x, y = v->y;
			for (size_t k = 0; k < v->ops.n; k++)
				emit_op(&v->ops.v[k], x0, y, &lr);
		}
		close_span();
		emit_notes(pi, &lr);
		emit_page_number((int)pi);
		emit_link_rects(&lr);
	}
}

int
set_main(FILE *in, FILE *o, const char *file)
{
	diag_file = file;
	out = o;
	Node *doc = tree_read(in);
	if (*doc->rest)
		diag_file = doc->rest;	/* the source file, named by parse */
	doc_root = doc;
	read_meta(doc);
	number_document(doc);
	hyph_init();

	TW = 32.7 * BS;
	if (TW > PW - 2 * 54)
		TW = PW - 2 * 54;
	LM = (PW - TW) / 2;
	TOP = PH * 0.11 + BS;
	BOT = PH - PH * 0.13;

	doc_el = el(-1, "pdf2", "Document");
	if (meta_title)
		prop(doc_el, "title", meta_title);
	Box_ bx = {doc_el, {0, TW}, BS, FACE_REG};
	collect_sections(doc);
	title_block(bx);
	if (child(doc, "abstract") == NULL)
		classification(bx);
	after_heading = true;
	blocks(doc, bx);
	if (vl.n == 0)
		vline(BS * 0.72, BS * 0.22, lead_for(BS), 0, NULL);
	paginate();
	emit();
	return nerrors ? 1 : 0;
}
