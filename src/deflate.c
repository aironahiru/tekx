/*
 * deflate.c -- zlib/DEFLATE (RFC 1950, 1951).
 *
 * Compression: hash-chain LZ77 with lazy matching; each block uses fixed
 * or dynamic Huffman codes, whichever is smaller.  Decompression is a
 * straightforward canonical-Huffman decoder.
 */
#include "tekx.h"

/* zlib's level-6 search: chains of 128, enough at 128 bytes, no lazy look past 16 */
enum { WSIZE = 32768, MINM = 3, MAXM = 258, HBITS = 15, CHAIN = 128, NICE = 128, LAZY = 16, BLOCK = 1 << 16 };
_Static_assert(WSIZE <= 32768 && MINM == 3 && MAXM == 258, "RFC 1951: a 32 KiB window, matches of 3 to 258");
_Static_assert(NICE <= MAXM && LAZY <= NICE && HBITS <= 24, "search parameters within the format");

static const u16 lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const u8 lextra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const u16 dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const u8 dextra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
static const u8 clorder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

typedef struct {
	u16 len;	/* 0 = literal */
	u16 val;	/* literal byte or distance */
} Tok;

typedef VEC(Tok) TokVec;

typedef struct {
	Buf out;
	u64 acc;
	int n;
} Bits;

static void
putbits(Bits *w, u32 v, int n)
{
	w->acc |= (u64)v << w->n;
	w->n += n;
	while (w->n >= 8) {
		bputc(&w->out, (int)(w->acc & 0xFF));
		w->acc >>= 8;
		w->n -= 8;
	}
}

static void
putcode(Bits *w, u32 code, int len)
{
	u32 rev = 0;

	for (int i = 0; i < len; i++)
		rev |= ((code >> i) & 1) << (len - 1 - i);
	putbits(w, rev, len);
}

static int
lcode(int len, u32 *extra, int *nextra)
{
	int i = 28;

	while (lbase[i] > len)
		i--;
	*extra = (u32)(len - lbase[i]);
	*nextra = lextra[i];
	return 257 + i;
}

static int
dcode(int d, u32 *extra, int *nextra)
{
	int i = 29;

	while (dbase[i] > d)
		i--;
	*extra = (u32)(d - dbase[i]);
	*nextra = dextra[i];
	return i;
}

static size_t
hash3(const u8 *d, size_t i)
{
	u32 v = (u32)d[i] << 10 ^ (u32)d[i + 1] << 5 ^ d[i + 2];

	return (v * 2654435761u) >> (32 - HBITS) & ((1u << HBITS) - 1);
}

static void
lz77(const u8 *d, size_t n, TokVec *toks)
{
	/*
	 * The hash heads live for the whole run and are cleared after each
	 * stream only where it touched them: a PDF holds a thousand small
	 * streams (each formula's MathML), and clearing 256 KB for each was
	 * most of the time spent writing a document.  The chains need no
	 * clearing: a slot is written before it is read.
	 */
	static size_t *head;
	const size_t NONE = (size_t)-1;
	if (head == NULL) {
		head = xmalloc(sizeof(size_t) << HBITS);
		for (size_t i = 0; i < (1u << HBITS); i++)
			head[i] = NONE;
	}
	size_t *prev = xmalloc(sizeof(size_t) * MIN(MAX(n, (size_t)1), (size_t)WSIZE));
#define INSERT(i) do { if ((i) + MINM <= n) { size_t h_ = hash3(d, i); prev[(i) % WSIZE] = head[h_]; head[h_] = (i); } } while (0)
	size_t i = 0;
	while (i < n) {
		size_t best = 0, dist = 0, cand, chain = 0, maxl;
		for (int pass = 0; pass < 2; pass++) {
			size_t at = i + (size_t)pass, bl = 0, bd = 0;
			if (at + MINM > n)
				break;
			maxl = MIN((size_t)MAXM, n - at);
			cand = head[hash3(d, at)];
			chain = 0;
			while (cand != NONE && chain < CHAIN && cand < at && at - cand < WSIZE) {
				size_t l = 0;
				while (l < maxl && d[cand + l] == d[at + l])
					l++;
				if (l > bl) {
					bl = l;
					bd = at - cand;
					if (l == maxl || l >= NICE)
						break;
				}
				size_t p = prev[cand % WSIZE];
				if (p == NONE || p >= cand)
					break;
				cand = p;
				chain++;
			}
			if (pass == 0) {
				best = bl;
				dist = bd;
				if (best < MINM)
					break;
				INSERT(i);
				if (best >= LAZY)
					break;	/* long enough: no lazy look at the next byte */
			} else if (bl > best + 1) {
				best = 0;	/* lazy: emit literal, take the longer match next */
			}
		}
		if (best >= MINM) {
			Tok t = {(u16)best, (u16)dist};
			VPUSH(*toks, t);
			for (size_t k = 1; k < best; k++)
				INSERT(i + k);
			i += best;
		} else {
			if (i + MINM <= n && (head[hash3(d, i)] != i))
				INSERT(i);
			Tok t = {0, d[i]};
			VPUSH(*toks, t);
			i++;
		}
	}
#undef INSERT
	for (size_t k = 0; k + MINM <= n; k++)
		head[hash3(d, k)] = NONE;
	free(prev);
}

