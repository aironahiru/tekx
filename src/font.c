/*
 * font.c -- sfnt container, cmap, metrics, glyph boxes.
 */
#include "font.h"

Rd
rd_at(const u8 *p, size_t n, size_t pos)
{
	Rd r = {p, n, pos, pos > n};

	return r;
}

static const u8 *
take(Rd *r, size_t k)
{
	const u8 *s;

	if (r->err || r->pos + k > r->n || r->pos + k < r->pos) {
		r->err = 1;
		return NULL;
	}
	s = r->p + r->pos;
	r->pos += k;
	return s;
}

u8
rd_u8(Rd *r)
{
	const u8 *s = take(r, 1);

	return s ? s[0] : 0;
}

u16
rd_u16(Rd *r)
{
	const u8 *s = take(r, 2);

	return s ? (u16)(s[0] << 8 | s[1]) : 0;
}

i16
rd_i16(Rd *r)
{
	return (i16)rd_u16(r);
}

u32
rd_u24(Rd *r)
{
	const u8 *s = take(r, 3);

	return s ? (u32)s[0] << 16 | (u32)s[1] << 8 | s[2] : 0;
}

u32
rd_u32(Rd *r)
{
	const u8 *s = take(r, 4);

	return s ? (u32)s[0] << 24 | (u32)s[1] << 16 | (u32)s[2] << 8 | s[3] : 0;
}

u32
rd_uint(Rd *r, int size)
{
	switch (size) {
	case 1: return rd_u8(r);
	case 2: return rd_u16(r);
	case 3: return rd_u24(r);
	case 4: return rd_u32(r);
	}
	r->err = 1;
	return 0;
}

void
rd_skip(Rd *r, size_t k)
{
	take(r, k);
}

u16
u16at(const u8 *p, size_t n, size_t off)
{
	Rd r = rd_at(p, n, off);

	return rd_u16(&r);
}

u32
sfnt_checksum(const u8 *p, size_t n)
{
	u32 sum = 0;
	size_t i;

	for (i = 0; i < n; i += 4) {
		u32 w = 0;
		for (size_t k = 0; k < 4; k++)
			w = w << 8 | (i + k < n ? p[i + k] : 0);
		sum += w;
	}
	return sum;
}

/* table directory lookup */
static bool
find_table(const u8 *d, size_t len, const char *tag, size_t *off, size_t *tlen)
{
	Rd r = rd_at(d, len, 4);
	u16 n = rd_u16(&r);

	rd_skip(&r, 6);
	for (u16 i = 0; i < n && !r.err; i++) {
		const u8 *t = r.p + r.pos;
		rd_skip(&r, 8);
		u32 o = rd_u32(&r), l = rd_u32(&r);
		if (!r.err && memcmp(t, tag, 4) == 0) {
			if ((size_t)o + l > len)
				return false;
			*off = o;
			*tlen = l;
			return true;
		}
	}
	return false;
}

const u8 *
font_table(Font *f, const char *tag, size_t *len)
{
	size_t off, n;

	if (!find_table(f->data, f->len, tag, &off, &n))
		return NULL;
	if (len)
		*len = n;
	return f->data + off;
}

static char *
name_string(const u8 *t, size_t n, int want)
{
	Rd r = rd_at(t, n, 2);
	u16 count = rd_u16(&r), storage = rd_u16(&r);
	char *best = NULL;

	for (u16 i = 0; i < count && !r.err; i++) {
		u16 plat = rd_u16(&r);
		rd_skip(&r, 4);
		u16 id = rd_u16(&r), len = rd_u16(&r), off = rd_u16(&r);
		if (id != want || (size_t)storage + off + len > n)
			continue;
		const u8 *s = t + storage + off;
		Buf b = {0};
		if (plat == 3 || plat == 0) {
			for (u16 k = 0; k + 1 < len; k += 2)
				utf8_enc(&b, (u32)(s[k] << 8 | s[k + 1]));
		} else {
			bput(&b, s, len);
		}
		if (plat == 3 || best == NULL)
			best = bstr(&b);
	}
	return best;
}

static int
cmp_cmap(const void *a, const void *b)
{
	u32 x = *(const u32 *)a, y = *(const u32 *)b;

	return x < y ? -1 : x > y;
}

typedef struct { u32 c; u16 g; } CmapEnt;
typedef VEC(CmapEnt) CmapVec;

static void
cmap_add(CmapVec *m, u32 c, u32 g)
{
	CmapEnt e = {c, (u16)g};

	if (g != 0 && g < 0x10000)
		VPUSH(*m, e);
}

