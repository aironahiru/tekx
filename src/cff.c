/*
 * cff.c -- Compact Font Format: parsing, Type 2 charstrings, subsetting.
 *
 * Subsets keep glyph ids (CID == GID): unused charstrings become a bare
 * `endchar` and unused subroutines a bare `return`.  Brute force, and it
 * keeps every index valid without renumbering anything.
 */
#include "font.h"

#include <math.h>

typedef struct {
	size_t *off;	/* count+1 absolute offsets */
	int count;
} Index;

typedef struct {
	u16 op;
	double v[48];
	int nv;
	size_t start, end;	/* raw bytes including operator */
} Entry;

typedef struct {
	Entry *e;
	int n;
	const u8 *base;
} Dict;

typedef struct {
	Dict priv;
	Index subrs;
	size_t poff;	/* absolute offset of the Private DICT */
} Priv;

struct Cff {
	const u8 *p;
	size_t n;
	char *name;
	Dict top;
	Index gsubrs, cs;
	Priv *fd;
	int nfd;
	u8 *fdsel;
	bool cid;
};

static bool
parse_index(const u8 *p, size_t n, size_t off, Index *ix, size_t *end)
{
	Rd r = rd_at(p, n, off);
	int count = rd_u16(&r);

	ix->count = count;
	ix->off = NULL;
	if (count == 0) {
		*end = off + 2;
		return !r.err;
	}
	int os = rd_u8(&r);
	ix->off = xmalloc(((size_t)count + 1) * sizeof *ix->off);
	size_t base = off + 3 + (size_t)os * ((size_t)count + 1) - 1;
	for (int i = 0; i <= count; i++) {
		ix->off[i] = base + rd_uint(&r, os);
		if (ix->off[i] > n || (i > 0 && ix->off[i] < ix->off[i - 1]))
			return false;
	}
	*end = ix->off[count];
	return !r.err;
}

static double
parse_real(const u8 *p, size_t n, size_t *i)
{
	char s[64];
	int k = 0;

	while (*i < n) {
		u8 b = p[(*i)++];
		int nib[2] = {b >> 4, b & 15};
		for (int j = 0; j < 2; j++) {
			int c = nib[j];
			if (c == 15)
				goto done;
			if (k > 56)
				continue;
			if (c <= 9)
				s[k++] = (char)('0' + c);
			else if (c == 10)
				s[k++] = '.';
			else if (c == 11)
				s[k++] = 'E';
			else if (c == 12) {
				s[k++] = 'E';
				s[k++] = '-';
			} else if (c == 14)
				s[k++] = '-';
		}
	}
done:
	s[k] = 0;
	return strtod(s, NULL);
}

static bool
parse_dict(const u8 *p, size_t start, size_t end, Dict *d)
{
	VEC(Entry) es = {0};
	Entry cur = {0};
	size_t i = start;

	cur.start = start;
	d->base = p;
	while (i < end) {
		u8 b = p[i];
		if (b <= 21) {
			cur.op = b;
			if (b == 12) {
				if (++i >= end)
					return false;
				cur.op = (u16)(1200 + p[i]);
			}
			i++;
			cur.end = i;
			VPUSH(es, cur);
			memset(&cur, 0, sizeof cur);
			cur.start = i;
			continue;
		}
		double v;
		if (b == 28) {
			if (i + 2 >= end)
				return false;
			v = (i16)(p[i + 1] << 8 | p[i + 2]);
			i += 3;
		} else if (b == 29) {
			if (i + 4 >= end)
				return false;
			v = (i32)((u32)p[i + 1] << 24 | (u32)p[i + 2] << 16 | (u32)p[i + 3] << 8 | p[i + 4]);
			i += 5;
		} else if (b == 30) {
			i++;
			v = parse_real(p, end, &i);
		} else if (b >= 32 && b <= 246) {
			v = b - 139;
			i++;
		} else if (b >= 247 && b <= 250) {
			if (i + 1 >= end)
				return false;
			v = (b - 247) * 256 + p[i + 1] + 108;
			i += 2;
		} else if (b >= 251 && b <= 254) {
			if (i + 1 >= end)
				return false;
			v = -(b - 251) * 256 - p[i + 1] - 108;
			i += 2;
		} else {
			return false;
		}
		if (cur.nv < 48)
			cur.v[cur.nv++] = v;
	}
	d->e = es.v;
	d->n = (int)es.n;
	return true;
}

