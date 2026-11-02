/*
 * otl.c -- OpenType Layout on the font's raw tables.
 *
 * GSUB: single (1), alternate (3), ligature (4), contextual (5) and
 * chaining contextual (6) substitution in all formats, with nested
 * lookups and extension subtables.  GPOS: pair kerning (2), extension (9).
 */
#include "font.h"

int
cov_index(const u8 *t, size_t n, size_t off, int g)
{
	Rd r = rd_at(t, n, off);
	int format = rd_u16(&r), count = rd_u16(&r);
	int lo = 0, hi = count;

	if (r.err)
		return -1;
	if (format == 1) {
		while (lo < hi) {
			int mid = (lo + hi) / 2, v = u16at(t, n, off + 4 + 2 * (size_t)mid);
			if (v < g)
				lo = mid + 1;
			else if (v > g)
				hi = mid;
			else
				return mid;
		}
	} else if (format == 2) {
		while (lo < hi) {
			int mid = (lo + hi) / 2;
			size_t rec = off + 4 + 6 * (size_t)mid;
			int s = u16at(t, n, rec), e = u16at(t, n, rec + 2);
			if (e < g)
				lo = mid + 1;
			else if (s > g)
				hi = mid;
			else
				return u16at(t, n, rec + 4) + (g - s);
		}
	}
	return -1;
}

int
class_of(const u8 *t, size_t n, size_t off, int g)
{
	Rd r = rd_at(t, n, off);
	int format = rd_u16(&r);

	if (format == 1) {
		int start = rd_u16(&r), count = rd_u16(&r);
		if (g >= start && g < start + count)
			return u16at(t, n, off + 6 + 2 * (size_t)(g - start));
	} else if (format == 2) {
		int count = rd_u16(&r), lo = 0, hi = count;
		while (lo < hi) {
			int mid = (lo + hi) / 2;
			size_t rec = off + 4 + 6 * (size_t)mid;
			int s = u16at(t, n, rec), e = u16at(t, n, rec + 2);
			if (e < g)
				lo = mid + 1;
			else if (s > g)
				hi = mid;
			else
				return u16at(t, n, rec + 4);
		}
	}
	return 0;
}

/* lookup indices of a feature, union over all scripts */
static int
feature_lookups(const u8 *t, size_t n, const char *tag, int *out, int max)
{
	size_t fl = u16at(t, n, 6);
	Rd r = rd_at(t, n, fl);
	int count = rd_u16(&r), k = 0;

	for (int i = 0; i < count && !r.err; i++) {
		const u8 *tg = t + r.pos;
		rd_skip(&r, 4);
		size_t off = fl + rd_u16(&r);
		if (r.err || memcmp(tg, tag, 4) != 0)
			continue;
		Rd fr = rd_at(t, n, off + 2);
		int nl = rd_u16(&fr);
		for (int j = 0; j < nl && !fr.err; j++) {
			int li = rd_u16(&fr), dup = 0;
			for (int q = 0; q < k; q++)
				dup |= out[q] == li;
			if (!dup && k < max)
				out[k++] = li;
		}
	}
	/* lookups apply in lookup-list order */
	for (int i = 1; i < k; i++)
		for (int j = i; j > 0 && out[j] < out[j - 1]; j--) {
			int x = out[j];
			out[j] = out[j - 1];
			out[j - 1] = x;
		}
	return k;
}

/* subtable offsets of a lookup, unwrapping extensions */
static int
lookup_subs(const u8 *t, size_t n, int li, int ext, int *type, size_t *subs, int max)
{
	size_t ll = u16at(t, n, 8);
	size_t lo = ll + u16at(t, n, ll + 2 + 2 * (size_t)li);
	Rd r = rd_at(t, n, lo);
	int declared = rd_u16(&r), k = 0;

	*type = declared;
	rd_skip(&r, 2);
	int count = rd_u16(&r);
	for (int i = 0; i < count && !r.err && k < max; i++) {
		size_t off = lo + rd_u16(&r);
		if (declared == ext) {
			Rd e = rd_at(t, n, off + 2);
			*type = rd_u16(&e);
			off += rd_u32(&e);
			if (e.err)
				continue;
		}
		subs[k++] = off;
	}
	return k;
}

