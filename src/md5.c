/*
 * md5.c -- MD5 (RFC 1321), used to derive file identifiers and subset tags.
 */
#include "tekx.h"

#include <math.h>

static const u8 S[64] = {
	7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
	5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
	4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
	6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

static u32
rotl(u32 x, int c)
{
	return x << c | x >> (32 - c);
}

void
md5(const u8 *data, size_t n, u8 out[16])
{
	u32 K[64], a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
	size_t total = ((n + 8) / 64 + 1) * 64;
	u8 *msg = xcalloc(total, 1);
	u64 bits = (u64)n * 8;

	for (int i = 0; i < 64; i++)
		K[i] = (u32)(fabs(sin(i + 1.0)) * 4294967296.0);
	memcpy(msg, data, n);
	msg[n] = 0x80;
	for (int i = 0; i < 8; i++)
		msg[total - 8 + (size_t)i] = (u8)(bits >> (8 * i));
	for (size_t off = 0; off < total; off += 64) {
		u32 M[16], A = a0, B = b0, C = c0, D = d0;
		for (int i = 0; i < 16; i++)
			M[i] = (u32)msg[off + 4 * (size_t)i] | (u32)msg[off + 4 * (size_t)i + 1] << 8 |
			    (u32)msg[off + 4 * (size_t)i + 2] << 16 | (u32)msg[off + 4 * (size_t)i + 3] << 24;
		for (int i = 0; i < 64; i++) {
			u32 F;
			int g;
			switch (i / 16) {
			case 0: F = (B & C) | (~B & D); g = i; break;
			case 1: F = (D & B) | (~D & C); g = (5 * i + 1) % 16; break;
			case 2: F = B ^ C ^ D; g = (3 * i + 5) % 16; break;
			default: F = C ^ (B | ~D); g = (7 * i) % 16; break;
			}
			F += A + K[i] + M[g];
			A = D;
			D = C;
			C = B;
			B += rotl(F, S[i]);
		}
		a0 += A;
		b0 += B;
		c0 += C;
		d0 += D;
	}
	u32 r[4] = {a0, b0, c0, d0};
	for (int i = 0; i < 4; i++)
		for (int k = 0; k < 4; k++)
			out[4 * i + k] = (u8)(r[i] >> (8 * k));
	free(msg);
}
