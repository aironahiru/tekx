/*
 * hyph.c -- Liang's hyphenation algorithm with the US English patterns
 * (ushyphmax, Gerard D.C. Kuiken) and exception list, both embedded.
 */
#include "tekx.h"

typedef struct {
	char *letters;	/* pattern letters, e.g. ".ach" */
	char *digits;	/* strlen(letters)+1 priorities */
} Pattern;

static Pattern *pats;
static size_t npats;
static VEC(char *) exceptions;	/* hyphenated forms, e.g. "as-so-ciate" */

static int
cmp_pat(const void *a, const void *b)
{
	return strcmp(((const Pattern *)a)->letters, ((const Pattern *)b)->letters);
}

void
hyph_init(void)
{
	const Asset *a = asset("hyph-en-us.pat.txt"), *e = asset("hyph-en-us.hyp.txt");
	VEC(Pattern) v = {0};

	if (pats || a == NULL)
		return;
	const char *s = (const char *)a->data, *end = s + a->len;
	while (s < end) {
		const char *l = s;
		while (s < end && *s != '\n')
			s++;
		Pattern p;
		Buf lt = {0}, dg = {0};
		int pending = 0;
		for (const char *q = l; q < s; q++) {
			if (*q >= '0' && *q <= '9') {
				pending = *q - '0';
			} else if (*q != ' ' && *q != '\r') {
				bputc(&dg, '0' + pending);
				bputc(&lt, *q);
				pending = 0;
			}
		}
		bputc(&dg, '0' + pending);
		if (lt.n) {
			p.letters = bstr(&lt);
			p.digits = bstr(&dg);
			VPUSH(v, p);
		}
		s++;
	}
	sort(v.v, v.n, sizeof *v.v, cmp_pat);
	pats = v.v;
	npats = v.n;
	if (e) {
		const char *t = (const char *)e->data, *te = t + e->len;
		while (t < te) {
			const char *l = t;
			while (t < te && *t != '\n')
				t++;
			if (t > l)
				VPUSH(exceptions, xstrndup(l, (size_t)(t - l)));
			t++;
		}
	}
}

static const Pattern *
find(const char *s, size_t n)
{
	size_t lo = 0, hi = npats;

	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		int c = strncmp(pats[mid].letters, s, n);
		if (c == 0 && pats[mid].letters[n] != 0)
			c = 1;
		if (c < 0)
			lo = mid + 1;
		else if (c > 0)
			hi = mid;
		else
			return &pats[mid];
	}
	return NULL;
}

/*
 * Compute hyphenation points of a lowercase ASCII word.  points[i] = 1 when
 * a hyphen may be inserted before byte i.  Returns the number of points.
 * Minimum fragments: 2 letters before, 3 after (en-US).
 */
int
hyph_points(const char *word, unsigned char *points)
{
	size_t n = strlen(word), i, j;
	int count = 0;

	memset(points, 0, n + 1);
	if (n < 5 || pats == NULL)
		return 0;
	for (i = 0; i < n; i++)
		if (word[i] < 'a' || word[i] > 'z')
			return 0;
	for (size_t k = 0; k < exceptions.n; k++) {
		const char *x = exceptions.v[k];
		size_t xi = 0, wi = 0;
		while (x[xi] && wi <= n) {
			if (x[xi] == '-') {
				xi++;
				continue;
			}
			if (wi >= n || x[xi] != word[wi])
				break;
			xi++;
			wi++;
		}
		if (x[xi] == 0 && wi == n) {
			for (xi = 0, wi = 0; x[xi]; xi++) {
				if (x[xi] == '-')
					points[wi] = 1, count++;
				else
					wi++;
			}
			return count;
		}
	}
	char *w = xmalloc(n + 3);
	unsigned char *v = xcalloc(n + 3, 1);
	w[0] = '.';
	memcpy(w + 1, word, n);
	w[n + 1] = '.';
	w[n + 2] = 0;
	for (i = 0; i < n + 2; i++) {
		for (j = 1; i + j <= n + 2; j++) {
			const Pattern *p = find(w + i, j);
			if (p == NULL)
				continue;
			for (size_t k = 0; k <= j; k++) {
				unsigned char d = (unsigned char)(p->digits[k] - '0');
				if (d > v[i + k])
					v[i + k] = d;
			}
		}
	}
	/* v[k] is the priority between w[k-1] and w[k]; word byte b is w[b+1] */
	for (i = 2; i + 3 <= n; i++) {
		if (v[i + 1] % 2 == 1) {
			points[i] = 1;
			count++;
		}
	}
	free(w);
	free(v);
	return count;
}
