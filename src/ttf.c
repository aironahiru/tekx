/*
 * ttf.c -- TrueType glyf/loca: bounding boxes and glyph-id preserving subsets.
 */
#include "font.h"

typedef struct {
	const char *tag;
	const u8 *p;
	size_t n;
} Tab;

struct Ttf {
	const u8 *glyf;
	size_t nglyf;
	size_t *start, *end;
	int n;
};

Ttf *
ttf_parse(const u8 *loca, size_t nloca, const u8 *glyf, size_t nglyf, int n, bool longfmt)
{
	Ttf *t = NEW(Ttf);
	Rd r = rd_at(loca, nloca, 0);
	size_t prev = 0;

	t->glyf = glyf;
	t->nglyf = nglyf;
	t->n = n;
	t->start = xcalloc((size_t)n + 1, sizeof *t->start);
	t->end = xcalloc((size_t)n + 1, sizeof *t->end);
	for (int i = 0; i <= n; i++) {
		size_t off = longfmt ? rd_u32(&r) : (size_t)rd_u16(&r) * 2;
		if (off > nglyf)
			off = nglyf;
		if (i > 0) {
			t->start[i - 1] = prev;
			t->end[i - 1] = off < prev ? prev : off;
		}
		prev = off;
	}
	return t;
}

BBox
ttf_bbox(Ttf *t, int g)
{
	BBox b = {0, 0, 0, 0};

	if (g < 0 || g >= t->n || t->end[g] - t->start[g] < 10)
		return b;
	Rd r = rd_at(t->glyf, t->nglyf, t->start[g] + 2);
	b.x0 = rd_i16(&r);
	b.y0 = rd_i16(&r);
	b.x1 = rd_i16(&r);
	b.y1 = rd_i16(&r);
	return b;
}

/* mark composite components recursively */
static void
closure(Ttf *t, int g, u8 *keep, int depth)
{
	if (g < 0 || g >= t->n || depth > 16 || t->end[g] - t->start[g] < 10)
		return;
	Rd r = rd_at(t->glyf, t->end[g], t->start[g]);
	if (rd_i16(&r) >= 0)
		return;
	rd_skip(&r, 8);
	for (;;) {
		u16 flags = rd_u16(&r), comp = rd_u16(&r);
		if (r.err)
			return;
		if (comp < t->n && !keep[comp]) {
			keep[comp] = 1;
			closure(t, comp, keep, depth + 1);
		}
		size_t skip = (flags & 1) ? 4 : 2;
		if (flags & 0x08)
			skip += 2;
		else if (flags & 0x40)
			skip += 4;
		else if (flags & 0x80)
			skip += 8;
		rd_skip(&r, skip);
		if (!(flags & 0x20))
			return;
	}
}

static void
p32(Buf *b, u32 v)
{
	bputc(b, (int)(v >> 24));
	bputc(b, (int)(v >> 16 & 0xFF));
	bputc(b, (int)(v >> 8 & 0xFF));
	bputc(b, (int)(v & 0xFF));
}

static void
p16(Buf *b, unsigned v)
{
	bputc(b, (int)(v >> 8 & 0xFF));
	bputc(b, (int)(v & 0xFF));
}

u8 *
ttf_subset(Font *f, Ttf *t, const u8 *used, size_t *len)
{
	static const char *copy[] = {"OS/2", "cvt ", "fpgm", "head", "hhea", "hmtx", "maxp", "name", "post", "prep"};
	u8 *keep = xcalloc((size_t)t->n + 1, 1);
	Buf glyf = {0}, loca = {0}, out = {0};
	Tab tab[16];
	int nt = 0;

	keep[0] = 1;
	for (int g = 0; g < t->n; g++)
		if (used[g])
			keep[g] = 1;
	for (int g = 0; g < t->n; g++)
		if (keep[g])
			closure(t, g, keep, 0);
	for (int g = 0; g < t->n; g++) {
		p32(&loca, (u32)glyf.n);
		if (keep[g]) {
			bput(&glyf, t->glyf + t->start[g], t->end[g] - t->start[g]);
			while (glyf.n % 4)
				bputc(&glyf, 0);
		}
	}
	p32(&loca, (u32)glyf.n);

	u8 *head = NULL;
	for (size_t i = 0; i < NELEM(copy); i++) {
		size_t n;
		const u8 *p = font_table(f, copy[i], &n);
		if (!p)
			continue;
		if (streq(copy[i], "head")) {
			if (n < 54)
				continue;
			head = xmalloc(n);
			memcpy(head, p, n);
			memset(head + 8, 0, 4);
			head[50] = 0;
			head[51] = 1;	/* long loca */
			p = head;
		}
		tab[nt].tag = copy[i];
		tab[nt].p = p;
		tab[nt].n = n;
		nt++;
	}
	tab[nt].tag = "glyf";
	tab[nt].p = (const u8 *)glyf.s;
	tab[nt].n = glyf.n;
	nt++;
	tab[nt].tag = "loca";
	tab[nt].p = (const u8 *)loca.s;
	tab[nt].n = loca.n;
	nt++;
	/* sort by tag (insertion sort, n is tiny) */
	for (int i = 1; i < nt; i++)
		for (int j = i; j > 0 && memcmp(tab[j].tag, tab[j - 1].tag, 4) < 0; j--) {
			Tab tmp = tab[j];
			tab[j] = tab[j - 1];
			tab[j - 1] = tmp;
		}
	int es = 0;
	while ((1 << (es + 1)) <= nt)
		es++;
	p32(&out, 0x00010000);
	p16(&out, (unsigned)nt);
	p16(&out, (unsigned)((1 << es) * 16));
	p16(&out, (unsigned)es);
	p16(&out, (unsigned)(nt * 16 - (1 << es) * 16));
	size_t off = 12 + 16 * (size_t)nt, headpos = 0;
	for (int i = 0; i < nt; i++) {
		bput(&out, tab[i].tag, 4);
		p32(&out, sfnt_checksum(tab[i].p, tab[i].n));
		p32(&out, (u32)off);
		p32(&out, (u32)tab[i].n);
		if (streq(tab[i].tag, "head"))
			headpos = off;
		off += (tab[i].n + 3) & ~(size_t)3;
	}
	for (int i = 0; i < nt; i++) {
		bput(&out, tab[i].p, tab[i].n);
		while (out.n % 4)
			bputc(&out, 0);
	}
	if (headpos) {
		u32 adj = 0xB1B0AFBAu - sfnt_checksum((u8 *)out.s, out.n);
		u8 *h = (u8 *)out.s + headpos + 8;
		h[0] = (u8)(adj >> 24);
		h[1] = (u8)(adj >> 16);
		h[2] = (u8)(adj >> 8);
		h[3] = (u8)adj;
	}
	*len = out.n;
	return (u8 *)out.s;
}