static bool
parse_cmap(Font *f, const u8 *t, size_t n)
{
	CmapVec m = {0};
	Rd r = rd_at(t, n, 2);
	u16 count = rd_u16(&r);
	size_t best[3] = {0, 0, 0};	/* priority 1..3 offsets */

	for (u16 i = 0; i < count && !r.err; i++) {
		u16 p = rd_u16(&r), e = rd_u16(&r);
		u32 off = rd_u32(&r);
		int pri = 0;
		if ((p == 3 && e == 10) || (p == 0 && (e == 4 || e == 6)))
			pri = 3;
		else if ((p == 3 && e == 1) || (p == 0 && e <= 3))
			pri = 2;
		else if (p == 3 && e == 0)
			pri = 1;
		if (pri && !best[pri - 1])
			best[pri - 1] = off;
	}
	/* lower priority first so that better tables override */
	for (int pri = 0; pri < 3; pri++) {
		size_t off = best[pri];
		if (!off)
			continue;
		u16 format = u16at(t, n, off);
		if (format == 4) {
			Rd s = rd_at(t, n, off + 6);
			size_t segs = rd_u16(&s) / 2;
			size_t ends = off + 14, starts = ends + 2 * segs + 2;
			size_t deltas = starts + 2 * segs, ranges = deltas + 2 * segs;
			for (size_t k = 0; k < segs; k++) {
				u32 end = u16at(t, n, ends + 2 * k), start = u16at(t, n, starts + 2 * k);
				i16 delta = (i16)u16at(t, n, deltas + 2 * k);
				u16 ro = u16at(t, n, ranges + 2 * k);
				if (start > end || start == 0xFFFF)
					continue;
				for (u32 c = start; c <= end; c++) {
					u32 g;
					if (ro == 0) {
						g = (c + (u32)delta) & 0xFFFF;
					} else {
						size_t a = ranges + 2 * k + ro + 2 * (c - start);
						u16 raw = u16at(t, n, a);
						g = raw ? (raw + (u32)delta) & 0xFFFF : 0;
					}
					cmap_add(&m, c, g);
				}
			}
		} else if (format == 12) {
			Rd s = rd_at(t, n, off + 12);
			u32 groups = rd_u32(&s);
			for (u32 k = 0; k < groups && !s.err; k++) {
				u32 a = rd_u32(&s), b = rd_u32(&s), g = rd_u32(&s);
				if (b < a || b - a > 0x10FFFF)
					continue;
				for (u32 c = a; c <= b; c++)
					cmap_add(&m, c, g + (c - a));
			}
		}
	}
	if (m.n == 0)
		return false;
	/* stable sort keeps later (better) entries after earlier ones; dedupe keeping the last */
	qsort(m.v, m.n, sizeof *m.v, cmp_cmap);
	size_t w = 0;
	for (size_t i = 0; i < m.n; i++) {
		if (w > 0 && m.v[w - 1].c == m.v[i].c)
			m.v[w - 1] = m.v[i];
		else
			m.v[w++] = m.v[i];
	}
	f->cmap = (void *)m.v;
	f->ncmap = w;
	return true;
}

