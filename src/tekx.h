/*
 * tekx.h -- common definitions.
 *
 * tekx is a pipeline of small filters connected by text streams:
 *
 *	pp | parse | eqn | set | pdf
 *
 * Memory is allocated and never freed: each stage is a short-lived
 * filter, and the operating system reclaims everything at exit.
 */
#ifndef TEKX_H
#define TEKX_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

#define TEKX_VERSION "0.2.0"
#define TEKX_VERSION_SHORT "0.2"	/* the language version a document may declare: tekx 0.2 */

#if defined(__GNUC__) || defined(__clang__)
#define PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define PRINTF(a, b)
#endif
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/* memory: allocation failure is fatal */
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t sz);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
#define NEW(T) ((T *)xcalloc(1, sizeof(T)))

/* growable arrays */
#define VEC(T) struct { T *v; size_t n, cap; }
#define VPUSH(a, x) do { \
	if ((a).n == (a).cap) { \
		(a).cap = (a).cap ? 2 * (a).cap : 8; \
		(a).v = xrealloc((a).v, (a).cap * sizeof *(a).v); \
	} \
	(a).v[(a).n++] = (x); \
} while (0)

/* byte buffers */
typedef struct {
	char *s;
	size_t n, cap;
} Buf;

void bput(Buf *b, const void *p, size_t n);
void bputs(Buf *b, const char *s);
void bputc(Buf *b, int c);
void bprintf(Buf *b, const char *fmt, ...) PRINTF(2, 3);
void bnum(Buf *b, double v);	/* shortest decimal, at most 3 places */
char *bstr(Buf *b);		/* NUL-terminated contents */
void breset(Buf *b);

/* UTF-8 */
int utf8_dec(const char *s, size_t n, u32 *cp);	/* bytes consumed (>=1) */
void utf8_enc(Buf *b, u32 cp);
size_t utf8_len(const char *s);	/* number of code points */
u32 utf8_first(const char *s);

/* sorting: qsort, but defined for an empty array (whose pointer may be NULL) */
static inline void
sort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
	if (n > 1)
		qsort(base, n, size, cmp);
}

/* strings */
bool streq(const char *a, const char *b);
bool startswith(const char *s, const char *prefix);
char *trim(char *s);		/* in place, returns s advanced */
char *fmt(const char *fmt, ...) PRINTF(1, 2);	/* allocated sprintf */
int edit_distance(const char *a, const char *b);
void pct_escape(Buf *b, const char *s);	/* escape %, :, space, controls */
char *pct_unescape(const char *s);
char *next_field(char **line);		/* split off one space-separated field */

/* diagnostics: file:line:col: kind: message */
extern const char *diag_file;
extern int nerrors;
extern Buf *diag_sink;		/* when set, diagnostics collect here instead of printing */
void error_at(int line, int col, const char *fmt, ...) PRINTF(3, 4);
void warn_at(int line, int col, const char *fmt, ...) PRINTF(3, 4);
void note(const char *fmt, ...) PRINTF(1, 2);
_Noreturn void die(const char *fmt, ...) PRINTF(1, 2);

/*
 * Internal invariants, checked in every build.  A broken invariant is a
 * bug in tekx, never in the document: tekx stops with exit status 70 and
 * writes nothing, rather than a file that is subtly wrong.
 */
_Noreturn void internal_error(const char *file, int line, const char *what);
#define INVARIANT(c) ((c) ? (void)0 : internal_error(__FILE__, __LINE__, #c))

_Static_assert(sizeof(u8) == 1 && sizeof(u16) == 2 && sizeof(u32) == 4 && sizeof(u64) == 8,
    "fixed-width types");

/* I/O */
char *read_file(const char *path, size_t *len);
char *read_all(FILE *f, size_t *len);
char *read_line(FILE *f);	/* without newline; NULL at EOF */

/* embedded assets (fonts, hyphenation patterns) */
typedef struct {
	const char *name;
	const unsigned char *data;
	size_t len;
} Asset;
extern const Asset assets[];
const Asset *asset(const char *name);

/* time: SOURCE_DATE_EPOCH or now */
i64 doc_time(void);

/* stages: each reads `in` and writes `out`; returns exit status */
int pp_main(FILE *in, FILE *out, const char *file);
int parse_main(FILE *in, FILE *out, const char *file);
int eqn_main(FILE *in, FILE *out, const char *file);
int set_main(FILE *in, FILE *out, const char *file);
int pdf_main(FILE *in, FILE *out, const char *file);
int html_main(FILE *in, FILE *out, const char *file);	/* a second back end: HTML and MathML */
/* LaTeX, both ways (texout.c, latex.c) */
int tex_main(FILE *in, FILE *out, const char *file);	/* a third back end: LaTeX for submission */
int latex_import(FILE *in, FILE *out, const char *file);	/* LaTeX in, tekx out */

/* commands around the compiler (cli.c) */
int watch_main(const char *self, int argc, char **argv, const char *file);	/* tekx -w */
int new_main(int argc, char **argv);	/* tekx new NAME */

/* hyphenation */
void hyph_init(void);
int hyph_points(const char *word, unsigned char *points);	/* points[i]=1: break before byte i */

/* compression and hashing */
u8 *zlib_compress(const u8 *data, size_t n, size_t *outlen);
u8 *zlib_decompress(const u8 *data, size_t n, size_t *outlen);
void md5(const u8 *data, size_t n, u8 out[16]);

#endif
