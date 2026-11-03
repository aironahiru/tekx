/*
 * parse.c -- core tekx in, document tree out.
 *
 * Structure comes from keywords at the start of a line and from
 * indentation.  Inside a block, lines indented further than the block's
 * text are display mathematics: math is a first-class citizen.
 */
#include "bib.h"
#include "pkg.h"
#include "tree.h"

#include <ctype.h>

typedef struct {
	int no, indent;
	char *s;		/* text without indentation or trailing blanks */
	const char *file;	/* the document, or the package file it came from */
} Line;

static Line *L;
static int NL;

static const char *theorem_kinds[] = {
	"theorem", "lemma", "proposition", "corollary", "definition", "example",
	"remark", "note", "conjecture", "claim", "exercise", "solution", "axiom",
	"problem", "observation", "notation", "assumption", "hypothesis", "fact",
};

/* user environments declared with `define NAME like KIND` (via pp's `env`) */
static VEC(char *) env_names;
/* text macros known to pp, for suggestions */
static VEC(char *) macro_names;

/* blocks that packages implement with their filters: `#block NAME PACKAGE` */
typedef struct {
	char *name, *package;
} Custom;
static VEC(Custom) customs;

static Custom *
find_custom(const char *w)
{
	for (size_t i = 0; i < customs.n; i++)
		if (streq(customs.v[i].name, w))
			return &customs.v[i];
	return NULL;
}

/* package filters, run on the finished tree: `#filter PROGRAM DIR` */
typedef struct {
	char *program, *dir;
} Filter;
static VEC(Filter) filters;
static const char *doc_source;	/* the document's file, from pp's #document */

/* ---- Ruby-style arguments: "Ada Lovelace", affiliation: "Dept, Univ" ---- */

typedef struct {
	char *key;	/* NULL for a positional value */
	char *val;
} KV;
typedef VEC(KV) KVs;

/* split at top-level commas; "key: value" items have keys; quotes are
 * removed and protect commas: "London, UK" is one value */
static KVs
kwargs(const char *s)
{
	KVs v = {0};

	while (*s) {
		while (*s == ' ' || *s == ',')
			s++;
		if (!*s)
			break;
		KV kv = {NULL, NULL};
		const char *k = s;
		while (isalnum((unsigned char)*k) || *k == '_' || *k == '-')
			k++;
		if (k > s && *k == ':' && (k[1] == ' ' || k[1] == '"')) {
			kv.key = xstrndup(s, (size_t)(k - s));
			s = k + 1;
			while (*s == ' ')
				s++;
		}
		Buf b = {0};
		if (*s == '"') {
			for (s++; *s && *s != '"'; s++) {
				if (*s == '\\' && s[1])
					s++;
				bputc(&b, *s);
			}
			if (*s == '"')
				s++;
			while (*s && *s != ',')
				s++;
		} else {
			while (*s && *s != ',')
				bputc(&b, *s++);
		}
		kv.val = trim(bstr(&b));
		VPUSH(v, kv);
	}
	return v;
}

static bool
has_kwargs(const char *s)
{
	KVs v = kwargs(s);

	for (size_t i = 0; i < v.n; i++)
		if (v.v[i].key)
			return true;
	return false;
}

static bool
is_blank(int i)
{
	return L[i].s[0] == 0;
}

static char *
first_word(const char *s, const char **rest)
{
	const char *e = s;

	while (*e && *e != ' ')
		e++;
	if (rest) {
		*rest = e;
		while (**rest == ' ')
			(*rest)++;
	}
	return xstrndup(s, (size_t)(e - s));
}

static bool
is_theorem_kind(const char *w)
{
	size_t n = strlen(w);
	if (n > 1 && w[n - 1] == '*') {
		/* theorem*: the same block, unnumbered */
		char *base = xstrndup(w, n - 1);
		return is_theorem_kind(base);
	}
	for (size_t i = 0; i < NELEM(theorem_kinds); i++)
		if (streq(w, theorem_kinds[i]))
			return true;
	for (size_t i = 0; i < env_names.n; i++)
		if (streq(w, env_names.v[i]))
			return true;
	return false;
}

static char *
unquote(const char *s)
{
	size_t n = strlen(s);

	if (n >= 2 && ((s[0] == '"' && s[n - 1] == '"') || (s[0] == '\'' && s[n - 1] == '\'')))
		return xstrndup(s + 1, n - 2);
	return xstrdup(s);
}

/* split off a trailing `#label`; returns label or NULL and trims text */
static char *
take_label(char *s)
{
	char *h = strrchr(s, '#');

	if (h == NULL || (h > s && h[-1] != ' '))
		return NULL;
	for (char *p = h + 1; *p; p++)
		if (!(*p == '-' || *p == '_' || *p == ':' || *p == '.' || (*p >= '0' && *p <= '9') ||
		      ((*p | 32) >= 'a' && (*p | 32) <= 'z')))
			return NULL;
	if (h[1] == 0)
		return NULL;
	char *label = xstrdup(h + 1);
	*h = 0;
	char *t = trim(s);
	memmove(s, t, strlen(t) + 1);
	return label;
}

/* ---- inline markup ---- */

typedef struct {
	const char *s;	/* joined text */
	int *line, *col;	/* origin of each byte */
	size_t n;
} Src;

static void inlines(Node *parent, Src *src, size_t a, size_t b);
static bool is_citation(const char *s, size_t i, size_t b);
static size_t citation(Node *parent, Src *src, size_t i, size_t b);

static void
flush(Node *parent, Buf *t)
{
	if (t->n) {
		node_add(parent, "text", 0, 0, bstr(t));
		breset(t);
	}
}

