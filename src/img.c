/*
 * img.c -- images for figures: PNG (decoded to samples) and JPEG (passed
 * through to the PDF's DCTDecode filter untouched).
 */
#include "tekx.h"
#include "img.h"

#include <errno.h>

static u32
be32(const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static int
paeth(int a, int b, int c)
{
	int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);

	return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

static Image *
png(const u8 *d, size_t n, char **err)
{
	Buf idat = {0};
	u32 w = 0, h = 0;
	int depth = 0, ctype = -1, interlace = 0;
	const u8 *plte = NULL;
	size_t nplte = 0;

	for (size_t pos = 8; pos + 12 <= n;) {
		u32 len = be32(d + pos);
		const u8 *type = d + pos + 4, *body = d + pos + 8;
		if (len > n - pos - 12) {
			*err = "truncated PNG";
			return NULL;
		}
		if (memcmp(type, "IHDR", 4) == 0 && len >= 13) {
			w = be32(body);
			h = be32(body + 4);
			depth = body[8];
			ctype = body[9];
			interlace = body[12];
		} else if (memcmp(type, "PLTE", 4) == 0) {
			plte = body;
			nplte = len / 3;
		} else if (memcmp(type, "IDAT", 4) == 0) {
			bput(&idat, body, len);
		} else if (memcmp(type, "IEND", 4) == 0) {
			break;
		}
		pos += 12 + len;
	}
	if (w == 0 || h == 0 || w > 30000 || h > 30000) {
		*err = "PNG without a valid size";
		return NULL;
	}
	if (interlace) {
		*err = "interlaced PNG is not supported; save it without interlacing";
		return NULL;
	}
	int ch = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
	if (ch == 0 || !(depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16) ||
	    (depth < 8 && ctype != 0 && ctype != 3) || (ctype == 3 && !plte)) {
		*err = "unsupported PNG colour type or depth";
		return NULL;
	}
	size_t rawlen;
	u8 *raw = zlib_decompress((u8 *)idat.s, idat.n, &rawlen);
	size_t bpp = (size_t)(ch * depth + 7) / 8;	/* bytes per complete pixel, at least 1 */
	size_t stride = ((size_t)w * (size_t)ch * (size_t)depth + 7) / 8;
	if (raw == NULL || rawlen < (stride + 1) * h) {
		*err = "corrupt PNG image data";
		return NULL;
	}
	/* undo the per-row filters in place */
	u8 *prev = xcalloc(stride, 1);
	u8 *px = xmalloc(stride * h);
	for (u32 y = 0; y < h; y++) {
		u8 *src = raw + y * (stride + 1), *row = px + y * stride;
		int f = src[0];
		for (size_t x = 0; x < stride; x++) {
			int a = x >= bpp ? row[x - bpp] : 0, b = prev[x], c = x >= bpp ? prev[x - bpp] : 0;
			int v = src[1 + x];
			switch (f) {
			case 1: v += a; break;
			case 2: v += b; break;
			case 3: v += (a + b) / 2; break;
			case 4: v += paeth(a, b, c); break;
			}
			row[x] = (u8)v;
		}
		memcpy(prev, row, stride);
	}
	/* to 8-bit samples: colour (gray or RGB) and, if any, alpha */
	Image *im = NEW(Image);
	bool alpha = ctype == 4 || ctype == 6;
	int cc = ctype == 2 || ctype == 3 || ctype == 6 ? 3 : 1;
	im->w = (int)w;
	im->h = (int)h;
	im->comps = cc;
	im->len = (size_t)w * h * (size_t)cc;
	im->data = xmalloc(im->len);
	if (alpha) {
		im->alen = (size_t)w * h;
		im->alpha = xmalloc(im->alen);
	}
	for (u32 y = 0; y < h; y++) {
		const u8 *row = px + y * stride;
		for (u32 x = 0; x < w; x++) {
			size_t o = ((size_t)y * w + x);
			if (depth < 8) {
				int per = 8 / depth;
				int v = row[x / (u32)per] >> (8 - depth * (int)(x % (u32)per + 1)) & ((1 << depth) - 1);
				if (ctype == 3) {
					int i = v < (int)nplte ? v : 0;
					memcpy(im->data + o * 3, plte + i * 3, 3);
				} else {
					im->data[o] = (u8)(v * 255 / ((1 << depth) - 1));
				}
				continue;
			}
			int step = depth / 8;	/* 16-bit samples keep their high byte */
			const u8 *p = row + (size_t)x * (size_t)ch * (size_t)step;
			if (ctype == 3) {
				int i = p[0] < nplte ? p[0] : 0;
				memcpy(im->data + o * 3, plte + i * 3, 3);
			} else {
				for (int k = 0; k < cc; k++)
					im->data[o * (size_t)cc + (size_t)k] = p[k * step];
				if (alpha)
					im->alpha[o] = p[cc * step];
			}
		}
	}
	free(raw);
	free(prev);
	free(px);
	return im;
}

static Image *
jpeg(const u8 *d, size_t n, char **err)
{
	for (size_t pos = 2; pos + 4 <= n;) {
		if (d[pos] != 0xFF) {
			pos++;
			continue;
		}
		int m = d[pos + 1];
		if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7) || m == 0xFF) {
			pos += m == 0xFF ? 1 : 2;
			continue;
		}
		size_t len = (size_t)d[pos + 2] << 8 | d[pos + 3];
		if ((m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) && pos + 9 < n) {
			Image *im = NEW(Image);
			im->h = d[pos + 5] << 8 | d[pos + 6];
			im->w = d[pos + 7] << 8 | d[pos + 8];
			im->comps = d[pos + 9];
			im->jpeg = true;
			im->data = (u8 *)d;
			im->len = n;
			if (im->comps != 1 && im->comps != 3) {
				*err = "only grayscale and RGB JPEG images are supported";
				return NULL;
			}
			return im;
		}
		pos += 2 + len;
	}
	*err = "JPEG without a frame header";
	return NULL;
}

Image *
image_load(const char *path, char **err)
{
	FILE *f = fopen(path, "rb");
	size_t n;

	if (f == NULL) {
		*err = fmt("cannot read %s: %s", path, strerror(errno));
		return NULL;
	}
	u8 *d = (u8 *)read_all(f, &n);
	fclose(f);
	if (n >= 8 && memcmp(d, "\x89PNG\r\n\x1a\n", 8) == 0)
		return png(d, n, err);
	if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF)
		return jpeg(d, n, err);
	*err = fmt("%s is neither a PNG nor a JPEG image", path);
	return NULL;
}
