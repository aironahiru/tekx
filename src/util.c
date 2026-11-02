/*
 * util.c -- memory, buffers, UTF-8, diagnostics, I/O.
 */
#include "tekx.h"

#include <errno.h>
#include <sys/types.h>
#include <time.h>

const char *diag_file = "<stdin>";
int nerrors;

void *
xmalloc(size_t n)
{
	void *p = malloc(n ? n : 1);

	if (p == NULL)
		die("out of memory");
	return p;
}

void *
xcalloc(size_t n, size_t sz)
{
	void *p = calloc(n ? n : 1, sz ? sz : 1);

	if (p == NULL)
		die("out of memory");
	return p;
}

void *
xrealloc(void *p, size_t n)
{
	p = realloc(p, n ? n : 1);
	if (p == NULL)
		die("out of memory");
	return p;
}

char *
xstrdup(const char *s)
{
	return xstrndup(s, strlen(s));
}

char *
xstrndup(const char *s, size_t n)
{
	char *p = xmalloc(n + 1);

	memcpy(p, s, n);
	p[n] = 0;
	return p;
}

void
bput(Buf *b, const void *p, size_t n)
{
	if (b->n + n + 1 > b->cap) {
		b->cap = MAX(2 * b->cap, b->n + n + 64);
		b->s = xrealloc(b->s, b->cap);
	}
	memcpy(b->s + b->n, p, n);
	b->n += n;
	b->s[b->n] = 0;
}

void
bputs(Buf *b, const char *s)
{
	bput(b, s, strlen(s));
}

void
bputc(Buf *b, int c)
{
	char ch = (char)c;

	bput(b, &ch, 1);
}

void
bprintf(Buf *b, const char *f, ...)
{
	va_list ap;
	char tmp[256];
	int n;

	if (f == NULL)
		return;
	va_start(ap, f);
	n = vsnprintf(tmp, sizeof tmp, f, ap);
	va_end(ap);
	if (n < 0)
		return;
	if ((size_t)n < sizeof tmp) {
		bput(b, tmp, (size_t)n);
		return;
	}
	char *big = xmalloc((size_t)n + 1);
	va_start(ap, f);
	vsnprintf(big, (size_t)n + 1, f, ap);
	va_end(ap);
	bput(b, big, (size_t)n);
	free(big);
}

void
bnum(Buf *b, double v)
{
	char tmp[64];
	int n;

	if (v > -0.0005 && v < 0.0005)
		v = 0;
	n = snprintf(tmp, sizeof tmp, "%.3f", v);
	while (n > 0 && tmp[n - 1] == '0')
		tmp[--n] = 0;
	if (n > 0 && tmp[n - 1] == '.')
		tmp[--n] = 0;
	if (streq(tmp, "-0"))
		strcpy(tmp, "0");
	bputs(b, tmp);
}

char *
bstr(Buf *b)
{
	if (b->s == NULL)
		bput(b, "", 0);
	return b->s;
}

void
breset(Buf *b)
{
	b->n = 0;
	if (b->s)
		b->s[0] = 0;
}

int
utf8_dec(const char *s, size_t n, u32 *cp)
{
	const u8 *u = (const u8 *)s;
	u32 c;
	int len, i;

	if (n == 0) {
		*cp = 0;
		return 1;
	}
	c = u[0];
	if (c < 0x80) {
		*cp = c;
		return 1;
	}
	if ((c & 0xE0) == 0xC0) {
		len = 2;
		c &= 0x1F;
	} else if ((c & 0xF0) == 0xE0) {
		len = 3;
		c &= 0x0F;
	} else if ((c & 0xF8) == 0xF0) {
		len = 4;
		c &= 0x07;
	} else {
		*cp = 0xFFFD;
		return 1;
	}
	if ((size_t)len > n) {
		*cp = 0xFFFD;
		return 1;
	}
	for (i = 1; i < len; i++) {
		if ((u[i] & 0xC0) != 0x80) {
			*cp = 0xFFFD;
			return 1;
		}
		c = (c << 6) | (u[i] & 0x3F);
	}
	*cp = c;
	return len;
}

void
utf8_enc(Buf *b, u32 c)
{
	char t[4];

	if (c < 0x80) {
		bputc(b, (int)c);
	} else if (c < 0x800) {
		t[0] = (char)(0xC0 | (c >> 6));
		t[1] = (char)(0x80 | (c & 0x3F));
		bput(b, t, 2);
	} else if (c < 0x10000) {
		t[0] = (char)(0xE0 | (c >> 12));
		t[1] = (char)(0x80 | ((c >> 6) & 0x3F));
		t[2] = (char)(0x80 | (c & 0x3F));
		bput(b, t, 3);
	} else {
		t[0] = (char)(0xF0 | (c >> 18));
		t[1] = (char)(0x80 | ((c >> 12) & 0x3F));
		t[2] = (char)(0x80 | ((c >> 6) & 0x3F));
		t[3] = (char)(0x80 | (c & 0x3F));
		bput(b, t, 4);
	}
}

size_t
utf8_len(const char *s)
{
	size_t n = 0, len = strlen(s), i = 0;
	u32 c;

	while (i < len) {
		i += (size_t)utf8_dec(s + i, len - i, &c);
		n++;
	}
	return n;
}