/* Huffman code lengths limited to `limit` bits by frequency halving. */
static void
lengths(const u32 *freq, int n, int limit, u8 *len)
{
	u64 *f = xmalloc((size_t)n * sizeof *f);
	int *par = xmalloc((size_t)(2 * n + 1) * sizeof *par);
	u64 *w = xmalloc((size_t)(2 * n + 1) * sizeof *w);
	int *used = xmalloc((size_t)n * sizeof *used);

	for (int i = 0; i < n; i++)
		f[i] = freq[i];
	for (;;) {
		int nu = 0, maxd = 0;
		memset(len, 0, (size_t)n);
		for (int i = 0; i < n; i++)
			if (f[i])
				used[nu++] = i;
		if (nu == 0)
			break;
		if (nu == 1) {
			len[used[0]] = 1;
			break;
		}
		/* simple O(n^2) Huffman: n <= 288, so brute force is fine */
		int nodes = nu;
		bool *alive = xcalloc((size_t)(2 * nu), sizeof *alive);
		for (int k = 0; k < nu; k++) {
			w[k] = f[used[k]];
			par[k] = -1;
			alive[k] = true;
		}
		for (int m = 0; m < nu - 1; m++) {
			int a = -1, b = -1;
			for (int k = 0; k < nodes; k++) {
				if (!alive[k])
					continue;
				if (a < 0 || w[k] < w[a]) {
					b = a;
					a = k;
				} else if (b < 0 || w[k] < w[b]) {
					b = k;
				}
			}
			w[nodes] = w[a] + w[b];
			par[nodes] = -1;
			par[a] = par[b] = nodes;
			alive[a] = alive[b] = false;
			alive[nodes] = true;
			nodes++;
		}
		for (int k = 0; k < nu; k++) {
			int dd = 0, p = k;
			while (par[p] >= 0) {
				p = par[p];
				dd++;
			}
			len[used[k]] = (u8)dd;
			maxd = MAX(maxd, dd);
		}
		free(alive);
		if (maxd <= limit)
			break;
		for (int i = 0; i < n; i++)
			if (f[i])
				f[i] = MAX(f[i] >> 1, 1);
	}
	free(f);
	free(par);
	free(w);
	free(used);
}

static void
canon(const u8 *len, int n, u32 *code)
{
	int count[17] = {0};
	u32 next[17] = {0}, c = 0;

	for (int i = 0; i < n; i++)
		count[len[i]]++;
	count[0] = 0;
	for (int b = 1; b <= 16; b++) {
		c = (c + (u32)count[b - 1]) << 1;
		next[b] = c;
	}
	for (int i = 0; i < n; i++)
		code[i] = len[i] ? next[len[i]]++ : 0;
}

