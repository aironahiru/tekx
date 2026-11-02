/*
 * mutate.c -- a small deterministic mutator for fuzzing tekx.
 *
 *	mutate SEED [text|binary] <input >output
 *
 * Applies one to eight mutations chosen by SEED: flipping a byte, deleting
 * or duplicating a run, or (for text) splicing in a fragment of tekx
 * syntax that tends to reach the parser's corners.  The same seed always
 * gives the same output, so a failing case can be reproduced and kept.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long long state;

static unsigned
rnd(unsigned n)
{
	state = state * 6364136223846793005ULL + 1442695040888963407ULL;
	return n ? (unsigned)((state >> 33) % n) : 0;
}

static const char *fragments[] = {
	"$", "$$", "^", "_", "(", ")", "[", "]", "{", "}", "|", "||", "&", "\\\\", "\\", "/", "'", "\"",
	"\\frac{", "\\sqrt[", "\\begin{pmatrix}", "\\end{pmatrix}", "\\begin{cases}", "\\text{", "\\left(",
	"sum ", "integral of ", " dx ", "limit of ", " as x approaches ", " for k from 1 to ", " over ",
	"cases\n      ", "matrix\n      ", "\n    ", "\n      ", "\n", "\n\n", "  ", "\t",
	"#", "#lab", "@lab", "@", "^[", "*", "**", "`", "```\n", "> ", "- ", "1. ", "| a | b |\n",
	"theorem\n  ", "proof\n  ", "section ", "figure \"x.png\"\n  alt \"a\"\n", "do\n", "end\n",
	"define block m(a = \"x\")\n  {a} {body}\n", "{m}", "define f(x) as x^2\n", "define x like theorem\n",
	"let f: RR -> RR be ", "references\n  k: ", "contents\n", "newpage\n", "\xc3\xa9", "\xe2\x88\x91",
	"\xf0\x9d\x91\xa5", "\xff", "\x01", "0", "999999999999", "-", "+-", "<=>", "...",
};

int
main(int argc, char **argv)
{
	size_t cap = 1 << 16, n = 0;
	char *buf = malloc(cap);
	int c, text = argc < 3 || strcmp(argv[2], "binary") != 0;

	if (argc < 2 || !buf)
		return 2;
	state = strtoull(argv[1], NULL, 10) * 2862933555777941757ULL + 3037000493ULL;
	while ((c = getchar()) != EOF) {
		if (n + 1 >= cap && !(buf = realloc(buf, cap *= 2)))
			return 2;
		buf[n++] = (char)c;
	}
	int rounds = 1 + (int)rnd(8);
	for (int r = 0; r < rounds && n > 0; r++) {
		size_t at = rnd((unsigned)n), len = 1 + rnd(16);
		if (at + len > n)
			len = n - at;
		switch (rnd(text ? 5 : 3)) {
		case 0:	/* flip a byte */
			buf[at] ^= (char)(1 << rnd(8));
			break;
		case 1:	/* delete a run */
			memmove(buf + at, buf + at + len, n - at - len);
			n -= len;
			break;
		case 2:	/* duplicate a run */
			if (n + len + 1 >= cap && !(buf = realloc(buf, cap = 2 * (n + len + 1))))
				return 2;
			memmove(buf + at + len, buf + at, n - at);
			n += len;
			break;
		default: {	/* splice in a fragment of syntax */
			const char *f = fragments[rnd(sizeof fragments / sizeof *fragments)];
			size_t fl = strlen(f);
			if (n + fl + 1 >= cap && !(buf = realloc(buf, cap = 2 * (n + fl + 1))))
				return 2;
			memmove(buf + at + fl, buf + at, n - at);
			memcpy(buf + at, f, fl);
			n += fl;
		}
		}
	}
	fwrite(buf, 1, n, stdout);
	return 0;
}