u32
utf8_first(const char *s)
{
	u32 c;

	utf8_dec(s, strlen(s), &c);
	return c;
}

bool
streq(const char *a, const char *b)
{
	return strcmp(a, b) == 0;
}

bool
startswith(const char *s, const char *p)
{
	return strncmp(s, p, strlen(p)) == 0;
}

char *
trim(char *s)
{
	char *e;

	while (*s == ' ' || *s == '\t')
		s++;
	e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
		*--e = 0;
	return s;
}

char *
fmt(const char *f, ...)
{
	va_list ap;
	int n;
	char *p;

	char probe[1];

	if (f == NULL)
		f = "";
	va_start(ap, f);
	n = vsnprintf(probe, sizeof probe, f, ap);
	va_end(ap);
	p = xmalloc((size_t)n + 1);
	va_start(ap, f);
	vsnprintf(p, (size_t)n + 1, f, ap);
	va_end(ap);
	return p;
}

int
edit_distance(const char *a, const char *b)
{
	size_t la = strlen(a), lb = strlen(b), i, j;
	int *prev, *cur, d;

	prev = xmalloc((lb + 1) * sizeof *prev);
	cur = xmalloc((lb + 1) * sizeof *cur);
	for (j = 0; j <= lb; j++)
		prev[j] = (int)j;
	for (i = 1; i <= la; i++) {
		cur[0] = (int)i;
		for (j = 1; j <= lb; j++) {
			int cost = a[i - 1] != b[j - 1];
			cur[j] = MIN(MIN(prev[j] + 1, cur[j - 1] + 1), prev[j - 1] + cost);
		}
		memcpy(prev, cur, (lb + 1) * sizeof *prev);
	}
	d = prev[lb];
	free(prev);
	free(cur);
	return d;
}

void
pct_escape(Buf *b, const char *s)
{
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		if (c <= ' ' || c == '%' || c == ':' || c == 0x7F)
			bprintf(b, "%%%02X", c);
		else
			bputc(b, c);
	}
}

static int
hexval(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}

char *
pct_unescape(const char *s)
{
	Buf b = {0};

	for (; *s; s++) {
		int h, l;
		if (*s == '%' && (h = hexval(s[1])) >= 0 && (l = hexval(s[2])) >= 0) {
			bputc(&b, h * 16 + l);
			s += 2;
		} else {
			bputc(&b, *s);
		}
	}
	return bstr(&b);
}

char *
next_field(char **line)
{
	char *s = *line, *start;

	while (*s == ' ')
		s++;
	start = s;
	while (*s && *s != ' ')
		s++;
	if (*s)
		*s++ = 0;
	*line = s;
	return start;
}

static void
vdiag(const char *kind, int line, int col, const char *f, va_list ap)
{
	fflush(stdout);
	if (line > 0)
		fprintf(stderr, "%s:%d:%d: %s: ", diag_file, line, col > 0 ? col : 1, kind);
	else
		fprintf(stderr, "%s: %s: ", diag_file, kind);
	vfprintf(stderr, f, ap);
	fputc('\n', stderr);
}

void
error_at(int line, int col, const char *f, ...)
{
	va_list ap;

	va_start(ap, f);
	vdiag("error", line, col, f, ap);
	va_end(ap);
	nerrors++;
}

void
warn_at(int line, int col, const char *f, ...)
{
	va_list ap;

	va_start(ap, f);
	vdiag("warning", line, col, f, ap);
	va_end(ap);
}

void
note(const char *f, ...)
{
	va_list ap;

	va_start(ap, f);
	fprintf(stderr, "  note: ");
	vfprintf(stderr, f, ap);
	fputc('\n', stderr);
	va_end(ap);
}

_Noreturn void
die(const char *f, ...)
{
	va_list ap;

	fflush(stdout);
	fprintf(stderr, "tekx: ");
	va_start(ap, f);
	vfprintf(stderr, f, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(2);
}

char *
read_all(FILE *f, size_t *len)
{
	Buf b = {0};
	char tmp[65536];
	size_t n;

	while ((n = fread(tmp, 1, sizeof tmp, f)) > 0)
		bput(&b, tmp, n);
	if (len)
		*len = b.n;
	return bstr(&b);
}

char *
read_file(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	char *s;

	if (f == NULL)
		die("cannot open %s: %s", path, strerror(errno));
	s = read_all(f, len);
	fclose(f);
	return s;
}

char *
read_line(FILE *f)
{
	char *line = NULL;
	size_t cap = 0;
	ssize_t n = getline(&line, &cap, f);

	if (n < 0) {
		free(line);
		return NULL;
	}
	while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
		line[--n] = 0;
	return line;
}

const Asset *
asset(const char *name)
{
	const Asset *a;

	for (a = assets; a->name; a++)
		if (streq(a->name, name))
			return a;
	return NULL;
}

i64
doc_time(void)
{
	const char *e = getenv("SOURCE_DATE_EPOCH");

	if (e && *e)
		return strtoll(e, NULL, 10);
	return (i64)time(NULL);
}