static Entry *
dict_get(Dict *d, u16 op)
{
	for (int i = 0; i < d->n; i++)
		if (d->e[i].op == op)
			return &d->e[i];
	return NULL;
}

static bool
read_private(Cff *c, Dict *owner, Priv *pv)
{
	Entry *e = dict_get(owner, 18);
	size_t end;

	memset(pv, 0, sizeof *pv);
	if (e == NULL || e->nv < 2)
		return true;
	size_t size = (size_t)e->v[0], off = (size_t)e->v[1];
	if (off + size > c->n)
		return false;
	pv->poff = off;
	if (!parse_dict(c->p, off, off + size, &pv->priv))
		return false;
	Entry *s = dict_get(&pv->priv, 19);
	if (s && s->nv > 0)
		return parse_index(c->p, c->n, off + (size_t)s->v[0], &pv->subrs, &end);
	return true;
}

Cff *
cff_parse(const u8 *p, size_t n, char **err)
{
	Cff *c = NEW(Cff);
	Index names, tops, strings;
	size_t a, b, d, e;

	c->p = p;
	c->n = n;
	if (n < 4 || p[0] != 1) {
		*err = xstrdup("unsupported CFF version");
		return NULL;
	}
	if (!parse_index(p, n, p[2], &names, &a) || !parse_index(p, n, a, &tops, &b) ||
	    !parse_index(p, n, b, &strings, &d) || !parse_index(p, n, d, &c->gsubrs, &e) ||
	    tops.count < 1) {
		*err = xstrdup("corrupt CFF header indexes");
		return NULL;
	}
	c->name = names.count ? xstrndup((const char *)p + names.off[0], names.off[1] - names.off[0]) : xstrdup("");
	if (!parse_dict(p, tops.off[0], tops.off[1], &c->top)) {
		*err = xstrdup("corrupt CFF Top DICT");
		return NULL;
	}
	Entry *cs = dict_get(&c->top, 17);
	if (cs == NULL || !parse_index(p, n, (size_t)cs->v[0], &c->cs, &a)) {
		*err = xstrdup("CFF without CharStrings");
		return NULL;
	}
	c->cid = dict_get(&c->top, 1230) != NULL;
	if (c->cid) {
		Entry *fa = dict_get(&c->top, 1236), *fs = dict_get(&c->top, 1237);
		Index fds;
		if (!fa || !fs || !parse_index(p, n, (size_t)fa->v[0], &fds, &a)) {
			*err = xstrdup("CID CFF without FDArray/FDSelect");
			return NULL;
		}
		c->nfd = fds.count;
		c->fd = xcalloc((size_t)c->nfd + 1, sizeof *c->fd);
		for (int i = 0; i < fds.count; i++) {
			Dict fd;
			if (!parse_dict(p, fds.off[i], fds.off[i + 1], &fd) || !read_private(c, &fd, &c->fd[i])) {
				*err = xstrdup("corrupt CFF FD");
				return NULL;
			}
		}
		c->fdsel = xcalloc((size_t)c->cs.count + 1, 1);
		Rd r = rd_at(p, n, (size_t)fs->v[0]);
		int format = rd_u8(&r);
		if (format == 0) {
			for (int g = 0; g < c->cs.count; g++)
				c->fdsel[g] = rd_u8(&r);
		} else if (format == 3) {
			int nr = rd_u16(&r), first = rd_u16(&r);
			for (int k = 0; k < nr; k++) {
				int fd = rd_u8(&r), next = rd_u16(&r);
				for (int g = first; g < next && g < c->cs.count; g++)
					c->fdsel[g] = (u8)fd;
				first = next;
			}
		} else {
			*err = fmt("unsupported FDSelect format %d", format);
			return NULL;
		}
	} else {
		c->nfd = 1;
		c->fd = xcalloc(1, sizeof *c->fd);
		if (!read_private(c, &c->top, &c->fd[0])) {
			*err = xstrdup("corrupt CFF Private DICT");
			return NULL;
		}
	}
	return c;
}

