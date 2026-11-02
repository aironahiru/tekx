/*
 * sha256.h -- SHA-256.
 */
#ifndef SHA256_H
#define SHA256_H

#include "tekx.h"

typedef struct {
	u32 h[8];
	u64 len;
	u8 buf[64];
	size_t n;
} Sha256;

void sha256_init(Sha256 *c);
void sha256_update(Sha256 *c, const void *data, size_t n);
void sha256_final(Sha256 *c, u8 out[32]);
char *sha256_hex(const u8 digest[32]);

#endif