typedef struct {
	const u8 *t;
	size_t n;
	u16 *g;
	int *c;
	int len;
} Run;

static int apply_at(Run *run, int li, int i, int depth);

/* apply nested lookup records at positions relative to `i` */
static int
apply_records(Run *run, int i, size_t recs, int nrec, int ninput, int depth)
{
	int before = run->len;

	for (int k = 0; k < nrec; k++) {
		int seq = u16at(run->t, run->n, recs + 4 * (size_t)k);
		int li = u16at(run->t, run->n, recs + 4 * (size_t)k + 2);
		if (i + seq < run->len)
			apply_at(run, li, i + seq, depth + 1);
	}
	int consumed = ninput - (before - run->len);
	return i + (consumed > 0 ? consumed : 1);
}

static bool
match_seq(Run *run, int pos, int dir, size_t arr, int count, size_t cdef, int mode)
{
	/* mode 0: glyph ids, 1: classes (cdef), 2: coverage offsets relative to arr base */
	for (int k = 0; k < count; k++) {
		int p = pos + dir * k;
		if (p < 0 || p >= run->len)
			return false;
		int v = u16at(run->t, run->n, arr + 2 * (size_t)k);
		int g = run->g[p];
		if (mode == 0 && g != v)
			return false;
		if (mode == 1 && class_of(run->t, run->n, cdef, g) != v)
			return false;
		if (mode == 2 && cov_index(run->t, run->n, cdef + (size_t)v, g) < 0)
			return false;
	}
	return true;
}