static void
fixed(u8 *ll, u8 *dl)
{
	for (int i = 0; i < 288; i++)
		ll[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
	for (int i = 0; i < 30; i++)
		dl[i] = 5;
}

static u64
cost(const Tok *t, size_t n, const u8 *ll, const u8 *dl)
{
	u64 bits = ll[256];
	u32 e;
	int ne;

	for (size_t i = 0; i < n; i++) {
		if (t[i].len == 0) {
			bits += ll[t[i].val];
		} else {
			int lc = lcode(t[i].len, &e, &ne);
			bits += ll[lc] + (u64)ne;
			int dc = dcode(t[i].val, &e, &ne);
			bits += dl[dc] + (u64)ne;
		}
	}
	return bits;
}

static void
emit(Bits *w, const Tok *t, size_t n, const u8 *ll, const u8 *dl)
{
	u32 lc[288], dc[30], e;
	int ne;

	canon(ll, 288, lc);
	canon(dl, 30, dc);
	for (size_t i = 0; i < n; i++) {
		if (t[i].len == 0) {
			putcode(w, lc[t[i].val], ll[t[i].val]);
		} else {
			int c = lcode(t[i].len, &e, &ne);
			putcode(w, lc[c], ll[c]);
			if (ne)
				putbits(w, e, ne);
			c = dcode(t[i].val, &e, &ne);
			putcode(w, dc[c], dl[c]);
			if (ne)
				putbits(w, e, ne);
		}
	}
	putcode(w, lc[256], ll[256]);
}

static void
block(Bits *w, const Tok *t, size_t n, bool last)
{
	u32 lf[288] = {0}, df[30] = {0}, cf[19] = {0}, e;
	u8 ll[288] = {0}, dl[30] = {0}, cl[19], fl[288], fd[30];
	int ne, hlit, hdist, hclen;

	lf[256] = 1;
	for (size_t i = 0; i < n; i++) {
		if (t[i].len == 0) {
			lf[t[i].val]++;
		} else {
			lf[lcode(t[i].len, &e, &ne)]++;
			df[dcode(t[i].val, &e, &ne)]++;
		}
	}
	lengths(lf, 286, 15, ll);
	lengths(df, 30, 15, dl);
	{
		int any = 0;
		for (int i = 0; i < 30; i++)
			any |= dl[i];
		if (!any)
			dl[0] = 1;	/* DEFLATE needs at least one distance code */
	}
	for (hlit = 286; hlit > 257 && ll[hlit - 1] == 0; hlit--)
		;
	for (hdist = 30; hdist > 1 && dl[hdist - 1] == 0; hdist--)
		;
	/* run-length encode the code lengths */
	u8 all[316], sym[316], ext[316];
	int na = 0, ns = 0;
	for (int i = 0; i < hlit; i++)
		all[na++] = ll[i];
	for (int i = 0; i < hdist; i++)
		all[na++] = dl[i];
	for (int i = 0; i < na;) {
		int l = all[i], run = 1;
		while (i + run < na && all[i + run] == l)
			run++;
		if (l == 0 && run >= 3) {
			int r = run;
			while (r >= 3) {
				if (r >= 11) {
					int k = MIN(r, 138);
					sym[ns] = 18;
					ext[ns++] = (u8)(k - 11);
					r -= k;
				} else {
					int k = MIN(r, 10);
					sym[ns] = 17;
					ext[ns++] = (u8)(k - 3);
					r -= k;
				}
			}
			while (r-- > 0) {
				sym[ns] = 0;
				ext[ns++] = 0;
			}
		} else if (l != 0 && run >= 4) {
			sym[ns] = (u8)l;
			ext[ns++] = 0;
			int r = run - 1;
			while (r >= 3) {
				int k = MIN(r, 6);
				sym[ns] = 16;
				ext[ns++] = (u8)(k - 3);
				r -= k;
			}
			while (r-- > 0) {
				sym[ns] = (u8)l;
				ext[ns++] = 0;
			}
		} else {
			sym[ns] = (u8)l;
			ext[ns++] = 0;
			run = 1;
		}
		i += run;
	}
	for (int i = 0; i < ns; i++)
		cf[sym[i]]++;
	lengths(cf, 19, 7, cl);
	for (hclen = 19; hclen > 4 && cl[clorder[hclen - 1]] == 0; hclen--)
		;
	u64 dbits = 3 + 5 + 5 + 4 + 3 * (u64)hclen;
	for (int i = 0; i < ns; i++)
		dbits += cl[sym[i]] + (sym[i] == 16 ? 2u : sym[i] == 17 ? 3u : sym[i] == 18 ? 7u : 0u);
	dbits += cost(t, n, ll, dl);
	fixed(fl, fd);
	u64 fbits = 3 + cost(t, n, fl, fd);

	putbits(w, last, 1);
	if (fbits <= dbits) {
		putbits(w, 1, 2);
		emit(w, t, n, fl, fd);
		return;
	}
	putbits(w, 2, 2);
	putbits(w, (u32)(hlit - 257), 5);
	putbits(w, (u32)(hdist - 1), 5);
	putbits(w, (u32)(hclen - 4), 4);
	for (int i = 0; i < hclen; i++)
		putbits(w, cl[clorder[i]], 3);
	u32 cc[19];
	canon(cl, 19, cc);
	for (int i = 0; i < ns; i++) {
		putcode(w, cc[sym[i]], cl[sym[i]]);
		if (sym[i] == 16)
			putbits(w, ext[i], 2);
		else if (sym[i] == 17)
			putbits(w, ext[i], 3);
		else if (sym[i] == 18)
			putbits(w, ext[i], 7);
	}
	emit(w, t, n, ll, dl);
}

static u32
adler32(const u8 *d, size_t n)
{
	u32 a = 1, b = 0;

	while (n > 0) {
		size_t k = MIN(n, (size_t)5552);
		n -= k;
		while (k--) {
			a += *d++;
			b += a;
		}
		a %= 65521;
		b %= 65521;
	}
	return b << 16 | a;
}

u8 *
zlib_compress(const u8 *d, size_t n, size_t *outlen)
{
	TokVec toks = {0};
	Bits w = {{0}, 0, 0};

	lz77(d, n, &toks);
	bputc(&w.out, 0x78);
	bputc(&w.out, 0xDA);
	if (toks.n == 0) {
		block(&w, NULL, 0, true);
	} else {
		for (size_t i = 0; i < toks.n; i += BLOCK)
			block(&w, toks.v + i, MIN((size_t)BLOCK, toks.n - i), i + BLOCK >= toks.n);
	}
	if (w.n > 0)
		bputc(&w.out, (int)(w.acc & 0xFF));
	u32 a = adler32(d, n);
	bputc(&w.out, (int)(a >> 24));
	bputc(&w.out, (int)(a >> 16 & 0xFF));
	bputc(&w.out, (int)(a >> 8 & 0xFF));
	bputc(&w.out, (int)(a & 0xFF));
	free(toks.v);
	*outlen = w.out.n;
	return (u8 *)w.out.s;
}

/* ---- inflate ---- */

typedef struct {
	const u8 *d;
	size_t n, pos;
	int bit, err;
} BitIn;

static int
getbit(BitIn *r)
{
	if (r->pos >= r->n) {
		r->err = 1;
		return 0;
	}
	int v = (r->d[r->pos] >> r->bit) & 1;
	if (++r->bit == 8) {
		r->bit = 0;
		r->pos++;
	}
	return v;
}

static u32
getbits(BitIn *r, int n)
{
	u32 v = 0;

	for (int i = 0; i < n; i++)
		v |= (u32)getbit(r) << i;
	return v;
}

typedef struct {
	u16 count[16], sym[320];
} Huff;

static void
huff_build(Huff *h, const u8 *len, int n)
{
	u16 offs[16];

	memset(h->count, 0, sizeof h->count);
	for (int i = 0; i < n; i++)
		h->count[len[i]]++;
	h->count[0] = 0;
	offs[1] = 0;
	for (int i = 1; i < 15; i++)
		offs[i + 1] = (u16)(offs[i] + h->count[i]);
	for (int i = 0; i < n; i++)
		if (len[i])
			h->sym[offs[len[i]]++] = (u16)i;
}

static int
huff_decode(BitIn *r, Huff *h)
{
	int code = 0, first = 0, index = 0;

	for (int len = 1; len < 16; len++) {
		code |= getbit(r);
		int count = h->count[len];
		if (code - count < first)
			return h->sym[index + (code - first)];
		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
	}
	r->err = 1;
	return -1;
}

u8 *
zlib_decompress(const u8 *d, size_t n, size_t *outlen)
{
	BitIn r = {d, n, 2, 0, 0};
	Buf out = {0};
	int last;

	if (n < 6 || (d[0] & 0x0F) != 8 || ((unsigned)d[0] * 256 + d[1]) % 31)
		return NULL;
	do {
		last = getbit(&r);
		int type = (int)getbits(&r, 2);
		if (type == 0) {
			if (r.bit) {
				r.bit = 0;
				r.pos++;
			}
			if (r.pos + 4 > n)
				return NULL;
			size_t len = (size_t)d[r.pos] | (size_t)d[r.pos + 1] << 8;
			r.pos += 4;
			if (r.pos + len > n)
				return NULL;
			bput(&out, d + r.pos, len);
			r.pos += len;
		} else if (type == 1 || type == 2) {
			Huff lit, dist;
			u8 ll[320] = {0}, dl[32] = {0};
			if (type == 1) {
				u8 fl[288], fd[30];
				fixed(fl, fd);
				huff_build(&lit, fl, 288);
				huff_build(&dist, fd, 30);
			} else {
				int hlit = (int)getbits(&r, 5) + 257, hdist = (int)getbits(&r, 5) + 1;
				int hclen = (int)getbits(&r, 4) + 4;
				u8 cl[19] = {0}, lens[320];
				Huff ch;
				for (int i = 0; i < hclen; i++)
					cl[clorder[i]] = (u8)getbits(&r, 3);
				huff_build(&ch, cl, 19);
				int k = 0;
				while (k < hlit + hdist && !r.err) {
					int s = huff_decode(&r, &ch), rep = 0, val = 0;
					if (s < 0)
						return NULL;	/* not a code: a corrupt stream */
					if (s < 16) {
						lens[k++] = (u8)s;
						continue;
					}
					if (s == 16) {
						if (k == 0)
							return NULL;
						val = lens[k - 1];
						rep = 3 + (int)getbits(&r, 2);
					} else if (s == 17) {
						rep = 3 + (int)getbits(&r, 3);
					} else {
						rep = 11 + (int)getbits(&r, 7);
					}
					while (rep-- > 0 && k < hlit + hdist)
						lens[k++] = (u8)val;
				}
				memcpy(ll, lens, (size_t)hlit);
				memcpy(dl, lens + hlit, (size_t)hdist);
				huff_build(&lit, ll, hlit);
				huff_build(&dist, dl, hdist);
			}
			for (;;) {
				int s = huff_decode(&r, &lit);
				if (r.err || s < 0)
					return NULL;
				if (s < 256) {
					bputc(&out, s);
				} else if (s == 256) {
					break;
				} else {
					s -= 257;
					if (s >= 29)
						return NULL;
					size_t len = lbase[s] + getbits(&r, lextra[s]);
					int dc = huff_decode(&r, &dist);
					if (dc < 0 || dc >= 30)
						return NULL;
					size_t dd = dbase[dc] + getbits(&r, dextra[dc]);
					if (dd > out.n)
						return NULL;
					for (size_t q = 0; q < len; q++)
						bputc(&out, out.s[out.n - dd]);
				}
			}
		} else {
			return NULL;
		}
		if (r.err)
			return NULL;
	} while (!last);
	*outlen = out.n;
	return (u8 *)bstr(&out);
}