static bool
is_label_char(char c)
{
	return c == '-' || c == '_' || c == ':' || (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z');
}

static size_t
find_close(const char *s, size_t i, size_t b, const char *close)
{
	size_t cl = strlen(close);

	for (; i + cl <= b; i++) {
		if (s[i] == '\\') {
			i++;
			continue;
		}
		if (strncmp(s + i, close, cl) == 0)
			return i;
	}
	return (size_t)-1;
}

static void
inlines(Node *parent, Src *src, size_t a, size_t b)
{
	const char *s = src->s;
	Buf t = {0};
	size_t i = a;

	while (i < b) {
		char c = s[i];
		char prev = i > a ? s[i - 1] : ' ';
		if (c == '\\' && i + 1 < b) {
			bputc(&t, s[i + 1]);
			i += 2;
			continue;
		}
		if (c == '$') {
			/* Pandoc's rule keeps prices out of mathematics: the opening $
			 * touches the formula, and the closing $ has no blank before it
			 * and no digit after it.  "$5 and $10" stays text. */
			size_t e = (size_t)-1;
			if (i + 1 < b && s[i + 1] != ' ' && s[i + 1] != '$')
				for (size_t k = i + 1; k < b; k++) {
					if (s[k] == '\\') {
						k++;
						continue;
					}
					if (s[k] == '$') {
						/* the next $ closes the formula, or this $ was money */
						if (s[k - 1] != ' ' && !(k + 1 < b && s[k + 1] >= '0' && s[k + 1] <= '9'))
							e = k;
						break;
					}
				}
			if (e == (size_t)-1) {
				bputc(&t, c);
				i++;
				continue;
			}
			flush(parent, &t);
			node_add(parent, "math", src->line[i + 1 < b ? i + 1 : i], src->col[i + 1 < b ? i + 1 : i],
			    xstrndup(s + i + 1, e - i - 1));
			i = e + 1;
			continue;
		}
		if (c == '`') {
			size_t e = find_close(s, i + 1, b, "`");
			if (e != (size_t)-1) {
				flush(parent, &t);
				node_add(parent, "code", 0, 0, xstrndup(s + i + 1, e - i - 1));
				i = e + 1;
				continue;
			}
		}
		if (c == '*' && i + 1 < b && s[i + 1] == '*' && i + 2 < b && s[i + 2] != ' ') {
			size_t e = find_close(s, i + 2, b, "**");
			if (e != (size_t)-1 && e > i + 2) {
				flush(parent, &t);
				inlines(node_add(parent, "strong", 0, 0, ""), src, i + 2, e);
				i = e + 2;
				continue;
			}
		}
		if (c == '*' && i + 1 < b && s[i + 1] != ' ' && s[i + 1] != '*') {
			size_t e = find_close(s, i + 1, b, "*");
			if (e != (size_t)-1 && s[e - 1] != ' ') {
				flush(parent, &t);
				inlines(node_add(parent, "emph", 0, 0, ""), src, i + 1, e);
				i = e + 1;
				continue;
			}
		}
		if (c == '^' && i + 1 < b && s[i + 1] == '[') {
			/* a footnote, ^[text]: brackets nest, mathematics is skipped */
			size_t e = i + 2;
			int depth = 1;
			bool math = false;
			for (; e < b; e++) {
				if (s[e] == '\\') {
					e++;
					continue;
				}
				if (s[e] == '$')
					math = !math;
				else if (!math && s[e] == '[')
					depth++;
				else if (!math && s[e] == ']' && --depth == 0)
					break;
			}
			if (e >= b) {
				error_at(src->line[i], src->col[i], "unterminated footnote: missing closing ]");
				bputc(&t, c);
				i++;
				continue;
			}
			flush(parent, &t);
			inlines(node_add(parent, "note", src->line[i], src->col[i], ""), src, i + 2, e);
			i = e + 1;
			continue;
		}
		if (c == '[' && is_citation(s, i, b)) {
			flush(parent, &t);
			i = citation(parent, src, i, b);
			continue;
		}
		if (c == '[') {
			size_t e = find_close(s, i + 1, b, "](");
			if (e != (size_t)-1) {
				size_t u = find_close(s, e + 2, b, ")");
				if (u != (size_t)-1) {
					flush(parent, &t);
					Node *lk = node_add(parent, "link", 0, 0, xstrndup(s + e + 2, u - e - 2));
					inlines(lk, src, i + 1, e);
					i = u + 1;
					continue;
				}
			}
		}
		if (c == '<' && (strncmp(s + i + 1, "http://", 7) == 0 || strncmp(s + i + 1, "https://", 8) == 0)) {
			size_t e = find_close(s, i + 1, b, ">");
			if (e != (size_t)-1) {
				flush(parent, &t);
				node_add(parent, "url", 0, 0, xstrndup(s + i + 1, e - i - 1));
				i = e + 1;
				continue;
			}
		}
		if ((c == 'h') && (prev == ' ' || prev == '(' || i == a) &&
		    (strncmp(s + i, "http://", 7) == 0 || strncmp(s + i, "https://", 8) == 0)) {
			size_t e = i;
			while (e < b && s[e] != ' ')
				e++;
			while (e > i && strchr(".,;:!?)'\"", s[e - 1]))
				e--;
			flush(parent, &t);
			node_add(parent, "url", 0, 0, xstrndup(s + i, e - i));
			i = e;
			continue;
		}
		if (c == '@' && !is_label_char(prev) && i + 1 < b && is_label_char(s[i + 1])) {
			size_t e = i + 1;
			while (e < b && is_label_char(s[e]))
				e++;
			while (e > i + 1 && (s[e - 1] == ':' || s[e - 1] == '-'))
				e--;
			flush(parent, &t);
			node_add(parent, "ref", src->line[i], src->col[i], xstrndup(s + i + 1, e - i - 1));
			i = e;
			continue;
		}
		if (c == '{' && i + 1 < b && ((s[i + 1] | 32) >= 'a' && (s[i + 1] | 32) <= 'z')) {
			size_t e = i + 1;
			while (e < b && is_label_char(s[e]))
				e++;
			if (e < b && (s[e] == '}' || s[e] == ' ')) {
				char *name = xstrndup(s + i + 1, e - i - 1);
				const char *best = NULL;
				int bd = 3;
				static const char *builtin[] = {"today", "tekx"};
				for (size_t k = 0; k < NELEM(builtin) + macro_names.n; k++) {
					const char *cand = k < NELEM(builtin) ? builtin[k] : macro_names.v[k - NELEM(builtin)];
					int dd = edit_distance(name, cand);
					if (dd < bd) {
						bd = dd;
						best = cand;
					}
				}
				if (best)
					error_at(src->line[i], src->col[i], "unknown macro {%s}; did you mean {%s}?", name, best);
				else
					error_at(src->line[i], src->col[i], "unknown macro {%s} (define it with: define %s as \"...\")", name, name);
			}
		}
		/* typography */
		if (strncmp(s + i, "---", 3) == 0) {
			bputs(&t, "—");
			i += 3;
			continue;
		}
		if (strncmp(s + i, "--", 2) == 0) {
			bputs(&t, "–");
			i += 2;
			continue;
		}
		if (strncmp(s + i, "...", 3) == 0) {
			bputs(&t, "…");
			i += 3;
			continue;
		}
		if (c == '"') {
			bool open = prev == ' ' || prev == '(' || prev == '[' || i == a;
			bputs(&t, open ? "“" : "”");
			i++;
			continue;
		}
		if (c == '\'') {
			bool open = prev == ' ' || prev == '(' || prev == '[' || i == a;
			bputs(&t, open ? "‘" : "’");
			i++;
			continue;
		}
		if (c == '~') {
			bputs(&t, " ");
			i++;
			continue;
		}
		bputc(&t, c);
		i++;
	}
	flush(parent, &t);
}

/*
 * Citations, Pandoc's way: [@key], [@a; @b], [@key, Thm. 2].  Every item
 * starts with @ and items are separated by semicolons; whatever follows
 * the key is a note, and may hold any inline text, mathematics included.
 */
static size_t
citation_end(const char *s, size_t i, size_t b)
{
	bool math = false;

	for (size_t e = i + 1; e < b; e++) {
		if (s[e] == '\\') {
			e++;
			continue;
		}
		if (s[e] == '$')
			math = !math;
		else if (!math && s[e] == '[')
			return (size_t)-1;
		else if (!math && s[e] == ']')
			return e;
	}
	return (size_t)-1;
}

static bool
is_citation(const char *s, size_t i, size_t b)
{
	size_t e = citation_end(s, i, b), k = i + 1;

	if (e == (size_t)-1 || (e + 1 < b && s[e + 1] == '('))
		return false;
	/* every item must start with @key */
	for (;;) {
		while (k < e && s[k] == ' ')
			k++;
		if (k >= e || s[k] != '@' || k + 1 >= e || !is_label_char(s[k + 1]))
			return false;
		bool math = false;
		while (k < e && (math || s[k] != ';')) {
			if (s[k] == '$')
				math = !math;
			k++;
		}
		if (k >= e)
			return true;
		k++;
	}
}

static size_t
citation(Node *parent, Src *src, size_t i, size_t b)
{
	const char *s = src->s;
	size_t e = citation_end(s, i, b), k = i + 1;
	Node *c = node_add(parent, "cite", src->line[i], src->col[i], "");

	while (k < e) {
		while (k < e && s[k] == ' ')
			k++;
		size_t a = k + 1, z = a;
		while (z < e && is_label_char(s[z]))
			z++;
		while (z > a && (s[z - 1] == ':' || s[z - 1] == '-'))
			z--;
		Node *it = node_add(c, "item", src->line[k], src->col[k], xstrndup(s + a, z - a));
		/* the note: after the key, an optional comma, to the semicolon */
		size_t n = z, m;
		bool math = false;
		for (m = n; m < e && (math || s[m] != ';'); m++)
			if (s[m] == '$')
				math = !math;
		while (n < m && (s[n] == ' ' || s[n] == ','))
			n++;
		size_t q = m;
		while (q > n && s[q - 1] == ' ')
			q--;
		if (q > n)
			inlines(it, src, n, q);
		k = m < e ? m + 1 : m;
	}
	return e + 1;
}

/* Join lines a..b-1 (each from column `from` of its text) and parse inlines. */
static void
inline_lines(Node *parent, int a, int b, int skip_first)
{
	Buf j = {0};
	VEC(int) ln = {0}, cl = {0};

	for (int i = a; i < b; i++) {
		const char *s = L[i].s + (i == a ? skip_first : 0);
		int col0 = L[i].indent + (i == a ? skip_first : 0) + 1;
		if (i > a) {
			bputc(&j, ' ');
			VPUSH(ln, L[i].no);
			VPUSH(cl, col0);
		}
		for (int k = 0; s[k]; k++) {
			bputc(&j, s[k]);
			VPUSH(ln, L[i].no);
			VPUSH(cl, col0 + k);
		}
	}
	VPUSH(ln, a < NL ? L[a].no : 0);
	VPUSH(cl, 0);
	Src src = {bstr(&j), ln.v, cl.v, j.n};
	inlines(parent, &src, 0, j.n);
}

static void
inline_text(Node *parent, const char *text, int line, int col)
{
	size_t n = strlen(text);
	int *ln = xmalloc((n + 1) * sizeof *ln), *cl = xmalloc((n + 1) * sizeof *cl);

	for (size_t k = 0; k <= n; k++) {
		ln[k] = line;
		cl[k] = col + (int)k;
	}
	Src src = {text, ln, cl, n};
	inlines(parent, &src, 0, n);
}

/* the typography of running text (quotes, dashes, ellipses) for plain
 * values such as the title, which carry no markup */
static char *
typography(const char *text, int line, int col)
{
	Node *tmp = node_new("tmp", 0, 0, "");
	Buf b = {0};

	inline_text(tmp, text, line, col);
	for (size_t i = 0; i < tmp->kids.n; i++)
		bputs(&b, tmp->kids.v[i]->rest);
	return bstr(&b);
}

/* ---- blocks ---- */

static int blocks(Node *parent, int i, int base, bool top);

/* lines belonging to an indented body after line i: returns end index */
static int
body_end(int i, int base)
{
	int j = i + 1, last = i;

	while (j < NL) {
		if (is_blank(j)) {
			j++;
			continue;
		}
		if (L[j].indent <= base || streq(L[j].s, "#end"))
			break;
		last = j;
		j++;
	}
	return last + 1;
}

static int
first_body_indent(int i, int end)
{
	for (int j = i + 1; j < end; j++)
		if (!is_blank(j))
			return L[j].indent;
	return -1;
}

static bool
is_item(const char *s, int *mlen, bool *ordered, int *num)
{
	if ((s[0] == '-' || s[0] == '+' || s[0] == '*') && s[1] == ' ') {
		*mlen = 2;
		*ordered = false;
		return true;
	}
	int k = 0;
	while (s[k] >= '0' && s[k] <= '9' && k < 6)
		k++;
	if (k > 0 && (s[k] == '.' || s[k] == ')') && s[k + 1] == ' ') {
		*mlen = k + 2;
		*ordered = true;
		*num = atoi(s);
		return true;
	}
	return false;
}

static bool
starts_block(int i)
{
	const char *s = L[i].s, *rest;
	int m, num;
	bool ord;

	if (*s == 0)
		return true;
	if (is_item(s, &m, &ord, &num) || *s == '|' || startswith(s, "```") || startswith(s, "$$") ||
	    startswith(s, "> ") || streq(s, ">") || startswith(s, "# ") || startswith(s, "## ") ||
	    startswith(s, "### ") || startswith(s, "#line "))
		return true;
	char *w = first_word(s, &rest);
	bool kw = streq(w, "section") || streq(w, "subsection") || streq(w, "subsubsection") ||
	    streq(w, "section*") || streq(w, "subsection*") || streq(w, "subsubsection*") ||
	    streq(w, "abstract") || streq(w, "proof") || streq(w, "quote") || streq(w, "define") ||
	    streq(w, "references") || streq(w, "bibliography") || streq(w, "contents") ||
	    streq(w, "newpage") || streq(w, "env") || is_theorem_kind(w) ||
	    streq(w, "figure") || find_custom(w) ||
	    (streq(w, "let") && !strchr(s, '$'));
	free(w);
	return kw;
}

/* does a display line end with a word that takes the deeper lines as rows? */
static bool
takes_rows(const char *s)
{
	static const char *kw[] = {"cases", "matrix", "pmatrix", "bmatrix", "Bmatrix", "vmatrix",
	    "Vmatrix", "system", "aligned"};
	const char *e = s + strlen(s), *w = e;

	while (w > s && w[-1] != ' ' && w[-1] != '=' && w[-1] != '(')
		w--;
	for (size_t i = 0; i < NELEM(kw); i++)
		if (strlen(kw[i]) == (size_t)(e - w) && strncmp(w, kw[i], (size_t)(e - w)) == 0)
			return true;
	return false;
}

/* display math: lines indented deeper than `base`, starting at i */
static int
display(Node *parent, int i, int base)
{
	int ind = L[i].indent;
	Node *d = node_add(parent, "display", L[i].no, 0, "");
	Node *cur = NULL;
	bool rows = false;

	while (i < NL && !is_blank(i) && L[i].indent > base) {
		char *s = xstrdup(L[i].s);
		if (L[i].indent > ind && cur && rows) {
			node_add(cur, "cont", L[i].no, L[i].indent + 1, s);
		} else {
			char *label = take_label(s);
			cur = node_add(d, "line", L[i].no, L[i].indent + 1, fmt("%s %s", label ? label : "-", s));
			rows = takes_rows(s);
		}
		i++;
	}
	return i;
}

static int
paragraph(Node *parent, int i, int base, bool cont)
{
	int a = i;

	while (i < NL && !is_blank(i) && L[i].indent == base && (i == a || !starts_block(i)))
		i++;
	Node *p = node_add(parent, "para", L[a].no, 0, cont ? "1" : "0");
	inline_lines(p, a, i, 0);
	return i;
}

static int
list(Node *parent, int i, int base)
{
	int m, num = 1;
	bool ordered;

	is_item(L[i].s, &m, &ordered, &num);
	Node *lst = node_add(parent, "list", L[i].no, 0, fmt("%d %d", ordered, num));
	while (i < NL) {
		int mm, nn;
		bool oo;
		if (is_blank(i)) {
			int j = i;
			while (j < NL && is_blank(j))
				j++;
			if (j < NL && L[j].indent == base && is_item(L[j].s, &mm, &oo, &nn) && oo == ordered) {
				i = j;
				continue;
			}
			break;
		}
		if (L[i].indent != base || !is_item(L[i].s, &mm, &oo, &nn) || oo != ordered)
			break;
		Node *item = node_add(lst, "item", L[i].no, 0, "");
		int content = base + mm;
		/* rewrite the item's first line as if it started at the content column */
		char *saved = L[i].s;
		int saved_ind = L[i].indent;
		L[i].s = saved + mm;
		while (*L[i].s == ' ') {
			L[i].s++;
			content++;
		}
		L[i].indent = content;
		int end = blocks(item, i, content, false);
		L[i].s = saved;
		L[i].indent = saved_ind;
		i = end;
	}
	return i;
}

static int
table(Node *parent, int i)
{
	Node *t = node_add(parent, "table", L[i].no, 0, "");
	VEC(Node *) rows = {0};
	int header_rows = 0;
	char aligns[64];
	int naligns = 0;

	for (; i < NL && L[i].s[0] == '|'; i++) {
		const char *s = L[i].s + 1;
		/* separator row? */
		bool sep = true;
		for (const char *p = s; *p; p++)
			if (!strchr("|-: ", *p))
				sep = false;
		if (sep && strchr(s, '-')) {
			header_rows = (int)rows.n;
			naligns = 0;
			for (const char *p = s; *p && naligns < 64;) {
				const char *e = strchr(p, '|');
				if (!e)
					e = p + strlen(p);
				char *cell = trim(xstrndup(p, (size_t)(e - p)));
				if (*cell) {
					size_t n = strlen(cell);
					aligns[naligns++] = cell[0] == ':' && cell[n - 1] == ':' ? 'c' : cell[n - 1] == ':' ? 'r' : 'l';
				}
				p = *e ? e + 1 : e;
			}
			continue;
		}
		Node *row = node_new("row", L[i].no, 0, "0");
		size_t k = 0, len = strlen(s), start = 0;
		bool inmath = false, incode = false;
		for (k = 0; k <= len; k++) {
			char c = s[k];
			if (c == '\\' && k + 1 < len) {
				k++;
				continue;
			}
			if (c == '$' && !incode)
				inmath = !inmath;
			if (c == '`' && !inmath)
				incode = !incode;
			if ((c == '|' && !inmath && !incode) || c == 0) {
				char *cell = trim(xstrndup(s + start, k - start));
				if (c == '|' || *cell) {
					Node *cn = node_add(row, "cell", 0, 0, "");
					inline_text(cn, cell, L[i].no, L[i].indent + (int)start + 2);
				}
				start = k + 1;
			}
		}
		VPUSH(rows, row);
	}
	if (naligns) {
		Buf a = {0};
		for (int k = 0; k < naligns; k++)
			bprintf(&a, "%s%c", k ? " " : "", aligns[k]);
		node_add(t, "align", 0, 0, bstr(&a));
	}
	for (size_t r = 0; r < rows.n; r++) {
		if ((int)r < header_rows) {
			free(rows.v[r]->rest);
			rows.v[r]->rest = xstrdup("1");
		}
		node_append(t, rows.v[r]);
	}
	return i;
}

static int
code_block(Node *parent, int i)
{
	int fence = L[i].indent;
	Node *c = node_add(parent, "code", L[i].no, 0, trim(xstrdup(L[i].s + 3)));

	for (i++; i < NL; i++) {
		if (startswith(L[i].s, "```") && L[i].indent <= fence)
			return i + 1;
		int pad = MAX(L[i].indent - fence, 0);
		node_add(c, "verbatim", 0, 0, fmt("%*s%s", pad, "", L[i].s));
	}
	error_at(c->line, fence + 1, "unterminated code block: missing closing ```");
	return i;
}

static int
references(Node *parent, int i, int base)
{
	int end = body_end(i, base), ind = first_body_indent(i, end);
	Node *r = node_add(parent, "refs", L[i].no, 0, "");
	Node *entry = NULL;
	Buf text = {0};
	int eline = 0, ecol = 0;

	for (int j = i + 1; j <= end; j++) {
		bool done = j == end || (!is_blank(j) && L[j].indent == ind);
		if (done && entry) {
			inline_text(entry, bstr(&text), eline, ecol);
			breset(&text);
			entry = NULL;
		}
		if (j == end)
			break;
		if (is_blank(j))
			continue;
		if (L[j].indent == ind) {
			const char *rest;
			char *key = first_word(L[j].s, &rest);
			size_t kl = strlen(key);
			if (kl && key[kl - 1] == ':')
				key[kl - 1] = 0;
			entry = node_add(r, "entry", L[j].no, 0, key);
			bputs(&text, rest);
			eline = L[j].no;
			ecol = L[j].indent + (int)(rest - L[j].s) + 1;
		} else {
			bputc(&text, ' ');
			bputs(&text, L[j].s);
		}
	}
	return end;
}

static Node *section_stack[4];

/* the settings a document may open with; anything else that looks like one
 * (a word followed by a quoted value) is an error, not silently prose */
static const char *settings[] = {"title", "subtitle", "author", "date", "language",
    "keywords", "paper", "size", "font", "msc", "numbering", "bibliography", "citations",
    "running", "linenumbers", "notation"};

static bool
is_setting(const char *w)
{
	for (size_t i = 0; i < NELEM(settings); i++)
		if (streq(w, settings[i]))
			return true;
	return false;
}

static int
lang_version_cmp(const char *a, const char *b)
{
	for (int i = 0; i < 3; i++) {
		int x = atoi(a), y = atoi(b);
		if (x != y)
			return x < y ? -1 : 1;
		a = strchr(a, '.') ? strchr(a, '.') + 1 : "0";
		b = strchr(b, '.') ? strchr(b, '.') + 1 : "0";
	}
	return 0;
}

/* a lowercase word followed by a quoted value and nothing else */
static bool
looks_like_setting(int i)
{
	const char *s = L[i].s, *rest;
	char *w = first_word(s, &rest);
	size_t n = strlen(rest);

	return w[0] >= 'a' && w[0] <= 'z' && n >= 2 && rest[0] == '"' && rest[n - 1] == '"';
}

/* a line that starts with a known setting and a value */
static bool
setting_line(int i)
{
	const char *rest;
	char *w = first_word(L[i].s, &rest);

	return is_setting(w) && *rest;
}

static const char *
closest(const char *w, const char *const *list, size_t n)
{
	const char *best = NULL;
	int bd = w[0] && w[1] && w[2] ? 3 : 2;

	for (size_t k = 0; k < n; k++) {
		int d = edit_distance(w, list[k]);
		if (d < bd) {
			bd = d;
			best = list[k];
		}
	}
	return best;
}

/* author "Ada Lovelace", affiliation: "...", email: "...", orcid: "..." */
static void
author_line(const char *rest, int line, int col)
{
	static const char *const keys[] = {"affiliation", "email", "orcid", "url"};
	KVs v = kwargs(rest);
	Node *a = NULL;

	for (size_t i = 0; i < v.n; i++) {
		KV *kv = &v.v[i];
		if (kv->key == NULL) {
			if (a)
				error_at(line, col, "one author per line when details are given: "
				    "author \"Name\", affiliation: \"...\"");
			a = node_add(section_stack[0], "meta", line, 0,
			    fmt("author %s", typography(kv->val, line, col)));
			continue;
		}
		if (a == NULL) {
			error_at(line, col, "the author's name comes first: author \"Name\", %s: \"...\"", kv->key);
			a = node_add(section_stack[0], "meta", line, 0, "author ?");
		}
		bool known = false;
		for (size_t k = 0; k < NELEM(keys); k++)
			known |= streq(kv->key, keys[k]);
		if (!known) {
			const char *best = closest(kv->key, keys, NELEM(keys));
			if (best)
				error_at(line, col, "unknown author detail '%s:'; did you mean '%s:'?", kv->key, best);
			else
				error_at(line, col, "unknown author detail '%s:' (expected affiliation, email, orcid or url)", kv->key);
			continue;
		}
		node_add(a, kv->key, 0, 0, streq(kv->key, "affiliation") ? typography(kv->val, line, col) : kv->val);
	}
}

static int
blocks(Node *parent, int i, int base, bool top)
{
	bool after_display = false;

	while (i < NL) {
		if (is_blank(i)) {
			/* a sentence may go on after a display, even past a blank
			 * line: "where x is ..." continues, "The next ..." does not */
			i++;
			continue;
		}
		Line *ln = &L[i];
		if (ln->indent < base)
			break;
		if (streq(ln->s, "#end")) {
			if (ln->indent == base && !top)
				break;
			i++;
			continue;
		}
		if (ln->indent > base) {
			i = display(parent, i, base);
			after_display = true;
			continue;
		}
		bool prev_display = after_display;
		after_display = false;
		const char *s = ln->s, *rest;
		int m, num;
		bool ord;
		char *w = first_word(s, &rest);

		/* sections nest: everything until the next heading of the same or higher level */
		int level = 0;
		bool numbered = true;
		if (streq(w, "section") || streq(w, "#"))
			level = 1;
		else if (streq(w, "subsection") || streq(w, "##"))
			level = 2;
		else if (streq(w, "subsubsection") || streq(w, "###"))
			level = 3;
		else if (streq(w, "section*"))
			level = 1, numbered = false;
		else if (streq(w, "subsection*"))
			level = 2, numbered = false;
		else if (streq(w, "subsubsection*"))
			level = 3, numbered = false;
		if (level && *rest && top) {
			char *title = xstrdup(rest), *label = take_label(title);
			Node *container = section_stack[level - 1];
			if (container == NULL)
				for (int k = level - 2; k >= 0 && !container; k--)
					container = section_stack[k];
			if (container == NULL)
				container = parent;
			Node *sec = node_add(container, "section", ln->no, 0,
			    fmt("%d %d %s", level, numbered, label ? label : "-"));
			inline_text(node_add(sec, "heading", 0, 0, ""), unquote(title), ln->no, (int)(rest - s) + 1);
			section_stack[level] = sec;
			for (int k = level + 1; k < 4; k++)
				section_stack[k] = NULL;
			parent = sec;
			i++;
			after_display = false;
			continue;
		}
		if (top && streq(w, "tekx") && *rest && strspn(rest, "0123456789.") == strlen(rest)) {
			/* the language version the document is written for */
			if (lang_version_cmp(rest, TEKX_VERSION) > 0)
				error_at(ln->no, ln->indent + 6, "this document is written for tekx %s, newer than this "
				    "tekx (%s): update tekx to build it", rest, TEKX_VERSION);
			node_add(section_stack[0], "meta", ln->no, 0, fmt("tekx %s", rest));
			i++;
			continue;
		}
		if (top && is_setting(w) && *rest) {
			int col = (int)(rest - s) + ln->indent + 1;
			if (streq(w, "author") && has_kwargs(rest)) {
				author_line(rest, ln->no, col);
			} else if (streq(w, "bibliography")) {
				/* bibliography "refs.bib" [, "more.bib"]: read BibTeX */
				KVs v = kwargs(rest);
				for (size_t k = 0; k < v.n; k++)
					node_add(section_stack[0], "bibfile", ln->no, col, v.v[k].val);
			} else if (streq(w, "linenumbers") && !streq(unquote(rest), "on") && !streq(unquote(rest), "off")) {
				error_at(ln->no, col, "expected: linenumbers on (or off), not '%s'", unquote(rest));
			} else if (streq(w, "citations") && !streq(unquote(rest), "numeric") &&
			    !streq(unquote(rest), "alphabetic") && !streq(unquote(rest), "ordered")) {
				error_at(ln->no, col, "unknown citation style '%s' (expected: citations numeric, "
				    "citations ordered, or citations alphabetic)", unquote(rest));
			} else if (streq(w, "numbering") && !streq(unquote(rest), "by section") &&
			    !streq(unquote(rest), "continuous")) {
				error_at(ln->no, col, "unknown numbering '%s' (expected: numbering by section, or "
				    "numbering continuous)", unquote(rest));
			} else if (streq(w, "notation") && !streq(unquote(rest), "tex") && !streq(unquote(rest), "tekx")) {
				error_at(ln->no, col, "unknown notation '%s' (expected: notation tex, for formulas "
				    "written in LaTeX throughout, or notation tekx)", unquote(rest));
			} else {
				char *v = unquote(rest);
				if (streq(w, "title") || streq(w, "subtitle") || streq(w, "author") || streq(w, "date") ||
				    streq(w, "keywords") || streq(w, "msc") || streq(w, "running"))
					v = typography(v, ln->no, col);
				node_add(section_stack[0], "meta", ln->no, 0,
				    fmt("%s %s", w, v));
			}
			i++;
			continue;
		}
		/* one spelling for each thing: the old second spellings say what to write */
		if (top && *rest && (streq(w, "document") || streq(w, "lang") || streq(w, "subject"))) {
			error_at(ln->no, ln->indent + 1, "%s", streq(w, "document") ?
			    "write each setting on a line of its own: title \"...\", then author \"...\"" :
			    streq(w, "lang") ? "write language, not lang" :
			    "the PDF subject is the subtitle: write subtitle \"...\"");
			i++;
			continue;
		}
		if (top && streq(w, "appendix") && !*rest) {
			/* the sections after this line are appendices: A, B, ... */
			node_add(section_stack[0], "appendix", ln->no, 0, "");
			for (int k = 1; k < 4; k++)
				section_stack[k] = NULL;
			parent = section_stack[0];
			i++;
			continue;
		}
		if (streq(w, "env")) {
			const char *r2;
			char *name = first_word(rest, &r2);
			VPUSH(env_names, name);
			node_add(parent, "env", ln->no, 0, rest);
			i++;
			continue;
		}
		if (streq(w, "define")) {
			Node *d = node_add(parent, "define", ln->no, (int)(rest - s) + ln->indent + 1, rest);
			if (ln->file && doc_source && !streq(ln->file, doc_source))
				node_add(d, "from", 0, 0, ln->file);
			i++;
			continue;
		}
		/* a package's block, never one of tekx's own (those win) */
		Custom *cu = is_theorem_kind(w) || streq(w, "proof") || streq(w, "abstract") || streq(w, "quote") ||
		    streq(w, "let") ? NULL : find_custom(w);
		if (cu) {
			/* a package's block: its header and raw body go to the package's filter */
			int end = body_end(i, base), ind = first_body_indent(i, end);
			Node *c = node_add(parent, "custom", ln->no, 0, fmt("%s %s", w, cu->package));
			if (*rest)
				node_add(c, "args", 0, 0, rest);
			for (int j = i + 1; j < end; j++)
				node_add(c, "verbatim", L[j].no, 0,
				    is_blank(j) ? "" : fmt("%*s%s", MAX(L[j].indent - ind, 0), "", L[j].s));
			i = end;
			continue;
		}
		if (streq(w, "let") && *rest && !strchr(s, '$')) {
			const char *be = strstr(rest, " be ");
			char *math = be ? xstrndup(rest, (size_t)(be - rest)) : xstrdup(rest);
			Node *lt = node_add(parent, "let", ln->no, (int)(rest - s) + ln->indent + 1, math);
			if (be)
				inline_text(node_add(lt, "prose", 0, 0, ""), be + 1, ln->no, (int)(be + 1 - s) + ln->indent + 1);
			i++;
			continue;
		}
		if (streq(w, "abstract") || streq(w, "quote") || streq(w, "proof") || is_theorem_kind(w)) {
			int end = body_end(i, base), ind = first_body_indent(i, end);
			bool needs_body = is_theorem_kind(w);
			if (ind < 0 && needs_body) {
				i = paragraph(parent, i, base, prev_display);
				continue;
			}
			char *title = xstrdup(rest), *label = take_label(title);
			Node *b;
			if (streq(w, "abstract"))
				b = node_add(parent, "abstract", ln->no, 0, "");
			else if (streq(w, "quote"))
				b = node_add(parent, "quote", ln->no, 0, "");
			else if (streq(w, "proof"))
				b = node_add(parent, "proof", ln->no, 0, "");
			else
				b = node_add(parent, "theorem", ln->no, 0, fmt("%s %s", w, label ? label : "-"));
			if (*title)
				inline_text(node_add(b, "heading", 0, 0, ""), unquote(title), ln->no, (int)(rest - s) + 1);
			if (ind >= 0) {
				int saved = NL;
				NL = end;
				int stop = blocks(b, i + 1, ind, false);
				NL = saved;
				if (stop < end)
					error_at(L[stop].no, L[stop].indent + 1,
					    "this line is indented less than the %s it belongs to (expected %d spaces)", w, ind);
				/* a display that ends the block after a blank line, indented
				 * only a little past its text, often meant to stand after it */
				Node *last = b->kids.n ? b->kids.v[b->kids.n - 1] : NULL;
				if (last && streq(last->kind, "display") && b->kids.n > 1 && !streq(w, "proof"))
					for (int j = i + 1; j < end; j++)
						if (L[j].no == last->line && j > 0 && is_blank(j - 1) && L[j].indent - ind < 4) {
							warn_at(L[j].no, L[j].indent + 1, "this display ends the %s above it; if it "
							    "should follow the %s, put a line of text before it or close the %s "
							    "with do ... end (indent it 4 spaces past the text to keep it and "
							    "silence this)", w, w, w);
							break;
						}
			}
			i = end;
			after_display = false;
			continue;
		}
		if (streq(w, "figure") && *rest) {
			/* figure "file.png" [width 60%] [#label], then alt text and a caption */
			char *spec = xstrdup(rest), *label = take_label(spec), *path, *after;
			if (*spec == '"') {
				char *e = strchr(spec + 1, '"');
				if (e == NULL) {
					error_at(ln->no, ln->indent + 8, "missing closing \" after the figure's file name");
					e = spec + strlen(spec);
				}
				path = xstrndup(spec + 1, (size_t)(e - spec - 1));
				after = *e ? e + 1 : e;
			} else {
				const char *a;
				path = first_word(spec, &a);
				after = (char *)a;
			}
			after = trim(after);
			char *width = "-";
			if (startswith(after, "width ")) {
				width = trim(after + 6);
			} else if (*after) {
				error_at(ln->no, ln->indent + 1, "unexpected '%s' after the figure's file name "
				    "(expected: figure \"file.png\" width 60%%)", after);
			}
			Buf pb = {0};
			pct_escape(&pb, path);
			Node *fig = node_add(parent, "figure", ln->no, 0, fmt("%s %s %s", bstr(&pb), width, label ? label : "-"));
			int end = body_end(i, base), ind = first_body_indent(i, end);
			if (ind >= 0) {
				for (int j = i + 1; j < end; j++)
					if (!is_blank(j) && L[j].indent == ind && (startswith(L[j].s, "alt ") || startswith(L[j].s, "alt:"))) {
						node_add(fig, "alt", L[j].no, 0, unquote(trim(L[j].s + 4)));
						L[j].s = "";
					}
				int saved = NL;
				NL = end;
				blocks(fig, i + 1, ind, false);
				NL = saved;
			}
			i = end;
			continue;
		}
		if (streq(w, "references") || streq(w, "bibliography")) {
			i = references(parent, i, base);
			continue;
		}
		if (streq(w, "contents") && !*rest) {
			node_add(parent, "contents", ln->no, 0, "");
			i++;
			continue;
		}
		if (streq(w, "pagebreak") && !*rest) {
			error_at(ln->no, ln->indent + 1, "write newpage, not pagebreak");
			i++;
			continue;
		}
		if (streq(w, "newpage") && !*rest) {
			node_add(parent, "newpage", ln->no, 0, "");
			i++;
			continue;
		}
		if (startswith(s, "```")) {
			i = code_block(parent, i);
			continue;
		}
		if (startswith(s, "$$")) {
			Node *d = node_add(parent, "display", ln->no, 0, "");
			char *one = xstrdup(s + 2);
			char *close = strstr(one, "$$");
			if (close) {
				*close = 0;
				char *label = take_label(trim(one));
				node_add(d, "line", ln->no, ln->indent + 3, fmt("%s %s", label ? label : "-", trim(one)));
				i++;
			} else {
				for (i++; i < NL && !startswith(L[i].s, "$$"); i++) {
					if (is_blank(i))
						continue;
					char *t = xstrdup(L[i].s), *label = take_label(t);
					node_add(d, "line", L[i].no, L[i].indent + 1, fmt("%s %s", label ? label : "-", t));
				}
				if (i >= NL)
					error_at(ln->no, ln->indent + 1, "unterminated $$ display: missing closing $$");
				i++;
			}
			after_display = true;
			continue;
		}
		if (startswith(s, "> ") || streq(s, ">")) {
			Node *q = node_add(parent, "quote", ln->no, 0, "");
			int a = i;
			while (i < NL && L[i].indent == base && (startswith(L[i].s, "> ") || streq(L[i].s, ">")))
				i++;
			/* re-parse the quoted lines with the marker removed */
			Line *saved = L;
			int saved_n = NL;
			Line *sub = xcalloc((size_t)(i - a) + 1, sizeof *sub);
			for (int k = a; k < i; k++) {
				sub[k - a] = saved[k];
				sub[k - a].s = saved[k].s[1] ? saved[k].s + 2 : saved[k].s + 1;
				sub[k - a].indent = 0;
			}
			L = sub;
			NL = i - a;
			blocks(q, 0, 0, false);
			L = saved;
			NL = saved_n;
			continue;
		}
		if (is_item(s, &m, &ord, &num)) {
			i = list(parent, i, base);
			continue;
		}
		if (*s == '|') {
			i = table(parent, i);
			continue;
		}
		{
			/* a lone word over an indented body looks like a misspelled block */
			static const char *blockwords[] = {"theorem", "lemma", "proposition", "corollary",
			    "definition", "example", "remark", "proof", "abstract", "quote", "references",
			    "section", "subsection", "note", "conjecture", "claim", "exercise", "contents", "figure"};
			int end = body_end(i, base);
			if (!*rest && end > i + 1 && w[0] >= 'a' && w[0] <= 'z') {
				const char *best = NULL;
				int bd = 3;
				for (size_t k = 0; k < NELEM(blockwords); k++) {
					int d = edit_distance(w, blockwords[k]);
					if (d < bd) {
						bd = d;
						best = blockwords[k];
					}
				}
				if (best) {
					error_at(ln->no, ln->indent + 1, "unknown block '%s'; did you mean '%s'?", w, best);
					i = end;
					continue;
				}
			}
		}
		if (top && looks_like_setting(i) && (i + 1 >= NL || is_blank(i + 1) || L[i + 1].indent != base ||
		    looks_like_setting(i + 1) || setting_line(i + 1))) {
			/* `word "value"` on a line of its own reads as a setting: say so,
			 * rather than quietly setting it as a paragraph */
			const char *best = closest(w, settings, NELEM(settings));
			if (streq(w, "affiliation") || streq(w, "email") || streq(w, "orcid"))
				error_at(ln->no, ln->indent + 1, "%s belongs to an author: author \"Name\", %s: %s",
				    w, w, rest);
			else if (best)
				error_at(ln->no, ln->indent + 1, "unknown setting '%s'; did you mean '%s'?", w, best);
			else
				error_at(ln->no, ln->indent + 1, "'%s' is not a setting (settings: title, subtitle, author, "
				    "date, language, keywords, msc, numbering, bibliography, citations, running, "
				    "linenumbers, paper, size, font); "
				    "write \\%s to start a paragraph with it", w, w);
			i++;
			continue;
		}
		if (!*rest && w[0] >= 'a' && w[0] <= 'z' && (i + 1 >= NL || is_blank(i + 1))) {
			static const char *const lone[] = {"appendix", "contents", "newpage", "references"};
			const char *best = closest(w, lone, NELEM(lone));
			if (best && !streq(best, w)) {
				error_at(ln->no, ln->indent + 1, "unknown line '%s'; did you mean '%s'?", w, best);
				i++;
				continue;
			}
		}
		if (*s == '\\')
			ln->s++;
		bool cont = prev_display && !(ln->s[0] >= 'A' && ln->s[0] <= 'Z');
		i = paragraph(parent, i, base, cont);
		after_display = false;
	}
	return i;
}

/* ---- citations and the bibliography ---- */

typedef VEC(char *) Names;

static bool
has(const Names *v, const char *s)
{
	for (size_t i = 0; i < v->n; i++)
		if (streq(v->v[i], s))
			return true;
	return false;
}

/* every #label the document defines */
static void
labels_in(Node *n, Names *out)
{
	for (size_t i = 0; i < n->kids.n; i++) {
		Node *k = n->kids.v[i];
		const char *l = streq(k->kind, "section") || streq(k->kind, "figure") ? field(k, 2) :
		    streq(k->kind, "theorem") ? field(k, 1) : streq(k->kind, "line") ? field(k, 0) : NULL;
		if (l && *l && !streq(l, "-"))
			VPUSH(*out, xstrdup(l));
		labels_in(k, out);
	}
}

static Node *
find_kind(Node *n, const char *kind)
{
	for (size_t i = 0; i < n->kids.n; i++) {
		Node *k = n->kids.v[i];
		if (streq(k->kind, kind))
			return k;
		Node *r = find_kind(k, kind);
		if (r)
			return r;
	}
	return NULL;
}

typedef VEC(Node *) NodeList;

static void
nodes_of(Node *n, const char *kind, NodeList *out)
{
	for (size_t i = 0; i < n->kids.n; i++) {
		if (streq(n->kids.v[i]->kind, kind))
			VPUSH(*out, n->kids.v[i]);
		nodes_of(n->kids.v[i], kind, out);
	}
}

/*
 * A bare @key is a cross-reference when the document has #key, and a
 * citation when a reference entry has that key; [@key] is always a
 * citation.  A key that is both is an error, because guessing is how a
 * reader ends up at equation (1) instead of the paper by Landau.
 */
static void
resolve_refs(Node *n, const Names *labels, const Names *keys, Names *cited)
{
	for (size_t i = 0; i < n->kids.n; i++) {
		Node *k = n->kids.v[i];
		if (streq(k->kind, "ref")) {
			char *key = k->rest;
			bool is_label = has(labels, key), is_key = has(keys, key);
			if (is_label && is_key) {
				error_at(k->line, k->col, "@%s names both the target #%s and a reference; write [@%s] "
				    "to cite the reference, or rename #%s", key, key, key, key);
			} else if (is_key) {
				k->kind = xstrdup("cite");
				k->rest = xstrdup("");
				node_add(k, "item", k->line, k->col, key);
			}
		}
		if (streq(k->kind, "cite")) {
			for (size_t j = 0; j < k->kids.n; j++) {
				Node *it = k->kids.v[j];
				if (!streq(it->kind, "item"))
					continue;
				if (has(keys, it->rest)) {
					if (!has(cited, it->rest))
						VPUSH(*cited, it->rest);
					continue;
				}
				const char *best = closest(it->rest, (const char *const *)keys->v, keys->n);
				if (best)
					error_at(it->line, it->col, "unknown reference @%s; did you mean @%s?", it->rest, best);
				else if (keys->n == 0)
					error_at(it->line, it->col, "unknown reference @%s (no bibliography: add "
					    "bibliography \"refs.bib\", or a references block)", it->rest);
				else
					error_at(it->line, it->col, "unknown reference @%s", it->rest);
			}
		}
		resolve_refs(k, labels, keys, cited);
	}
}

typedef struct {
	BibEntry *e;
	char *sort;
} Sorted;

static int
by_sort_key(const void *a, const void *b)
{
	const Sorted *x = a, *y = b;
	int c = strcmp(x->sort, y->sort);

	return c ? c : strcmp(x->e->key, y->e->key);
}

static void
bibliography(Node *doc, const char *source)
{
	Names labels = {0}, keys = {0}, cited = {0};
	BibEntries db = {0};
	NodeList files = {0}, entries = {0};

	labels_in(doc, &labels);
	nodes_of(doc, "entry", &entries);
	for (size_t i = 0; i < entries.n; i++)
		VPUSH(keys, entries.v[i]->rest);
	size_t nmanual = keys.n;

	/* BibTeX files, found next to the source */
	nodes_of(doc, "bibfile", &files);
	const char *slash = strrchr(source, '/');
	char *dir = slash ? xstrndup(source, (size_t)(slash - source + 1)) : "";
	for (size_t i = 0; i < files.n; i++) {
		Node *f = files.v[i];
		char *path = f->rest[0] == '/' ? f->rest : fmt("%s%s", dir, f->rest);
		size_t before = db.n;
		if (!bib_read(path, &db)) {
			error_at(f->line, f->col, "cannot read the bibliography '%s'", f->rest);
			continue;
		}
		for (size_t k = before; k < db.n; k++) {
			if (has(&keys, db.v[k].key)) {
				const char *saved = diag_file;
				diag_file = path;
				warn_at(db.v[k].line, 0, "duplicate key '%s' (the first one is used)", db.v[k].key);
				diag_file = saved;
				db.v[k].key = "";
				continue;
			}
			VPUSH(keys, db.v[k].key);
		}
	}
	bib_resolve(&db);
	resolve_refs(doc, &labels, &keys, &cited);

	/* the cited BibTeX entries, in alphabetical order by author, year, title */
	VEC(Sorted) list = {0};
	for (size_t i = 0; i < db.n; i++) {
		bool manual = false;
		for (size_t k = 0; k < nmanual; k++)
			manual |= streq(keys.v[k], db.v[i].key);
		if (*db.v[i].key && !manual && has(&cited, db.v[i].key)) {
			Sorted so = {&db.v[i], bib_sort_key(&db.v[i])};
			VPUSH(list, so);
		}
	}
	if (list.n == 0)
		return;
	const char *style = "numeric";
	for (size_t i = 0; i < doc->kids.n; i++)
		if (streq(doc->kids.v[i]->kind, "meta") && streq(field(doc->kids.v[i], 0), "citations"))
			style = field_rest(doc->kids.v[i], 1);
	if (streq(style, "ordered")) {
		/* numbered in the order of first citation, as unsrt does */
		for (size_t i = 0; i < list.n; i++)
			for (size_t k = 0; k < cited.n; k++)
				if (streq(cited.v[k], list.v[i].e->key))
					list.v[i].sort = fmt("%08zu", k);
	}
	sort(list.v, list.n, sizeof *list.v, by_sort_key);
	Node *refs = find_kind(doc, "refs");
	if (refs == NULL)
		refs = node_add(doc, "refs", 0, 0, "");
	bool alpha = false;
	for (size_t i = 0; i < doc->kids.n; i++)
		if (streq(doc->kids.v[i]->kind, "meta") && streq(field(doc->kids.v[i], 0), "citations"))
			alpha = streq(field_rest(doc->kids.v[i], 1), "alphabetic");
	Names labels_used = {0};
	for (size_t i = 0; i < list.n; i++) {
		Node *e = node_add(refs, "entry", 0, 0, list.v[i].e->key);
		if (alpha) {
			/* amsalpha: Hoo71, BC47, ABC+99; a second Hoo71 becomes Hoo71a */
			char *l = bib_alpha_label(list.v[i].e);
			int same = 0;
			for (size_t k = 0; k < labels_used.n; k++)
				if (strncmp(labels_used.v[k], l, strlen(l)) == 0)
					same++;
			VPUSH(labels_used, l);
			node_add(e, "label", 0, 0, same ? fmt("%s%c", l, 'a' + same) : l);
			if (same == 1) {
				/* the first of the pair becomes ...a */
				for (size_t k = 0; k + 1 < refs->kids.n; k++) {
					Node *lb = child(refs->kids.v[k], "label");
					if (lb && streq(lb->rest, l))
						lb->rest = fmt("%sa", l);
				}
				child(e, "label")->rest = fmt("%sb", l);
			}
		}
		inline_text(e, bib_format(list.v[i].e), 0, 0);
	}
}

int
parse_main(FILE *in, FILE *out, const char *file)
{
	const char *first_file = NULL;
	VEC(char *) package_lines = {0};

	VEC(Line) lines = {0};
	char *raw;
	int no = 0;

	diag_file = file;
	while ((raw = read_line(in)) != NULL) {
		no++;
		if (startswith(raw, "#macro ")) {
			VPUSH(macro_names, xstrdup(raw + 7));
			free(raw);
			continue;
		}
		if (startswith(raw, "#document ")) {
			char *q = strchr(raw, '"'), *e = q ? strrchr(q + 1, '"') : NULL;
			if (e && e > q + 1)
				doc_source = first_file = xstrndup(q + 1, (size_t)(e - q - 1));
			free(raw);
			continue;
		}
		if (startswith(raw, "#package ")) {
			VPUSH(package_lines, pct_unescape(raw + 9));
			free(raw);
			continue;
		}
		if (startswith(raw, "#block ")) {
			char *t = xstrdup(raw + 7), *sp = strchr(t, ' ');
			Custom cu = {t, sp ? trim(sp + 1) : ""};
			if (sp)
				*sp = 0;
			VPUSH(customs, cu);
			free(raw);
			continue;
		}
		if (startswith(raw, "#filter ")) {
			char *t = xstrdup(raw + 8), *sp = strchr(t, ' ');
			if (sp)
				*sp = 0;
			Filter f = {pct_unescape(t), sp ? pct_unescape(sp + 1) : "."};
			VPUSH(filters, f);
			free(raw);
			continue;
		}
		if (startswith(raw, "#line ")) {
			char *p = raw + 6;
			no = atoi(p) - 1;
			char *q = strchr(p, '"');
			if (q) {
				char *e = strrchr(q + 1, '"');
				if (e && e > q + 1) {
					diag_file = xstrndup(q + 1, (size_t)(e - q - 1));
					if (first_file == NULL && doc_source == NULL)
						first_file = diag_file;
				}
			}
			free(raw);
			continue;
		}
		Line ln;
		ln.file = diag_file;
		int ind = 0;
		char *s = raw;
		while (*s == ' ' || *s == '\t') {
			ind += *s == '\t' ? 4 - ind % 4 : 1;
			s++;
		}
		ln.no = no;
		ln.indent = ind;
		ln.s = trim(xstrdup(s));
		VPUSH(lines, ln);
		free(raw);
	}
	L = lines.v;
	NL = (int)lines.n;
	/* the root names the source, so that later stages, which may read a
	 * pipe, still report errors and find files relative to it */
	Node *doc = node_new("document", 0, 0, first_file ? first_file : file);
	section_stack[0] = doc;
	for (size_t i = 0; i < package_lines.n; i++)
		node_add(doc, "package", 0, 0, package_lines.v[i]);
	diag_file = first_file ? first_file : file;
	blocks(doc, 0, 0, true);
	diag_file = first_file ? first_file : file;
	bibliography(doc, first_file ? first_file : file);
	if (filters.n == 0 || nerrors) {
		tree_write(out, doc, 0);
		return nerrors ? 1 : 0;
	}
	/* the packages' filters, in order, each reading the tree the one
	 * before it wrote: in-house programs extending tekx */
	char *buf = NULL;
	size_t len = 0;
	FILE *mem = open_memstream(&buf, &len);
	if (mem == NULL)
		die("open_memstream failed");
	tree_write(mem, doc, 0);
	fclose(mem);
	for (size_t i = 0; i < filters.n; i++) {
		char *err = NULL;
		size_t n = 0;
		char *next = pkg_filter(filters.v[i].program, filters.v[i].dir, buf, len, &n, &err);
		if (next == NULL) {
			error_at(0, 0, "package filter: %s", err);
			break;
		}
		buf = next;
		len = n;
	}
	fwrite(buf, 1, len, out);
	return nerrors ? 1 : 0;
}