int
cff_nglyphs(Cff *c)
{
	return c->cs.count;
}

static int
fd_of(Cff *c, int g)
{
	if (!c->cid || g < 0 || g >= c->cs.count)
		return 0;
	return c->fdsel[g] < c->nfd ? c->fdsel[g] : 0;
}

static int
bias(int count)
{
	return count < 1240 ? 107 : count < 33900 ? 1131 : 32768;
}

/* Type 2 charstring interpreter */
typedef struct {
	Cff *c;
	Priv *pv;
	double st[96];
	int sp;
	double x, y, x0, y0, x1, y1;
	bool any, width_done, ended;
	int nstems, depth;
	u8 *usedg, *usedl;	/* subroutine marks, may be NULL */
} Interp;

static void
pt(Interp *t, double x, double y)
{
	if (!t->any) {
		t->x0 = t->x1 = x;
		t->y0 = t->y1 = y;
		t->any = true;
	}
	if (x < t->x0) t->x0 = x;
	if (x > t->x1) t->x1 = x;
	if (y < t->y0) t->y0 = y;
	if (y > t->y1) t->y1 = y;
}

static void
lineto(Interp *t, double dx, double dy)
{
	t->x += dx;
	t->y += dy;
	pt(t, t->x, t->y);
}

static void
curveto(Interp *t, double a, double b, double c, double d, double e, double f)
{
	double p0x = t->x, p0y = t->y;
	double p1x = p0x + a, p1y = p0y + b, p2x = p1x + c, p2y = p1y + d, p3x = p2x + e, p3y = p2y + f;

	pt(t, p3x, p3y);
	for (int axis = 0; axis < 2; axis++) {
		double q0 = axis ? p0y : p0x, q1 = axis ? p1y : p1x, q2 = axis ? p2y : p2x, q3 = axis ? p3y : p3x;
		double qa = -q0 + 3 * q1 - 3 * q2 + q3, qb = 2 * (q0 - 2 * q1 + q2), qc = q1 - q0;
		double roots[2];
		int nr = 0;
		if (fabs(qa) < 1e-12) {
			if (fabs(qb) > 1e-12)
				roots[nr++] = -qc / qb;
		} else {
			double disc = qb * qb - 4 * qa * qc;
			if (disc >= 0) {
				roots[nr++] = (-qb + sqrt(disc)) / (2 * qa);
				roots[nr++] = (-qb - sqrt(disc)) / (2 * qa);
			}
		}
		for (int k = 0; k < nr; k++) {
			double s = roots[k], m = 1 - s;
			if (s <= 0 || s >= 1)
				continue;
			double x = m * m * m * p0x + 3 * m * m * s * p1x + 3 * m * s * s * p2x + s * s * s * p3x;
			double y = m * m * m * p0y + 3 * m * m * s * p1y + 3 * m * s * s * p2y + s * s * s * p3y;
			pt(t, x, y);
		}
	}
	t->x = p3x;
	t->y = p3y;
}

static void
width(Interp *t, bool even_expected)
{
	if (t->width_done)
		return;
	t->width_done = true;
	if ((t->sp % 2 == 1) == even_expected && t->sp > 0) {
		memmove(t->st, t->st + 1, (size_t)(t->sp - 1) * sizeof t->st[0]);
		t->sp--;
	}
}

