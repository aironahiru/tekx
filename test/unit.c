/*
 * unit.c -- unit tests for the libc-only building blocks.
 */
#include "../src/font.h"

#include <math.h>

static int failed;

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); failed++; } } while (0)

static void
test_utf8(void)
{
	Buf b = {0};
	u32 c;

	utf8_enc(&b, 0x1D465);	/* 𝑥 */
	CHECK(b.n == 4);
	CHECK(utf8_dec(b.s, b.n, &c) == 4 && c == 0x1D465);
	CHECK(utf8_len("a∑b") == 3);
	CHECK(edit_distance("nrom", "norm") == 2);
}

static void
test_bnum(void)
{
	Buf b = {0};

	bnum(&b, 1.5);
	bputc(&b, ' ');
	bnum(&b, 2.0);
	bputc(&b, ' ');
	bnum(&b, -0.0001);
	bputc(&b, ' ');
	bnum(&b, 0.12345);
	CHECK(streq(bstr(&b), "1.5 2 0 0.123"));
}

static void
test_deflate(void)
{
	Buf in = {0};
	size_t n, m;

	for (int i = 0; i < 400; i++)
		bprintf(&in, "BT /F1 12 Tf 72 %d Td (Hello, world %d) Tj ET\n", 700 - i, i % 7);
	u8 *z = zlib_compress((u8 *)in.s, in.n, &n);
	u8 *d = zlib_decompress(z, n, &m);
	CHECK(d != NULL && m == in.n && memcmp(d, in.s, m) == 0);
	CHECK(n < in.n / 8);
	z = zlib_compress((const u8 *)"", 0, &n);
	d = zlib_decompress(z, n, &m);
	CHECK(d != NULL && m == 0);
	/* pseudo-random data exercises dynamic blocks and long distances */
	u32 x = 12345;
	Buf r = {0};
	for (int i = 0; i < 100000; i++) {
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		bputc(&r, 'a' + (int)(x % 7));
	}
	z = zlib_compress((u8 *)r.s, r.n, &n);
	d = zlib_decompress(z, n, &m);
	CHECK(d != NULL && m == r.n && memcmp(d, r.s, m) == 0);
	/*
	 * A dynamic block whose code-length code has one code, "0", followed
	 * by the bit 1: not a code.  Found by fuzzing, where the failed
	 * decode (-1) was stored as a code length of 255 and indexed past a
	 * table.  A corrupt stream must be refused.
	 */
	static const u8 bad[] = {0x78, 0x01, 0x05, 0x00, 0x00, 0xE4, 0xFF, 0xFF};
	CHECK(zlib_decompress(bad, sizeof bad, &m) == NULL);
}

static void
test_md5(void)
{
	u8 h[16];
	char hex[33];

	md5((const u8 *)"The quick brown fox jumps over the lazy dog", 43, h);
	for (int i = 0; i < 16; i++)
		sprintf(hex + 2 * i, "%02x", h[i]);
	CHECK(streq(hex, "9e107d9d372bb6826bd81d3542a419d6"));
	md5((const u8 *)"", 0, h);
	for (int i = 0; i < 16; i++)
		sprintf(hex + 2 * i, "%02x", h[i]);
	CHECK(streq(hex, "d41d8cd98f00b204e9800998ecf8427e"));
}

static void
test_fonts(void)
{
	Font *m = font_builtin("STIXTwoMath-Regular.otf");
	Font *t = font_builtin("STIXTwoText-Regular.otf");

	CHECK(m->upem == 1000 && m->cff && m->hasmath);
	CHECK(m->mc[ScriptPercentScaleDown] == 70);
	CHECK(m->mc[AxisHeight] > 0);
	int x = font_glyph(m, 'x');
	BBox b = font_bbox(m, x);
	CHECK(b.y1 > 300 && b.y1 < 600);
	Construction c;
	CHECK(math_construction(m, font_glyph(m, '('), true, &c) && c.nvariants > 1 && c.nparts >= 3);
	CHECK(math_construction(m, font_glyph(m, 0x221A), true, &c) && c.nvariants > 1);
	int sum = font_glyph(m, 0x2211);
	CHECK(math_construction(m, sum, true, &c) && c.nvariants >= 2);
	CHECK(gsub_single(m, "ssty", font_glyph(m, 0x2032), 0) != 0);

	/* STIX joins f+i with contextual alternates */
	u16 g[2] = {(u16)font_glyph(t, 'f'), (u16)font_glyph(t, 'i')};
	int cl[2] = {1, 1};
	int n = gsub_apply(t, "liga", g, cl, 2);
	CHECK(n == 2 && (g[0] != font_glyph(t, 'f') || g[1] != font_glyph(t, 'i')));
	CHECK(font_kern(t, font_glyph(t, 'A'), font_glyph(t, 'V')) < 0);

	/* subset re-parses with identical glyph boxes */
	u8 *used = xcalloc((size_t)m->nglyphs, 1);
	used[x] = used[sum] = used[font_glyph(m, '(')] = 1;
	size_t len;
	int kind;
	u8 *prog = font_subset(m, used, "ABCDEF+STIXTwoMath-Regular", &len, &kind);
	char *err = NULL;
	Cff *s = cff_parse(prog, len, &err);
	CHECK(kind == PROGRAM_CFF && s != NULL);
	if (s) {
		BBox b2 = cff_bbox(s, x);
		CHECK(cff_nglyphs(s) == m->nglyphs);
		CHECK(fabs(b2.y1 - b.y1) < 1e-9 && fabs(b2.x0 - b.x0) < 1e-9);
	}
	CHECK(len < 120000);
	FILE *f = fopen("test/out/sub.cff", "wb");
	if (f) {
		fwrite(prog, 1, len, f);
		fclose(f);
	}
}

int
main(void)
{
	test_utf8();
	test_bnum();
	test_deflate();
	test_md5();
	test_fonts();
	if (failed) {
		fprintf(stderr, "unit: %d check(s) failed\n", failed);
		return 1;
	}
	printf("unit: ok\n");
	return 0;
}