Font *
font_load(const u8 *d, size_t len, char **err)
{
	Font *f = NEW(Font);
	size_t off, n;
	const u8 *t;
	Rd r;

	f->data = d;
	f->len = len;
	r = rd_at(d, len, 0);
	u32 version = rd_u32(&r);
	if (version != 0x00010000 && version != 0x4F54544F && version != 0x74727565) {
		*err = xstrdup("not an OpenType or TrueType font (collections are not supported)");
		return NULL;
	}
#define NEED(tag) do { if ((t = font_table(f, tag, &n)) == NULL) { \
	*err = fmt("missing required table '%s'", tag); return NULL; } } while (0)

	NEED("head");
	r = rd_at(t, n, 18);
	f->upem = rd_u16(&r);
	r = rd_at(t, n, 36);
	for (int i = 0; i < 4; i++)
		f->bbox[i] = rd_i16(&r);
	u16 mac = rd_u16(&r);
	r = rd_at(t, n, 50);
	bool longloca = rd_i16(&r) == 1;
	if (f->upem < 16) {
		*err = xstrdup("invalid unitsPerEm");
		return NULL;
	}

	NEED("hhea");
	r = rd_at(t, n, 4);
	f->ascender = rd_i16(&r);
	f->descender = rd_i16(&r);
	f->linegap = rd_i16(&r);
	r = rd_at(t, n, 34);
	int nhm = rd_u16(&r);

	NEED("maxp");
	f->nglyphs = u16at(t, n, 4);

	NEED("hmtx");
	f->adv = xcalloc((size_t)f->nglyphs + 1, sizeof *f->adv);
	r = rd_at(t, n, 0);
	u16 last = 0;
	for (int i = 0; i < f->nglyphs; i++) {
		if (i < nhm) {
			last = rd_u16(&r);
			rd_skip(&r, 2);
		}
		f->adv[i] = last;
	}

	f->capheight = 0.7 * f->upem;
	f->xheight = 0.45 * f->upem;
	f->weight = 400;
	f->italic = mac & 2;
	if ((t = font_table(f, "OS/2", &n)) != NULL) {
		r = rd_at(t, n, 0);
		u16 ver = rd_u16(&r);
		rd_skip(&r, 2);
		f->weight = rd_u16(&r);
		rd_skip(&r, 2);
		u16 fstype = rd_u16(&r);
		if ((fstype & 0x000F) == 0x0002) {
			*err = xstrdup("the font's license does not permit embedding (OS/2 fsType = restricted)");
			return NULL;
		}
		if (n >= 78) {
			r = rd_at(t, n, 62);
			u16 sel = rd_u16(&r);
			f->italic |= sel & 1;
			r = rd_at(t, n, 68);
			i16 ta = rd_i16(&r), td = rd_i16(&r), tg = rd_i16(&r);
			if ((sel & 0x80) || (f->ascender == 0 && f->descender == 0)) {
				f->ascender = ta;
				f->descender = td;
				f->linegap = tg;
			}
		}
		if (ver >= 2 && n >= 90) {
			r = rd_at(t, n, 86);
			f->xheight = rd_i16(&r);
			f->capheight = rd_i16(&r);
		}
	}
	if ((t = font_table(f, "post", &n)) != NULL) {
		r = rd_at(t, n, 4);
		f->italic_angle = (i32)rd_u32(&r) / 65536.0;
		rd_skip(&r, 4);
		f->fixed = rd_u32(&r) != 0;
	}
	if ((t = font_table(f, "name", &n)) != NULL) {
		f->psname = name_string(t, n, 6);
		f->family = name_string(t, n, 1);
	}
	if (!f->psname)
		f->psname = xstrdup("Unnamed");
	if (!f->family)
		f->family = xstrdup("Unnamed");

	NEED("cmap");
	if (!parse_cmap(f, t, n)) {
		*err = xstrdup("no usable Unicode cmap subtable");
		return NULL;
	}

	if ((t = font_table(f, "CFF ", &n)) != NULL) {
		if ((f->cff = cff_parse(t, n, err)) == NULL)
			return NULL;
	} else {
		size_t nl, ng;
		const u8 *loca = font_table(f, "loca", &nl), *glyf = font_table(f, "glyf", &ng);
		if (!loca || !glyf) {
			*err = xstrdup("font has neither CFF nor glyf outlines");
			return NULL;
		}
		f->ttf = ttf_parse(loca, nl, glyf, ng, f->nglyphs, longloca);
	}
	f->bbcache = xcalloc((size_t)f->nglyphs + 1, sizeof *f->bbcache);
	f->bbdone = xcalloc((size_t)f->nglyphs + 1, 1);

	if (find_table(d, len, "GSUB", &off, &n)) {
		f->gsub = off;
		f->gsub_len = n;
	}
	if (find_table(d, len, "GPOS", &off, &n)) {
		f->gpos = off;
		f->gpos_len = n;
	}
	if (find_table(d, len, "MATH", &off, &n)) {
		const u8 *m = d + off;
		f->mathoff = off;
		f->mathlen = n;
		size_t co = u16at(m, n, 4);
		r = rd_at(m, n, co);
		f->mc[ScriptPercentScaleDown] = rd_i16(&r);
		f->mc[ScriptScriptPercentScaleDown] = rd_i16(&r);
		f->mc[DelimitedSubFormulaMinHeight] = (i16)rd_u16(&r);
		f->mc[DisplayOperatorMinHeight] = (i16)rd_u16(&r);
		for (int i = MathLeading; i < RadicalDegreeBottomRaisePercent; i++) {
			f->mc[i] = rd_i16(&r);
			rd_skip(&r, 2);	/* device table offset */
		}
		f->mc[RadicalDegreeBottomRaisePercent] = rd_i16(&r);
		f->hasmath = !r.err;
	}
#undef NEED
	return f;
}

Font *
font_builtin(const char *name)
{
	const Asset *a = asset(name);
	char *err = NULL;
	Font *f;

	if (a == NULL)
		die("built-in font %s is missing", name);
	f = font_load(a->data, a->len, &err);
	if (f == NULL)
		die("built-in font %s: %s", name, err);
	return f;
}

int
font_glyph(Font *f, u32 cp)
{
	size_t lo = 0, hi = f->ncmap;

	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		if (f->cmap[mid].c < cp)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo < f->ncmap && f->cmap[lo].c == cp ? f->cmap[lo].g : 0;
}

double
font_adv(Font *f, int g)
{
	return g >= 0 && g < f->nglyphs ? f->adv[g] : 0;
}

BBox
font_bbox(Font *f, int g)
{
	BBox z = {0, 0, 0, 0};

	if (g < 0 || g >= f->nglyphs)
		return z;
	if (!f->bbdone[g]) {
		f->bbcache[g] = f->cff ? cff_bbox(f->cff, g) : ttf_bbox(f->ttf, g);
		f->bbdone[g] = 1;
	}
	return f->bbcache[g];
}

u32
font_pdf_flags(Font *f)
{
	u32 fl = 4;	/* symbolic: glyphs outside the standard Latin set */

	if (f->fixed)
		fl |= 1;
	if (!f->hasmath && !strstr(f->psname, "Sans") && !f->fixed)
		fl |= 2;
	if (f->italic || f->italic_angle != 0)
		fl |= 64;
	return fl;
}

u8 *
font_subset(Font *f, const u8 *used, const char *name, size_t *len, int *kind)
{
	if (f->cff) {
		*kind = PROGRAM_CFF;
		return cff_subset(f->cff, used, f->nglyphs, name, len);
	}
	*kind = PROGRAM_TRUETYPE;
	return ttf_subset(f, f->ttf, used, len);
}