static int
exec(Interp *t, const u8 *code, size_t n)
{
	size_t i = 0;

	if (t->depth > 10)
		return -1;
	while (i < n && !t->ended) {
		u8 b = code[i];
		double *s = t->st;
		int k;
		if (t->sp >= 90)
			return -1;
		if (b >= 32 && b <= 246) {
			s[t->sp++] = b - 139;
			i++;
			continue;
		}
		if (b >= 247 && b <= 250) {
			s[t->sp++] = (b - 247) * 256 + (i + 1 < n ? code[i + 1] : 0) + 108;
			i += 2;
			continue;
		}
		if (b >= 251 && b <= 254) {
			s[t->sp++] = -(b - 251) * 256 - (i + 1 < n ? code[i + 1] : 0) - 108;
			i += 2;
			continue;
		}
		if (b == 28) {
			if (i + 2 >= n)
				return -1;
			s[t->sp++] = (i16)(code[i + 1] << 8 | code[i + 2]);
			i += 3;
			continue;
		}
		if (b == 255) {
			if (i + 4 >= n)
				return -1;
			s[t->sp++] = (i32)((u32)code[i + 1] << 24 | (u32)code[i + 2] << 16 |
			    (u32)code[i + 3] << 8 | code[i + 4]) / 65536.0;
			i += 5;
			continue;
		}
		i++;
		switch (b) {
		case 1: case 3: case 18: case 23:	/* stems */
			width(t, true);
			t->nstems += t->sp / 2;
			t->sp = 0;
			break;
		case 19: case 20:	/* hintmask, cntrmask */
			width(t, true);
			t->nstems += t->sp / 2;
			t->sp = 0;
			i += (size_t)(t->nstems + 7) / 8;
			break;
		case 21:	/* rmoveto */
			width(t, true);
			if (t->sp >= 2) {
				t->x += s[t->sp - 2];
				t->y += s[t->sp - 1];
			}
			t->sp = 0;
			break;
		case 22:	/* hmoveto */
			width(t, false);
			if (t->sp >= 1)
				t->x += s[t->sp - 1];
			t->sp = 0;
			break;
		case 4:		/* vmoveto */
			width(t, false);
			if (t->sp >= 1)
				t->y += s[t->sp - 1];
			t->sp = 0;
			break;
		case 5:		/* rlineto */
			for (k = 0; k + 1 < t->sp; k += 2)
				lineto(t, s[k], s[k + 1]);
			t->sp = 0;
			break;
		case 6: case 7: {	/* hlineto, vlineto */
			bool h = b == 6;
			for (k = 0; k < t->sp; k++, h = !h)
				lineto(t, h ? s[k] : 0, h ? 0 : s[k]);
			t->sp = 0;
			break;
		}
		case 8:		/* rrcurveto */
			for (k = 0; k + 5 < t->sp; k += 6)
				curveto(t, s[k], s[k + 1], s[k + 2], s[k + 3], s[k + 4], s[k + 5]);
			t->sp = 0;
			break;
		case 24:	/* rcurveline */
			for (k = 0; k + 7 < t->sp; k += 6)
				curveto(t, s[k], s[k + 1], s[k + 2], s[k + 3], s[k + 4], s[k + 5]);
			if (t->sp >= 2)
				lineto(t, s[t->sp - 2], s[t->sp - 1]);
			t->sp = 0;
			break;
		case 25:	/* rlinecurve */
			if (t->sp >= 6) {
				for (k = 0; k + 1 < t->sp - 6; k += 2)
					lineto(t, s[k], s[k + 1]);
				k = t->sp - 6;
				curveto(t, s[k], s[k + 1], s[k + 2], s[k + 3], s[k + 4], s[k + 5]);
			}
			t->sp = 0;
			break;
		case 26: {	/* vvcurveto */
			double dx1 = 0;
			k = 0;
			if (t->sp % 4 == 1)
				dx1 = s[k++];
			for (; k + 3 < t->sp; k += 4, dx1 = 0)
				curveto(t, dx1, s[k], s[k + 1], s[k + 2], 0, s[k + 3]);
			t->sp = 0;
			break;
		}
		case 27: {	/* hhcurveto */
			double dy1 = 0;
			k = 0;
			if (t->sp % 4 == 1)
				dy1 = s[k++];
			for (; k + 3 < t->sp; k += 4, dy1 = 0)
				curveto(t, s[k], dy1, s[k + 1], s[k + 2], s[k + 3], 0);
			t->sp = 0;
			break;
		}
		case 30: case 31: {	/* vhcurveto, hvcurveto */
			bool h = b == 31;
			for (k = 0; k + 3 < t->sp; k += 4, h = !h) {
				bool last = k + 4 == t->sp - 1;
				double ex = last ? s[k + 4] : 0;
				if (h)
					curveto(t, s[k], 0, s[k + 1], s[k + 2], ex, s[k + 3]);
				else
					curveto(t, 0, s[k], s[k + 1], s[k + 2], s[k + 3], ex);
				if (last)
					break;
			}
			t->sp = 0;
			break;
		}
		case 10: case 29: {	/* callsubr, callgsubr */
			Index *ix = b == 10 ? &t->pv->subrs : &t->c->gsubrs;
			u8 *mark = b == 10 ? t->usedl : t->usedg;
			if (t->sp < 1)
				return -1;
			int idx = (int)s[--t->sp] + bias(ix->count);
			if (idx < 0 || idx >= ix->count)
				return -1;
			if (mark)
				mark[idx] = 1;
			t->depth++;
			if (exec(t, t->c->p + ix->off[idx], ix->off[idx + 1] - ix->off[idx]) < 0)
				return -1;
			t->depth--;
			break;
		}
		case 11:	/* return */
			return 0;
		case 14:	/* endchar */
			if (!t->width_done) {
				t->width_done = true;
				if (t->sp == 1 || t->sp == 5)
					t->sp--;
			}
			t->ended = true;
			return 0;
		case 12: {	/* flex family */
			u8 b1 = i < n ? code[i] : 0;
			i++;
			if (b1 == 35 && t->sp >= 12) {
				curveto(t, s[0], s[1], s[2], s[3], s[4], s[5]);
				curveto(t, s[6], s[7], s[8], s[9], s[10], s[11]);
			} else if (b1 == 34 && t->sp >= 7) {
				curveto(t, s[0], 0, s[1], s[2], s[3], 0);
				curveto(t, s[4], 0, s[5], -s[2], s[6], 0);
			} else if (b1 == 36 && t->sp >= 9) {
				curveto(t, s[0], s[1], s[2], s[3], s[4], 0);
				curveto(t, s[5], 0, s[6], s[7], s[8], -(s[1] + s[3] + s[7]));
			} else if (b1 == 37 && t->sp >= 11) {
				double dx = s[0] + s[2] + s[4] + s[6] + s[8], dy = s[1] + s[3] + s[5] + s[7] + s[9];
				curveto(t, s[0], s[1], s[2], s[3], s[4], s[5]);
				if (fabs(dx) > fabs(dy))
					curveto(t, s[6], s[7], s[8], s[9], s[10], -dy);
				else
					curveto(t, s[6], s[7], s[8], s[9], -dx, s[10]);
			}
			t->sp = 0;
			break;
		}
		default:
			t->sp = 0;
		}
	}
	return 0;
}

