/*
 * pp.c -- the macro preprocessor: .tekx in, core .tekx out.
 *
 * Like cpp and m4 before it, pp knows nothing of documents; it rewrites
 * lines.  It strips // comments, turns `do ... end` into indentation,
 * collects definitions (wherever they appear: definitions are hoisted),
 * expands block macros and {text macros}, and marks the origin of every
 * line it moves with `#line N "file"` so later stages report errors
 * against the user's own source.
 *
 * Definitions it understands:
 *
 *	define NAME as "TEXT"			text macro, used as {NAME}
 *	define NAME(a, b = "x") as "TEXT {a}"	text macro with parameters
 *	define block NAME(a, b = "x")		block macro; the indented lines
 *	  TEMPLATE using {a} and {body}		below are its template
 *	define NAME like KIND [titled "T"]	new theorem-like environment
 *
 * Any other `define` line (mathematical notation) is passed through for
 * the eqn stage.
 *
 *	use NAME [VERSION]			take in a package's definitions
 *
 * splices in the files a package provides (and those of the packages it
 * depends on), each line keeping its own file for diagnostics, and
 * announces the package's custom blocks and filters to parse:
 *
 *	#package NAME VERSION DIR
 *	#block NAME PACKAGE
 *	#filter PROGRAM PACKAGE-DIR
 */
#include "tekx.h"
#include "pkg.h"

#include <time.h>

typedef struct {
	int no;			/* source line number */
	char *s;		/* text, tabs expanded */
	const char *file;	/* the file it came from: the document or a package */
} Line;

typedef VEC(Line) Lines;
typedef VEC(char *) Strs;

typedef struct {
	char *name;
	VEC(char *) params;
	VEC(char *) defaults;	/* NULL: required */
	char *text;		/* text macros */
	Lines body;		/* block macros: template, indentation relative to 0 */
	bool block;
	int no;
} Macro;

static VEC(Macro) macros;

static Macro *
find_macro(const char *name, bool block)
{
	for (size_t i = 0; i < macros.n; i++)
		if (macros.v[i].block == block && streq(macros.v[i].name, name))
			return &macros.v[i];
	return NULL;
}

static int
indent_of(const char *s)
{
	int n = 0;

	while (s[n] == ' ')
		n++;
	return n;
}