static int
apply_sub(Run *run, int type, size_t off, int i, int depth)
{
	const u8 *t = run->t;
	size_t n = run->n;
	int format = u16at(t, n, off);
	int g = run->g[i];

	if (type == 6 && format == 3) {
		Rd r = rd_at(t, n, off + 2);
		int nb = rd_u16(&r);
		size_t bt = r.pos;
		rd_skip(&r, 2 * (size_t)nb);
		int ni = rd_u16(&r);
		size_t in = r.pos;
		rd_skip(&r, 2 * (size_t)ni);
		int na = rd_u16(&r);
		size_t la = r.pos;
		rd_skip(&r, 2 * (size_t)na);
		int nrec = rd_u16(&r);
		if (r.err || ni == 0)
			return -1;
		if (!match_seq(run, i, 1, in, ni, off, 2) || !match_seq(run, i - 1, -1, bt, nb, off, 2) ||
		    !match_seq(run, i + ni, 1, la, na, off, 2))
			return -1;
		return apply_records(run, i, r.pos, nrec, ni, depth);
	}
	if (type == 5 && format == 3) {
		Rd r = rd_at(t, n, off + 2);
		int ni = rd_u16(&r), nrec = rd_u16(&r);
		size_t in = r.pos;
		if (ni == 0 || !match_seq(run, i, 1, in, ni, off, 2))
			return -1;
		return apply_records(run, i, in + 2 * (size_t)ni, nrec, ni, depth);
	}
	if ((type == 5 || type == 6) && (format == 1 || format == 2)) {
		int ci = cov_index(t, n, off + u16at(t, n, off + 2), g);
		if (ci < 0)
			return -1;
		size_t bcd = 0, icd = 0, lcd = 0, p = off + 4;
		if (format == 2) {
			if (type == 6) {
				bcd = off + u16at(t, n, p);
				icd = off + u16at(t, n, p + 2);
				lcd = off + u16at(t, n, p + 4);
				p += 6;
			} else {
				icd = bcd = lcd = off + u16at(t, n, p);
				p += 2;
			}
		}
		int nsets = u16at(t, n, p);
		int key = format == 1 ? ci : class_of(t, n, icd, g);
		if (key >= nsets)
			return -1;
		size_t setoff = u16at(t, n, p + 2 + 2 * (size_t)key);
		if (setoff == 0)
			return -1;
		size_t set = off + setoff;
		int nrules = u16at(t, n, set);
		int mode = format == 1 ? 0 : 1;
		for (int k = 0; k < nrules; k++) {
			size_t rule = set + u16at(t, n, set + 2 + 2 * (size_t)k);
			Rd r = rd_at(t, n, rule);
			if (type == 6) {
				int nb = rd_u16(&r);
				size_t bt = r.pos;
				rd_skip(&r, 2 * (size_t)nb);
				int ni = rd_u16(&r);
				size_t in = r.pos;
				rd_skip(&r, 2 * (size_t)(ni > 0 ? ni - 1 : 0));
				int na = rd_u16(&r);
				size_t la = r.pos;
				rd_skip(&r, 2 * (size_t)na);
				int nrec = rd_u16(&r);
				if (r.err || ni == 0)
					continue;
				if (match_seq(run, i + 1, 1, in, ni - 1, icd, mode) &&
				    match_seq(run, i - 1, -1, bt, nb, bcd, mode) &&
				    match_seq(run, i + ni, 1, la, na, lcd, mode))
					return apply_records(run, i, r.pos, nrec, ni, depth);
			} else {
				int ni = rd_u16(&r), nrec = rd_u16(&r);
				size_t in = r.pos;
				if (r.err || ni == 0)
					continue;
				if (match_seq(run, i + 1, 1, in, ni - 1, icd, mode))
					return apply_records(run, i, in + 2 * (size_t)(ni - 1), nrec, ni, depth);
			}
		}
		return -1;
	}

	int ci = cov_index(t, n, off + u16at(t, n, off + 2), g);
	if (ci < 0)
		return -1;
	if (type == 1) {
		if (format == 1)
			run->g[i] = (u16)(g + (i16)u16at(t, n, off + 4));
		else
			run->g[i] = u16at(t, n, off + 6 + 2 * (size_t)ci);
		return i + 1;
	}
	if (type == 3) {
		size_t set = off + u16at(t, n, off + 6 + 2 * (size_t)ci);
		if (u16at(t, n, set) > 0)
			run->g[i] = u16at(t, n, set + 2);
		return i + 1;
	}
	if (type == 4) {
		size_t set = off + u16at(t, n, off + 6 + 2 * (size_t)ci);
		int nl = u16at(t, n, set);
		for (int k = 0; k < nl; k++) {
			size_t lig = set + u16at(t, n, set + 2 + 2 * (size_t)k);
			int lg = u16at(t, n, lig), comp = u16at(t, n, lig + 2);
			if (comp < 1 || i + comp > run->len)
				continue;
			bool ok = true;
			for (int q = 1; q < comp && ok; q++)
				ok = run->g[i + q] == u16at(t, n, lig + 2 + 2 * (size_t)q);
			if (!ok)
				continue;
			int sum = 0;
			for (int q = 0; q < comp; q++)
				sum += run->c[i + q];
			run->g[i] = (u16)lg;
			run->c[i] = sum;
			memmove(run->g + i + 1, run->g + i + comp, (size_t)(run->len - i - comp) * sizeof *run->g);
			memmove(run->c + i + 1, run->c + i + comp, (size_t)(run->len - i - comp) * sizeof *run->c);
			run->len -= comp - 1;
			return i + 1;
		}
	}
	return -1;
}

static int
apply_at(Run *run, int li, int i, int depth)
{
	int type;
	size_t subs[64];

	if (depth > 4 || i >= run->len)
		return -1;
	int ns = lookup_subs(run->t, run->n, li, 7, &type, subs, 64);
	for (int k = 0; k < ns; k++) {
		int r = apply_sub(run, type, subs[k], i, depth);
		if (r >= 0)
			return r;
	}
	return -1;
}

int
gsub_apply(Font *f, const char *tag, u16 *glyphs, int *clusters, int n)
{
	int lk[64];
	Run run;

	if (!f->gsub || n == 0)
		return n;
	run.t = f->data + f->gsub;
	run.n = f->gsub_len;
	run.g = glyphs;
	run.c = clusters;
	run.len = n;
	int nl = feature_lookups(run.t, run.n, tag, lk, 64);
	for (int k = 0; k < nl; k++) {
		int i = 0;
		while (i < run.len) {
			int r = apply_at(&run, lk[k], i, 0);
			i = r > i ? r : i + 1;
		}
	}
	return run.len;
}