static int
run_glyph(Cff *c, int g, Interp *t)
{
	if (g < 0 || g >= c->cs.count)
		return -1;
	t->c = c;
	t->pv = &c->fd[fd_of(c, g)];
	return exec(t, c->p + c->cs.off[g], c->cs.off[g + 1] - c->cs.off[g]);
}

BBox
cff_bbox(Cff *c, int g)
{
	Interp t = {0};
	BBox b = {0, 0, 0, 0};

	if (run_glyph(c, g, &t) == 0 && t.any) {
		b.x0 = t.x0;
		b.y0 = t.y0;
		b.x1 = t.x1;
		b.y1 = t.y1;
	}
	return b;
}

/* ---- subset writer ---- */

static void
put16(Buf *b, unsigned v)
{
	bputc(b, (int)(v >> 8 & 0xFF));
	bputc(b, (int)(v & 0xFF));
}

static void
int5(Buf *b, i32 v)
{
	u32 u = (u32)v;

	bputc(b, 29);
	bputc(b, (int)(u >> 24));
	bputc(b, (int)(u >> 16 & 0xFF));
	bputc(b, (int)(u >> 8 & 0xFF));
	bputc(b, (int)(u & 0xFF));
}

static int
offsize(size_t max)
{
	return max < 0x100 ? 1 : max < 0x10000 ? 2 : max < 0x1000000 ? 3 : 4;
}

/* An item list for writing INDEX structures. */
typedef struct {
	const u8 *p;
	size_t n;
} Item;