static bool
is_name_char(char c)
{
	return c == '_' || c == '-' || (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z');
}

static char *
expand_tabs(const char *s)
{
	Buf b = {0};
	int col = 0;

	for (; *s; s++) {
		if (*s == '\t') {
			do
				bputc(&b, ' ');
			while (++col % 4);
		} else {
			bputc(&b, *s);
			if (((unsigned char)*s & 0xC0) != 0x80)
				col++;
		}
	}
	return bstr(&b);
}

/* Remove a // comment (preceded by a blank or at the start of the line,
 * outside `code` and "strings").  Sets *whole when the line was only a comment. */
static char *
strip_comment(char *s, bool *whole)
{
	bool code = false, str = false;

	*whole = false;
	for (char *p = s; *p; p++) {
		if (*p == '\\' && p[1]) {
			p++;
			continue;
		}
		if (*p == '`')
			code = !code;
		else if (*p == '"' && !code)
			str = !str;
		else if (!code && !str && p[0] == '/' && p[1] == '/' && (p == s || p[-1] == ' ')) {
			char *q = p;
			while (q > s && q[-1] == ' ')
				q--;
			*whole = q == s;
			*q = 0;
			return s;
		}
	}
	return s;
}

static char *
unquote(const char *s)
{
	size_t n = strlen(s);

	if (n >= 2 && s[0] == '"' && s[n - 1] == '"')
		return xstrndup(s + 1, n - 2);
	return xstrdup(s);
}

/* Split "a, b = 2, c" at top-level commas (outside quotes and brackets). */
static Strs
split_args(const char *s)
{
	Strs v = {0};
	int depth = 0;
	bool str = false;
	const char *start = s;

	if (*trim(xstrdup(s)) == 0)
		return v;
	for (const char *p = s;; p++) {
		if (*p == '\\' && p[1]) {
			p++;
			continue;
		}
		if (*p == '"')
			str = !str;
		else if (!str && (*p == '(' || *p == '[' || *p == '{'))
			depth++;
		else if (!str && (*p == ')' || *p == ']' || *p == '}'))
			depth--;
		if (*p == 0 || (*p == ',' && depth == 0 && !str)) {
			VPUSH(v, trim(xstrndup(start, (size_t)(p - start))));
			if (*p == 0)
				break;
			start = p + 1;
		}
	}
	return v;
}

/* Parse "NAME(params)" at s; returns the rest after the closing paren. */
static const char *
parse_signature(Macro *m, const char *s, int no)
{
	const char *p = s;

	while (is_name_char(*p))
		p++;
	m->name = xstrndup(s, (size_t)(p - s));
	if (*p == '(') {
		const char *e = strchr(p, ')');
		if (e == NULL) {
			error_at(no, (int)(p - s) + 1, "missing ')' in definition of %s", m->name);
			return p + strlen(p);
		}
		Strs ps = split_args(xstrndup(p + 1, (size_t)(e - p - 1)));
		for (size_t i = 0; i < ps.n; i++) {
			char *eq = strchr(ps.v[i], '=');
			if (eq) {
				*eq = 0;
				VPUSH(m->params, trim(ps.v[i]));
				VPUSH(m->defaults, unquote(trim(eq + 1)));
			} else {
				VPUSH(m->params, ps.v[i]);
				VPUSH(m->defaults, (char *)NULL);
			}
		}
		p = e + 1;
	}
	while (*p == ' ')
		p++;
	return p;
}

/* Bind call arguments (positional or `key: value`) to a macro's parameters. */
static char **
bind_args(Macro *m, const char *argtext, int no, int col)
{
	char **vals = xcalloc(m->params.n + 1, sizeof *vals);
	Strs args = split_args(argtext);
	size_t pos = 0;

	if (m->params.n == 1 && args.n > 1 && !strchr(argtext, ':')) {
		/* a single parameter takes the whole text, commas and all */
		vals[0] = unquote(trim(xstrdup(argtext)));
		return vals;
	}
	for (size_t i = 0; i < args.n; i++) {
		char *a = args.v[i], *colon = NULL;
		/* key: value (key is a parameter name) */
		for (char *p = a; is_name_char(*p) || *p == ':'; p++)
			if (*p == ':') {
				colon = p;
				break;
			}
		if (colon) {
			*colon = 0;
			size_t k;
			for (k = 0; k < m->params.n; k++)
				if (streq(m->params.v[k], a))
					break;
			if (k == m->params.n) {
				*colon = ':';
				goto positional;
			}
			vals[k] = unquote(trim(colon + 1));
			continue;
		}
	positional:
		while (pos < m->params.n && vals[pos])
			pos++;
		if (pos == m->params.n) {
			error_at(no, col, "too many arguments for %s (it takes %zu)", m->name, m->params.n);
			break;
		}
		vals[pos++] = unquote(a);
	}
	for (size_t k = 0; k < m->params.n; k++) {
		if (vals[k] == NULL && m->defaults.v[k])
			vals[k] = m->defaults.v[k];
		if (vals[k] == NULL) {
			error_at(no, col, "%s needs a value for '%s'", m->name, m->params.v[k]);
			vals[k] = "";
		}
	}
	return vals;
}

/* Replace {param} in s by the bound values. */
static char *
substitute(Macro *m, char **vals, const char *s)
{
	Buf b = {0};

	for (const char *p = s; *p;) {
		if (*p == '{') {
			const char *e = p + 1;
			while (is_name_char(*e))
				e++;
			if (*e == '}') {
				size_t n = (size_t)(e - p - 1);
				size_t k;
				for (k = 0; k < m->params.n; k++)
					if (strlen(m->params.v[k]) == n && strncmp(m->params.v[k], p + 1, n) == 0)
						break;
				if (k < m->params.n) {
					bputs(&b, vals[k]);
					p = e + 1;
					continue;
				}
			}
		}
		bputc(&b, *p++);
	}
	return bstr(&b);
}

static char *
today(void)
{
	static const char *month[] = {"January", "February", "March", "April", "May", "June", "July",
	    "August", "September", "October", "November", "December"};
	time_t t = (time_t)doc_time();
	struct tm tm;

	gmtime_r(&t, &tm);
	return fmt("%d %s %d", tm.tm_mday, month[tm.tm_mon], tm.tm_year + 1900);
}

/* Expand {text macros} in one line, outside `code` and $math$. */
static char *
expand_text(const char *s, int no, int depth)
{
	Buf b = {0};
	bool code = false, math = false;

	if (depth > 32) {
		error_at(no, 0, "macros nested too deeply (is a macro using itself?)");
		return xstrdup(s);
	}
	for (const char *p = s; *p;) {
		if (*p == '\\' && p[1]) {
			bputc(&b, *p++);
			bputc(&b, *p++);
			continue;
		}
		if (*p == '`' && !math)
			code = !code;
		else if (*p == '$' && !code)
			math = !math;
		if (*p == '{' && !code && !math && ((p[1] | 32) >= 'a' && (p[1] | 32) <= 'z')) {
			const char *e = p + 1;
			while (is_name_char(*e))
				e++;
			char *name = xstrndup(p + 1, (size_t)(e - p - 1));
			Macro *m = find_macro(name, false);
			bool builtin = streq(name, "today") || streq(name, "tekx");
			if ((m || builtin) && (*e == '}' || *e == ' ' || *e == '(')) {
				/* find the matching close brace */
				int d = 1;
				const char *q = e;
				for (; *q && d; q++) {
					if (*q == '{')
						d++;
					else if (*q == '}')
						d--;
				}
				if (d) {
					error_at(no, (int)(p - s) + 1, "missing '}' after {%s", name);
					bputs(&b, p);
					break;
				}
				char *args = xstrndup(e, (size_t)(q - 1 - e));
				args = trim(args);
				if (*args == '(' && args[strlen(args) - 1] == ')') {
					args[strlen(args) - 1] = 0;
					args++;
				}
				char *out;
				if (builtin && !m) {
					out = streq(name, "today") ? today() : "tekx";
				} else {
					char *inner = expand_text(args, no, depth + 1);
					char **vals = bind_args(m, inner, no, (int)(p - s) + 1);
					out = substitute(m, vals, m->text);
				}
				bputs(&b, expand_text(out, no, depth + 1));
				p = q;
				continue;
			}
		}
		bputc(&b, *p++);
	}
	return bstr(&b);
}

static Lines out;
static const char *emit_file;	/* the file of the line being handled */

static void
emit(int no, const char *s)
{
	Line l = {no, xstrdup(s), emit_file};

	VPUSH(out, l);
}

static bool
is_fence(const char *s)
{
	return startswith(s + indent_of(s), "```");
}

/* Expand block macros in lines[a..b), appending to out. */
static void
expand_blocks(Line *ls, size_t n, int depth)
{
	bool fenced = false;

	for (size_t i = 0; i < n; i++) {
		const char *s = ls[i].s;
		int ind = indent_of(s);
		emit_file = ls[i].file;
		diag_file = ls[i].file;
		if (is_fence(s))
			fenced = !fenced;
		if (fenced || is_fence(s)) {
			emit(ls[i].no, s);
			continue;
		}
		const char *w = s + ind, *e = w;
		while (is_name_char(*e))
			e++;
		char *name = xstrndup(w, (size_t)(e - w));
		Macro *m = *name && (*e == 0 || *e == ' ' || *e == '(') ? find_macro(name, true) : NULL;
		if (m == NULL) {
			emit(ls[i].no, s);
			continue;
		}
		if (depth > 16) {
			error_at(ls[i].no, ind + 1, "block macros nested too deeply (is %s using itself?)", name);
			continue;
		}
		/* the call's body: following lines indented deeper */
		size_t j = i + 1, last = i;
		for (; j < n; j++) {
			if (ls[j].s[indent_of(ls[j].s)] == 0)
				continue;
			if (indent_of(ls[j].s) <= ind)
				break;
			last = j;
		}
		char *argtext = trim(xstrdup(e));
		if (*argtext == '(' && argtext[strlen(argtext) - 1] == ')') {
			argtext[strlen(argtext) - 1] = 0;
			argtext++;
		}
		char **vals = bind_args(m, expand_text(argtext, ls[i].no, 0), ls[i].no, ind + 1);
		int body_base = -1;
		for (size_t k = i + 1; k <= last; k++)
			if (ls[k].s[indent_of(ls[k].s)] && (body_base < 0 || indent_of(ls[k].s) < body_base))
				body_base = indent_of(ls[k].s);
		Lines exp = {0};
		for (size_t t = 0; t < m->body.n; t++) {
			const char *tl = m->body.v[t].s;
			int tind = indent_of(tl);
			if (streq(tl + tind, "{body}")) {
				for (size_t k = i + 1; k <= last; k++) {
					const char *bl = ls[k].s;
					int bind = indent_of(bl);
					Line l = {ls[k].no, bl[bind] ? fmt("%*s%s", ind + tind + bind - body_base, "", bl + bind) : "",
					    ls[k].file};
					VPUSH(exp, l);
				}
				continue;
			}
			char *sub = substitute(m, vals, tl + tind);
			/* {body} inside a line: the body's text joined */
			char *bp = strstr(sub, "{body}");
			if (bp) {
				Buf joined = {0};
				for (size_t k = i + 1; k <= last; k++) {
					char *t2 = trim(xstrdup(ls[k].s));
					if (*t2)
						bprintf(&joined, "%s%s", joined.n ? " " : "", t2);
				}
				*bp = 0;
				sub = fmt("%s%s%s", sub, bstr(&joined), bp + 6);
			}
			Line l = {ls[i].no, fmt("%*s%s", ind + tind, "", sub), ls[i].file};
			VPUSH(exp, l);
		}
		expand_blocks(exp.v, exp.n, depth + 1);
		i = last;
	}
}

static void
define(Line *ls, size_t n, size_t *i)
{
	Line *l = &ls[*i];
	int ind = indent_of(l->s);
	const char *s = l->s + ind + 7;	/* after "define " */
	Macro m;

	emit_file = l->file;
	diag_file = l->file;
	memset(&m, 0, sizeof m);
	m.no = l->no;
	while (*s == ' ')
		s++;
	if (startswith(s, "block ")) {
		const char *rest = parse_signature(&m, s + 6, l->no);
		if (*rest)
			error_at(l->no, 0, "unexpected '%s' after block macro signature", rest);
		m.block = true;
		size_t j = *i + 1;
		int base = -1;
		for (; j < n; j++) {
			int k = indent_of(ls[j].s);
			if (ls[j].s[k] == 0) {
				Line blank = {ls[j].no, "", ls[j].file};
				VPUSH(m.body, blank);
				continue;
			}
			if (k <= ind)
				break;
			if (base < 0)
				base = k;
			Line t = {ls[j].no, xstrdup(ls[j].s + MIN(k, base)), ls[j].file};
			if (k >= base)
				t.s = fmt("%*s%s", k - base, "", ls[j].s + k);
			VPUSH(m.body, t);
		}
		while (m.body.n && m.body.v[m.body.n - 1].s[0] == 0)
			m.body.n--;
		if (m.body.n == 0)
			error_at(l->no, ind + 1, "block macro %s has no template (indent its lines below the define)", m.name);
		if (find_macro(m.name, true))
			error_at(l->no, ind + 1, "block macro %s is already defined", m.name);
		VPUSH(macros, m);
		*i = j - 1;
		return;
	}
	const char *nm = s;
	while (is_name_char(*nm))
		nm++;
	const char *after = nm;
	while (*after == ' ')
		after++;
	if (startswith(after, "like ")) {
		/* define NAME like KIND [titled "Title"] */
		char *name = xstrndup(s, (size_t)(nm - s));
		const char *k = after + 5, *ke = k;
		while (is_name_char(*ke))
			ke++;
		char *kind = xstrndup(k, (size_t)(ke - k));
		const char *t = strstr(ke, "titled ");
		char *title = t ? unquote(trim(xstrdup(t + 7))) : xstrdup(name);
		if (!t && title[0] >= 'a' && title[0] <= 'z')
			title[0] = (char)(title[0] - 32);
		emit(l->no, fmt("%*senv %s %s %s", ind, "", name, kind, title));
		return;
	}
	/* text macro: define NAME[(params)] as "TEXT" */
	Macro tm;
	memset(&tm, 0, sizeof tm);
	const char *rest = parse_signature(&tm, s, l->no);
	if (startswith(rest, "as \"")) {
		const char *q = rest + 3;
		size_t qn = strlen(q);
		if (qn >= 2 && q[qn - 1] == '"') {
			tm.text = xstrndup(q + 1, qn - 2);
			tm.no = l->no;
			if (find_macro(tm.name, false))
				error_at(l->no, ind + 8, "macro %s is already defined", tm.name);
			VPUSH(macros, tm);
			return;
		}
	}
	emit(l->no, l->s);	/* mathematical definition: for eqn */
}

/* ---- packages ---- */

static Strs used, markers;
static const char *doc_file;
static bool missing_package;	/* everything after a failed `use` would be noise */

static void read_lines(FILE *in, const char *file, Lines *into);

static void
use_package(Package *p, Lines *into, int no)
{
	for (size_t i = 0; i < used.n; i++)
		if (streq(used.v[i], p->name))
			return;
	VPUSH(used, p->name);
	for (size_t i = 0; i < p->depends.n; i++) {
		char *d = xstrdup(p->depends.v[i]), *sp = strchr(d, ' '), *err = NULL;
		if (sp)
			*sp = 0;
		Package *q = pkg_use(doc_file, d, sp ? trim(sp + 1) : NULL, &err);
		if (q == NULL) {
			error_at(no, 1, "%s (needed by %s)", err, p->name);
			missing_package = true;
		} else
			use_package(q, into, no);
	}
	VPUSH(markers, fmt("#package %s %s %s", p->name, p->version, p->dir));
	for (size_t i = 0; i < p->blocks.n; i++)
		VPUSH(markers, fmt("#block %s %s", p->blocks.v[i], p->name));
	for (size_t i = 0; i < p->filters.n; i++)
		VPUSH(markers, fmt("#filter %s/%s %s", p->dir, p->filters.v[i], p->dir));
	for (size_t i = 0; i < p->provides.n; i++) {
		char *path = fmt("%s/%s", p->dir, p->provides.v[i]);
		FILE *f = fopen(path, "rb");
		if (f == NULL) {
			error_at(no, 1, "cannot read %s", path);
			continue;
		}
		read_lines(f, path, into);
		fclose(f);
	}
}

/* `use NAME [VERSION]` at the start of a line, outside code */
static bool
use_line(const char *s, char **name, char **version)
{
	if (!startswith(s, "use "))
		return false;
	char *t = trim(xstrdup(s + 4)), *sp = strchr(t, ' ');
	if (sp) {
		*sp = 0;
		*version = trim(sp + 1);
	} else {
		*version = NULL;
	}
	*name = t;
	return **name != 0;
}

static void
read_lines(FILE *in, const char *file, Lines *into)
{
	char *raw;
	int no = 0;
	bool fenced = false;

	while ((raw = read_line(in)) != NULL) {
		if (no == 0 && startswith(raw, "\xef\xbb\xbf"))
			raw += 3;	/* a byte order mark is not text */
		Line l = {++no, expand_tabs(raw), file};
		size_t n = strlen(l.s);
		while (n && l.s[n - 1] == ' ')
			l.s[--n] = 0;
		if (is_fence(l.s))
			fenced = !fenced;
		char *name, *version;
		bool whole;
		char *code = strip_comment(xstrdup(l.s), &whole);
		if (!fenced && !whole && use_line(code, &name, &version)) {
			char *err = NULL;
			const char *saved = diag_file;
			diag_file = file;
			Package *p = pkg_use(doc_file, name, version, &err);
			if (p == NULL) {
				error_at(no, 5, "%s", err);
				missing_package = true;
			} else
				use_package(p, into, no);
			diag_file = saved;
			continue;
		}
		VPUSH(*into, l);
	}
}

int
pp_main(FILE *in, FILE *outf, const char *file)
{
	Lines src = {0}, norm = {0};

	diag_file = file;
	doc_file = file;
	read_lines(in, file, &src);
	if (missing_package)
		return 1;	/* the rest would only repeat what a missing package explains */

	/* 1. comments, and do ... end -> indentation */
	struct {
		int shift;	/* added to original indentation of body lines */
		int want;	/* indentation of the body */
		bool pending;	/* shift not yet known (no body line seen) */
		int no;
	} stack[64];
	int sp = 0;
	bool fenced = false;
	for (size_t i = 0; i < src.n; i++) {
		char *s = src.v[i].s;
		int ind = indent_of(s);
		diag_file = src.v[i].file;
		if (is_fence(s)) {
			fenced = !fenced;
		} else if (!fenced) {
			bool whole;
			s = strip_comment(s, &whole);
			if (whole)
				continue;
			ind = indent_of(s);
		}
		if (!fenced && streq(s + ind, "end") && sp > 0) {
			/* an explicit end of block, so that deeper lines after it
			 * cannot be mistaken for the block's body */
			sp--;
			Line l = {src.v[i].no, fmt("%*s#end", stack[sp].want - 2, ""), src.v[i].file};
			VPUSH(norm, l);
			continue;
		}
		int shift = 0;
		if (sp > 0 && s[ind]) {
			if (stack[sp - 1].pending) {
				stack[sp - 1].shift = stack[sp - 1].want - ind;
				stack[sp - 1].pending = false;
			}
			shift = stack[sp - 1].shift;
		}
		int nind = s[ind] ? MAX(ind + shift, sp > 0 ? stack[sp - 1].want : 0) : 0;
		if (fenced && !is_fence(s))
			nind = ind + shift;
		char *text = s + ind;
		size_t tl = strlen(text);
		bool opens = !fenced && tl >= 3 && streq(text + tl - 3, " do") && !strchr(text, '`');
		if (!opens && !fenced && streq(text, "do"))
			opens = true;
		if (opens) {
			text = xstrndup(text, tl >= 3 && text[tl - 3] == ' ' ? tl - 3 : 0);
			if (sp == (int)NELEM(stack))
				die("%s:%d: do ... end nested too deeply", file, src.v[i].no);
			stack[sp].want = nind + 2;
			stack[sp].pending = true;
			stack[sp].shift = 0;
			stack[sp].no = src.v[i].no;
			sp++;
			if (*text == 0)
				continue;
		}
		Line l = {src.v[i].no, *text ? fmt("%*s%s", nind, "", text) : "", src.v[i].file};
		VPUSH(norm, l);
	}
	diag_file = file;
	while (sp > 0)
		error_at(stack[--sp].no, 0, "'do' without a matching 'end'");

	/* 2. definitions (hoisted), outside code fences */
	Lines body = {0};
	fenced = false;
	for (size_t i = 0; i < norm.n; i++) {
		char *s = norm.v[i].s;
		if (is_fence(s))
			fenced = !fenced;
		if (!fenced && startswith(s + indent_of(s), "define ")) {
			size_t before = out.n;
			define(norm.v, norm.n, &i);
			for (size_t k = before; k < out.n; k++)
				VPUSH(body, out.v[k]);
			out.n = before;
			continue;
		}
		VPUSH(body, norm.v[i]);
	}

	/* 3. block macros, then 4. text macros */
	expand_blocks(body.v, body.n, 0);
	fprintf(outf, "#document \"%s\"\n", file);
	for (size_t i = 0; i < markers.n; i++)
		fprintf(outf, "%s\n", markers.v[i]);
	for (size_t i = 0; i < macros.n; i++)
		if (!macros.v[i].block)
			fprintf(outf, "#macro %s\n", macros.v[i].name);
	fenced = false;
	int expect = 0;
	const char *cur_file = NULL;
	for (size_t i = 0; i < out.n; i++) {
		Line *l = &out.v[i];
		const char *lf = l->file ? l->file : file;
		char *s = l->s;
		diag_file = lf;
		if (is_fence(s))
			fenced = !fenced;
		else if (!fenced)
			s = expand_text(s, l->no, 0);
		if (l->no != expect || cur_file == NULL || !streq(lf, cur_file)) {
			fprintf(outf, "#line %d \"%s\"\n", l->no, lf);
			expect = l->no;
			cur_file = lf;
		}
		fprintf(outf, "%s\n", s);
		expect++;
	}
	diag_file = file;
	return nerrors ? 1 : 0;
}
