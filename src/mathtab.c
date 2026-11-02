/*
 * mathtab.c -- OpenType MATH table queries, read in place.
 */
#include "font.h"

#define M (f->data + f->mathoff)
#define ML (f->mathlen)

static size_t
glyph_info(Font *f, int which)
{
	size_t gi;

	if (!f->hasmath || (gi = u16at(M, ML, 6)) == 0)
		return 0;
	size_t sub = u16at(M, ML, gi + 2 * (size_t)which);
	return sub ? gi + sub : 0;
}

/* value-record table with coverage: coverage offset, count, MathValueRecords */
static bool
value_lookup(Font *f, size_t tab, int g, int *v)
{
	if (!tab)
		return false;
	int ci = cov_index(M, ML, tab + u16at(M, ML, tab), g);
	if (ci < 0 || ci >= u16at(M, ML, tab + 2))
		return false;
	*v = (i16)u16at(M, ML, tab + 4 + 4 * (size_t)ci);
	return true;
}

int
math_italic(Font *f, int g)
{
	int v = 0;

	value_lookup(f, glyph_info(f, 0), g, &v);
	return v;
}

int
math_top_accent(Font *f, int g, bool *found)
{
	int v = 0;

	*found = value_lookup(f, glyph_info(f, 1), g, &v);
	return v;
}

bool
math_extended(Font *f, int g)
{
	size_t t = glyph_info(f, 2);

	return t && cov_index(M, ML, t, g) >= 0;
}

int
math_kern(Font *f, int g, int corner, double height)
{
	size_t t = glyph_info(f, 3);

	if (!t)
		return 0;
	int ci = cov_index(M, ML, t + u16at(M, ML, t), g);
	if (ci < 0 || ci >= u16at(M, ML, t + 2))
		return 0;
	size_t rec = t + 4 + 8 * (size_t)ci;
	size_t ko = u16at(M, ML, rec + 2 * (size_t)corner);
	if (!ko)
		return 0;
	size_t k = t + ko;
	int nh = u16at(M, ML, k);
	for (int i = 0; i < nh; i++) {
		int h = (i16)u16at(M, ML, k + 2 + 4 * (size_t)i);
		if (height < h)
			return (i16)u16at(M, ML, k + 2 + 4 * (size_t)nh + 4 * (size_t)i);
	}
	return (i16)u16at(M, ML, k + 2 + 4 * (size_t)nh + 4 * (size_t)nh);
}

int
math_min_overlap(Font *f)
{
	size_t v;

	if (!f->hasmath || (v = u16at(M, ML, 8)) == 0)
		return 0;
	return u16at(M, ML, v);
}

bool
math_construction(Font *f, int g, bool vertical, Construction *c)
{
	size_t v;

	memset(c, 0, sizeof *c);
	if (!f->hasmath || (v = u16at(M, ML, 8)) == 0)
		return false;
	Rd r = rd_at(M, ML, v + 2);
	size_t vcov = rd_u16(&r), hcov = rd_u16(&r);
	int vn = rd_u16(&r), hn = rd_u16(&r);
	size_t cov = vertical ? vcov : hcov;
	if (!cov)
		return false;
	int ci = cov_index(M, ML, v + cov, g);
	if (ci < 0 || ci >= (vertical ? vn : hn))
		return false;
	size_t slot = v + 10 + 2 * (size_t)((vertical ? 0 : vn) + ci);
	size_t co = u16at(M, ML, slot);
	if (!co)
		return false;
	size_t con = v + co;
	Rd cr = rd_at(M, ML, con);
	size_t asmb = rd_u16(&cr);
	int nv = rd_u16(&cr);
	for (int i = 0; i < nv && !cr.err; i++) {
		u16 vg = rd_u16(&cr), adv = rd_u16(&cr);
		if (c->nvariants < (int)NELEM(c->variant)) {
			c->variant[c->nvariants] = vg;
			c->advance[c->nvariants] = adv;
			c->nvariants++;
		}
	}
	if (asmb) {
		Rd ar = rd_at(M, ML, con + asmb);
		c->italic = rd_i16(&ar);
		rd_skip(&ar, 2);
		int np = rd_u16(&ar);
		for (int i = 0; i < np && !ar.err && c->nparts < (int)NELEM(c->part); i++) {
			GlyphPart *p = &c->part[c->nparts++];
			p->glyph = rd_u16(&ar);
			p->start = rd_u16(&ar);
			p->end = rd_u16(&ar);
			p->full = rd_u16(&ar);
			p->extender = rd_u16(&ar) & 1;
		}
	}
	return !cr.err;
}