static size_t
index_size(Item *it, int count)
{
	size_t total = 0;

	if (count == 0)
		return 2;
	for (int i = 0; i < count; i++)
		total += it[i].n;
	return 3 + (size_t)offsize(total + 1) * ((size_t)count + 1) + total;
}

static void
write_index(Buf *b, Item *it, int count)
{
	size_t total = 0, off = 1;
	int os;

	put16(b, (unsigned)count);
	if (count == 0)
		return;
	for (int i = 0; i < count; i++)
		total += it[i].n;
	os = offsize(total + 1);
	bputc(b, os);
	for (int i = 0; i <= count; i++) {
		for (int k = os - 1; k >= 0; k--)
			bputc(b, (int)(off >> (8 * k) & 0xFF));
		if (i < count)
			off += it[i].n;
	}
	for (int i = 0; i < count; i++)
		bput(b, it[i].p, it[i].n);
}

static const u8 ENDCHAR[1] = {14}, RETURN[1] = {11};

static void
top_dict(Cff *c, Buf *d, size_t charset, size_t fdsel, size_t cs, size_t fdarr, int n)
{
	int5(d, 391);	/* "Adobe" */
	int5(d, 392);	/* "Identity" */
	int5(d, 0);
	bputc(d, 12);
	bputc(d, 30);	/* ROS: must be first */
	for (int i = 0; i < c->top.n; i++) {
		Entry *e = &c->top.e[i];
		if (e->op == 1207 || e->op == 5)	/* FontMatrix, FontBBox */
			bput(d, c->p + e->start, e->end - e->start);
	}
	int5(d, n);
	bputc(d, 12);
	bputc(d, 34);	/* CIDCount */
	int5(d, (i32)charset);
	bputc(d, 15);
	int5(d, (i32)cs);
	bputc(d, 17);
	int5(d, (i32)fdarr);
	bputc(d, 12);
	bputc(d, 36);
	int5(d, (i32)fdsel);
	bputc(d, 12);
	bputc(d, 37);
}