int
gsub_single(Font *f, const char *tag, int g, int alt)
{
	int lk[64], type;
	size_t subs[64];

	if (!f->gsub)
		return g;
	const u8 *t = f->data + f->gsub;
	size_t n = f->gsub_len;
	int nl = feature_lookups(t, n, tag, lk, 64);
	for (int k = 0; k < nl; k++) {
		int ns = lookup_subs(t, n, lk[k], 7, &type, subs, 64);
		for (int s = 0; s < ns; s++) {
			size_t off = subs[s];
			int format = u16at(t, n, off);
			int ci = cov_index(t, n, off + u16at(t, n, off + 2), g);
			if (ci < 0)
				continue;
			if (type == 1) {
				g = format == 1 ? (u16)(g + (i16)u16at(t, n, off + 4)) : u16at(t, n, off + 6 + 2 * (size_t)ci);
				break;
			}
			if (type == 3) {
				size_t set = off + u16at(t, n, off + 6 + 2 * (size_t)ci);
				int na = u16at(t, n, set);
				if (na > 0)
					g = u16at(t, n, set + 2 + 2 * (size_t)MIN(alt, na - 1));
				break;
			}
		}
	}
	return g;
}

/* ---- GPOS kerning ---- */

static int
vrsize(int vf)
{
	int s = 0;

	for (int b = 0; b < 8; b++)
		s += (vf >> b) & 1;
	return 2 * s;
}

static int
xadv(const u8 *t, size_t n, size_t off, int vf)
{
	int xa = 0, xp = 0;
	size_t p = off;

	for (int b = 0; b < 8; b++) {
		if (!(vf & (1 << b)))
			continue;
		int v = (i16)u16at(t, n, p);
		if (b == 0)
			xp = v;
		if (b == 2)
			xa = v;
		p += 2;
	}
	return xa ? xa : xp;
}

double
font_kern(Font *f, int left, int right)
{
	int lk[32], type;
	size_t subs[64];

	if (!f->gpos)
		return 0;
	const u8 *t = f->data + f->gpos;
	size_t n = f->gpos_len;
	int nl = feature_lookups(t, n, "kern", lk, 32);
	for (int k = 0; k < nl; k++) {
		int ns = lookup_subs(t, n, lk[k], 9, &type, subs, 64);
		if (type != 2)
			continue;
		for (int s = 0; s < ns; s++) {
			size_t off = subs[s];
			Rd r = rd_at(t, n, off);
			int format = rd_u16(&r);
			size_t cov = off + rd_u16(&r);
			int vf1 = rd_u16(&r), vf2 = rd_u16(&r);
			int ci = cov_index(t, n, cov, left);
			if (ci < 0)
				continue;
			if (format == 1) {
				int nsets = rd_u16(&r);
				if (ci >= nsets)
					continue;
				size_t set = off + u16at(t, n, off + 10 + 2 * (size_t)ci);
				int count = u16at(t, n, set);
				size_t rec = 2 + (size_t)vrsize(vf1) + (size_t)vrsize(vf2);
				int lo = 0, hi = count;
				while (lo < hi) {
					int mid = (lo + hi) / 2;
					size_t ro = set + 2 + rec * (size_t)mid;
					int second = u16at(t, n, ro);
					if (second < right)
						lo = mid + 1;
					else if (second > right)
						hi = mid;
					else
						return xadv(t, n, ro + 2, vf1);
				}
			} else if (format == 2) {
				size_t cd1 = off + rd_u16(&r), cd2 = off + rd_u16(&r);
				int n1 = rd_u16(&r), n2 = rd_u16(&r);
				int c1 = class_of(t, n, cd1, left), c2 = class_of(t, n, cd2, right);
				if (c1 >= n1 || c2 >= n2)
					continue;
				size_t rec = (size_t)vrsize(vf1) + (size_t)vrsize(vf2);
				int v = xadv(t, n, r.pos + rec * ((size_t)c1 * (size_t)n2 + (size_t)c2), vf1);
				if (v)
					return v;
			}
		}
	}
	return 0;
}