u8 *
cff_subset(Cff *c, const u8 *used, int nglyphs, const char *name, size_t *outlen)
{
	int n = c->cs.count;
	u8 *keep = xcalloc((size_t)n + 1, 1);
	u8 *ug = xcalloc((size_t)c->gsubrs.count + 1, 1);
	u8 **ul = xcalloc((size_t)c->nfd + 1, sizeof *ul);
	Buf out = {0};

	for (int g = 0; g < n; g++)
		keep[g] = g == 0 || (g < nglyphs && used[g]);
	for (int i = 0; i < c->nfd; i++)
		ul[i] = xcalloc((size_t)c->fd[i].subrs.count + 1, 1);
	for (int g = 0; g < n; g++) {
		if (!keep[g])
			continue;
		Interp t = {0};
		t.usedg = ug;
		t.usedl = ul[fd_of(c, g)];
		run_glyph(c, g, &t);
	}

	Item *cs = xmalloc(((size_t)n + 1) * sizeof *cs);
	for (int g = 0; g < n; g++) {
		if (keep[g]) {
			cs[g].p = c->p + c->cs.off[g];
			cs[g].n = c->cs.off[g + 1] - c->cs.off[g];
		} else {
			cs[g].p = ENDCHAR;
			cs[g].n = 1;
		}
	}
	Item *gs = xmalloc(((size_t)c->gsubrs.count + 1) * sizeof *gs);
	for (int i = 0; i < c->gsubrs.count; i++) {
		if (ug[i]) {
			gs[i].p = c->p + c->gsubrs.off[i];
			gs[i].n = c->gsubrs.off[i + 1] - c->gsubrs.off[i];
		} else {
			gs[i].p = RETURN;
			gs[i].n = 1;
		}
	}

	Buf names = {0}, strings = {0}, gsubrs = {0}, charset = {0}, fdsel = {0}, csidx = {0};
	Item nm = {(const u8 *)name, strlen(name)};
	write_index(&names, &nm, 1);
	Item ss[2] = {{(const u8 *)"Adobe", 5}, {(const u8 *)"Identity", 8}};
	write_index(&strings, ss, 2);
	write_index(&gsubrs, gs, c->gsubrs.count);
	bputc(&charset, 2);
	if (n > 1) {
		put16(&charset, 1);
		put16(&charset, (unsigned)(n - 2));
	}
	{	/* FDSelect format 3 */
		int nr = 0;
		Buf r = {0};
		for (int g = 0; g < n; g++) {
			if (g == 0 || fd_of(c, g) != fd_of(c, g - 1)) {
				put16(&r, (unsigned)g);
				bputc(&r, fd_of(c, g));
				nr++;
			}
		}
		bputc(&fdsel, 3);
		put16(&fdsel, (unsigned)nr);
		bput(&fdsel, r.s, r.n);
		put16(&fdsel, (unsigned)n);
	}
	write_index(&csidx, cs, n);

	/* private dicts and local subrs */
	Buf *priv = xcalloc((size_t)c->nfd + 1, sizeof *priv);
	Buf *subrs = xcalloc((size_t)c->nfd + 1, sizeof *subrs);
	for (int f = 0; f < c->nfd; f++) {
		Priv *pv = &c->fd[f];
		for (int i = 0; i < pv->priv.n; i++) {
			Entry *e = &pv->priv.e[i];
			if (e->op != 19)
				bput(&priv[f], c->p + e->start, e->end - e->start);
		}
		if (pv->subrs.count > 0) {
			Item *ls = xmalloc((size_t)pv->subrs.count * sizeof *ls);
			for (int i = 0; i < pv->subrs.count; i++) {
				if (ul[f][i]) {
					ls[i].p = c->p + pv->subrs.off[i];
					ls[i].n = pv->subrs.off[i + 1] - pv->subrs.off[i];
				} else {
					ls[i].p = RETURN;
					ls[i].n = 1;
				}
			}
			write_index(&subrs[f], ls, pv->subrs.count);
			int5(&priv[f], (i32)(priv[f].n + 6));
			bputc(&priv[f], 19);
		}
	}

	Buf probe = {0};
	top_dict(c, &probe, 0, 0, 0, 0, n);
	Item tp = {(const u8 *)probe.s, probe.n};
	size_t pos = 4 + names.n + index_size(&tp, 1) + strings.n + gsubrs.n;
	size_t charset_off = pos;
	pos += charset.n;
	size_t fdsel_off = pos;
	pos += fdsel.n;
	size_t cs_off = pos;
	pos += csidx.n;
	size_t fdarr_off = pos;
	Item *fdi = xmalloc((size_t)c->nfd * sizeof *fdi);
	Buf *fdd = xcalloc((size_t)c->nfd, sizeof *fdd);
	for (int f = 0; f < c->nfd; f++) {
		fdi[f].n = 11;
	}
	pos += index_size(fdi, c->nfd);
	for (int f = 0; f < c->nfd; f++) {
		int5(&fdd[f], (i32)priv[f].n);
		int5(&fdd[f], (i32)pos);
		bputc(&fdd[f], 18);
		fdi[f].p = (const u8 *)fdd[f].s;
		fdi[f].n = fdd[f].n;
		pos += priv[f].n + subrs[f].n;
	}
	Buf top = {0};
	top_dict(c, &top, charset_off, fdsel_off, cs_off, fdarr_off, n);
	Item ti = {(const u8 *)top.s, top.n};

	bput(&out, "\x01\x00\x04\x04", 4);
	bput(&out, names.s, names.n);
	write_index(&out, &ti, 1);
	bput(&out, strings.s, strings.n);
	bput(&out, gsubrs.s, gsubrs.n);
	bput(&out, charset.s, charset.n);
	bput(&out, fdsel.s, fdsel.n);
	bput(&out, csidx.s, csidx.n);
	write_index(&out, fdi, c->nfd);
	for (int f = 0; f < c->nfd; f++) {
		bput(&out, priv[f].s ? priv[f].s : "", priv[f].n);
		bput(&out, subrs[f].s ? subrs[f].s : "", subrs[f].n);
	}
	*outlen = out.n;
	return (u8 *)out.s;
}
