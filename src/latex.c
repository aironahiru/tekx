/*
 * latex.c -- tekx import: a LaTeX document in, a tekx document out.
 *
 *	tekx import paper.tex		writes paper.tekx
 *
 * Mathematicians have decades of LaTeX.  This converts the subset real
 * papers use (amsart and article: front matter, sections, theorems,
 * proofs, displays, lists, tables, figures, footnotes, citations and
 * cross-references) into idiomatic tekx.  Formulas stay in LaTeX's
 * notation, which tekx reads, under `notation tex`; user macros are
 * expanded, and \DeclareMathOperator becomes `define operator`.  Whatever
 * cannot be converted is kept as text and reported as file:line:col
 * warnings, so the author knows exactly what to look at.
 */
#include "bib.h"
#include "eqn.h"

#include <ctype.h>
#include <stdarg.h>
#include <strings.h>

typedef VEC(char *) Names;
typedef struct { size_t a, b; } R;

/* ---- the source ---- */

static char *S;		/* comments blanked, macros expanded */
static size_t N;
static const char *path;
static const char *dir = "";	/* the source's directory, for figures and .bib files */

static int
line_of(size_t off)
{
	int line = 1;

	for (size_t i = 0; i < off && i < N; i++)
		line += S[i] == '\n' || S[i] == '\f';
	return line;
}

static int
col_of(size_t off)
{
	size_t i = off;

	while (i > 0 && S[i - 1] != '\n' && S[i - 1] != '\f')
		i--;
	return (int)(off - i) + 1;
}

static void
warn(size_t off, const char *f, ...)
{
	char msg[512];
	va_list ap;

	va_start(ap, f);
	vsnprintf(msg, sizeof msg, f, ap);
	va_end(ap);
	const char *saved = diag_file;
	diag_file = path;
	warn_at(line_of(off), col_of(off), "%s", msg);
	diag_file = saved;
}

/* warnings that would repeat for every occurrence are said once */
static Names said;

static void
warn_once(size_t off, const char *key, const char *msg)
{
	for (size_t i = 0; i < said.n; i++)
		if (streq(said.v[i], key))
			return;
	VPUSH(said, xstrdup(key));
	warn(off, "%s", msg);
}

static bool
has(const Names *v, const char *s)
{
	for (size_t i = 0; i < v->n; i++)
		if (streq(v->v[i], s))
			return true;
	return false;
}

static bool
is_letter(int c)
{
	return (c | 32) >= 'a' && (c | 32) <= 'z';
}

/* the name of the command at i (S[i] == '\\'): letters, or one other character */
static char *
cmd_name(size_t i, size_t *after)
{
	size_t j = i + 1;

	if (j < N && is_letter(S[j])) {
		while (j < N && is_letter(S[j]))
			j++;
	} else if (j < N) {
		j++;
	}
	*after = j;
	return xstrndup(S + i + 1, j - i - 1);
}

static size_t
skip_ws(size_t i, size_t b)
{
	while (i < b && isspace((unsigned char)S[i]))
		i++;
	return i;
}

/* {group} after i: its contents in r; or a single token */
static bool
group(size_t *i, size_t b, R *r)
{
	size_t j = skip_ws(*i, b);

	if (j >= b)
		return false;
	if (S[j] == '{') {
		int depth = 0;
		for (size_t k = j; k < b; k++) {
			if (S[k] == '\\' && k + 1 < b) {
				k++;
				continue;
			}
			if (S[k] == '{')
				depth++;
			else if (S[k] == '}' && --depth == 0) {
				r->a = j + 1;
				r->b = k;
				*i = k + 1;
				return true;
			}
		}
		r->a = j + 1;
		r->b = b;
		*i = b;
		return true;
	}
	if (S[j] == '}' || S[j] == ']')
		return false;
	r->a = j;
	if (S[j] == '\\') {
		size_t e;
		free(cmd_name(j, &e));
		r->b = e;
	} else {
		u32 cp;
		r->b = j + (size_t)utf8_dec(S + j, b - j, &cp);
	}
	*i = r->b;
	return true;
}

/* [optional] after i */
static bool
optional(size_t *i, size_t b, R *r)
{
	size_t j = skip_ws(*i, b);
	int depth = 0;

	if (j >= b || S[j] != '[')
		return false;
	for (size_t k = j + 1; k < b; k++) {
		if (S[k] == '\\' && k + 1 < b) {
			k++;
			continue;
		}
		if (S[k] == '{')
			depth++;
		else if (S[k] == '}')
			depth--;
		else if (S[k] == ']' && depth == 0) {
			r->a = j + 1;
			r->b = k;
			*i = k + 1;
			return true;
		}
	}
	return false;
}

static char *
str(R r)
{
	return xstrndup(S + r.a, r.b - r.a);
}

static bool
at(size_t i, size_t b, const char *s)
{
	size_t n = strlen(s);

	return i + n <= b && strncmp(S + i, s, n) == 0;
}

static bool
at_cmd(size_t i, size_t b, const char *name)
{
	size_t n = strlen(name);

	return i + n + 1 <= b && S[i] == '\\' && strncmp(S + i + 1, name, n) == 0 &&
	    (i + n + 1 >= b || !is_letter(S[i + n + 1]));
}

/* the \end{env} matching a \begin{env} whose body starts at i; the body
 * is [i, *end) and *after is past \end{env} */
static bool
env_end(const char *env, size_t i, size_t b, size_t *end, size_t *after)
{
	char *open = fmt("\\begin{%s}", env), *close = fmt("\\end{%s}", env);
	int depth = 1;

	for (size_t k = i; k < b; k++) {
		if (at(k, b, open)) {
			depth++;
			k += strlen(open) - 1;
		} else if (at(k, b, close) && --depth == 0) {
			*end = k;
			*after = k + strlen(close);
			return true;
		}
	}
	return false;
}

/* ---- comments and macros ---- */

static bool
verbatim_env(const char *env)
{
	return streq(env, "verbatim") || streq(env, "verbatim*") || streq(env, "lstlisting") ||
	    streq(env, "minted") || streq(env, "comment");
}

/* the name in "\begin{name}" at i, or NULL */
static char *
begin_at(const char *s, size_t i, size_t n)
{
	if (i + 7 > n || strncmp(s + i, "\\begin{", 7) != 0)
		return NULL;
	const char *e = memchr(s + i + 7, '}', n - i - 7);
	return e ? xstrndup(s + i + 7, (size_t)(e - s - i - 7)) : NULL;
}

/*
 * Comments become blanks.  A line that held only a comment ends in \f
 * rather than \n: it counts as a line, but does not make a blank line, so
 * it does not break a paragraph (as in LaTeX).  The comment environment
 * and \iffalse ... \fi disappear; verbatim text is left alone.
 */
static void
strip_comments(char *s, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		char *env = begin_at(s, i, n);
		if (env && verbatim_env(env)) {
			char *close = fmt("\\end{%s}", env);
			char *e = strstr(s + i, close);
			size_t stop = e ? (size_t)(e - s) : n;
			if (streq(env, "comment")) {
				for (size_t k = i; k < stop + (e ? strlen(close) : 0) && k < n; k++)
					if (s[k] != '\n')
						s[k] = ' ';
			}
			i = stop + (e ? strlen(close) : 0);
			if (i > 0)
				i--;
			continue;
		}
		if (strncmp(s + i, "\\iffalse", 8) == 0) {
			char *e = strstr(s + i, "\\fi");
			size_t stop = e ? (size_t)(e - s) + 3 : n;
			for (size_t k = i; k < stop; k++)
				if (s[k] != '\n')
					s[k] = ' ';
			i = stop - 1;
			continue;
		}
		if (s[i] == '\\' && i + 1 < n) {
			if (strncmp(s + i, "\\verb", 5) == 0 && i + 5 < n && !is_letter(s[i + 5])) {
				char d = s[i + 5];
				char *e = memchr(s + i + 6, d, n - i - 6);
				i = e ? (size_t)(e - s) : i + 5;
				continue;
			}
			i++;	/* \% is a percent sign */
			continue;
		}
		if (s[i] != '%')
			continue;
		size_t ls = i;
		while (ls > 0 && s[ls - 1] != '\n' && s[ls - 1] != '\f')
			ls--;
		bool alone = true;
		for (size_t k = ls; k < i; k++)
			alone &= isspace((unsigned char)s[k]) != 0;
		size_t k = i;
		while (k < n && s[k] != '\n')
			s[k++] = ' ';
		if (alone && k < n)
			s[k] = '\f';
		i = k;
	}
}

typedef struct {
	char *name;	/* without the backslash */
	int nargs;
	char *opt;	/* default of an optional first argument, or NULL */
	char *body;
	bool recursive;	/* its body names itself: never expanded */
} Macro;

static VEC(Macro) macros;

typedef struct {
	char *env, *kind, *title;
	bool numbered;
} Thm;

static VEC(Thm) thms;
static Names operators;		/* \DeclareMathOperator{\Var}{Var}: define operator Var */
static bool by_section, eq_by_section;

static const struct { const char *kind, *title; int style; } builtin[] = {
	{"theorem", "Theorem", 0}, {"lemma", "Lemma", 0}, {"proposition", "Proposition", 0},
	{"corollary", "Corollary", 0}, {"conjecture", "Conjecture", 0}, {"claim", "Claim", 0},
	{"axiom", "Axiom", 0}, {"hypothesis", "Hypothesis", 0}, {"fact", "Fact", 0},
	{"definition", "Definition", 1}, {"example", "Example", 1}, {"exercise", "Exercise", 1},
	{"problem", "Problem", 1}, {"solution", "Solution", 1}, {"notation", "Notation", 1},
	{"assumption", "Assumption", 1}, {"remark", "Remark", 2}, {"note", "Note", 2},
	{"observation", "Observation", 2},
};

static Buf defines;		/* define lines for the output */

static Thm *
find_thm(const char *env)
{
	for (size_t i = 0; i < thms.n; i++)
		if (streq(thms.v[i].env, env))
			return &thms.v[i];
	return NULL;
}

static void
blank_range(size_t a, size_t b)
{
	for (size_t k = a; k < b && k < N; k++)
		if (S[k] != '\n' && S[k] != '\f')
			S[k] = ' ';
}

static char *
plain_word(const char *s)
{
	Buf b = {0};

	for (; *s; s++)
		if (isalnum((unsigned char)*s))
			bputc(&b, tolower((unsigned char)*s));
	return bstr(&b);
}

static void
new_theorem(size_t at_off, const char *env, const char *title, bool numbered, int style, const char *within)
{
	Thm t = {xstrdup(env), NULL, xstrdup(title), numbered};

	if (within && streq(within, "section"))
		by_section = true;
	if (!numbered)
		warn_once(at_off, "unnumbered", "tekx numbers every theorem-like block; unnumbered "
		    "\\newtheorem* environments will be numbered");
	for (size_t i = 0; i < NELEM(builtin); i++)
		if (strcasecmp(builtin[i].title, title) == 0)
			t.kind = xstrdup(builtin[i].kind);
	if (t.kind == NULL) {
		/* a kind of its own: define NAME like theorem titled "Title" */
		static const char *base[] = {"theorem", "definition", "remark"};
		char *name = plain_word(env);
		for (size_t i = 0; i < NELEM(builtin); i++)
			if (streq(builtin[i].kind, name))
				name = fmt("%s%s", name, plain_word(title));
		if (!*name)
			name = "result";
		t.kind = name;
		bprintf(&defines, "define %s like %s titled \"%s\"\n", name, base[style], title);
	}
	VPUSH(thms, t);
}

/*
 * The preamble's definitions: \newcommand and friends (kept, to expand),
 * \DeclareMathOperator, \newtheorem with \theoremstyle, \numberwithin.
 * Each definition is blanked from the source once read.
 */
static void
read_definitions(void)
{
	int style = 0;

	for (size_t i = 0; i < N; i++) {
		if (S[i] != '\\')
			continue;
		char *env = begin_at(S, i, N);
		if (env && verbatim_env(env)) {
			char *e = strstr(S + i, fmt("\\end{%s}", env));
			if (e)
				i = (size_t)(e - S);
			continue;
		}
		size_t j;
		char *name = cmd_name(i, &j);
		bool star = j < N && S[j] == '*';
		if (star)
			j++;
		R r, o;
		if (streq(name, "newcommand") || streq(name, "renewcommand") || streq(name, "providecommand")) {
			R nm, body;
			if (!group(&j, N, &nm))
				continue;
			char *mname = str(nm);
			if (mname[0] != '\\')
				continue;
			Macro m = {xstrdup(mname + 1), 0, NULL, NULL, false};
			if (optional(&j, N, &o))
				m.nargs = atoi(str(o));
			if (optional(&j, N, &o))
				m.opt = str(o);
			if (!group(&j, N, &body))
				continue;
			m.body = str(body);
			for (char *p = m.body; *p; p++)
				if (*p == '\n' || *p == '\f')
					*p = ' ';
			VPUSH(macros, m);
			blank_range(i, j);
			i = j - 1;
		} else if (streq(name, "def")) {
			size_t k = j;
			if (k >= N || S[k] != '\\')
				continue;
			size_t e;
			char *mname = cmd_name(k, &e);
			int nargs = 0;
			while (e + 1 < N && S[e] == '#' && isdigit((unsigned char)S[e + 1])) {
				nargs++;
				e += 2;
			}
			R body;
			if (!group(&e, N, &body))
				continue;
			Macro m = {mname, nargs, NULL, str(body), false};
			VPUSH(macros, m);
			blank_range(i, e);
			i = e - 1;
		} else if (streq(name, "DeclareMathOperator")) {
			R nm, txt;
			if (!group(&j, N, &nm) || !group(&j, N, &txt))
				continue;
			char *mname = str(nm), *text = trim(str(txt));
			bool plain = mname[0] == '\\' && !star;
			for (char *p = text; *p && plain; p++)
				plain = is_letter(*p);
			if (plain && streq(mname + 1, text)) {
				VPUSH(operators, xstrdup(text));
			} else {
				/* \DeclareMathOperator{\Tr}{tr}: expand to \operatorname{tr} */
				Macro m = {xstrdup(mname + 1), 0, NULL, fmt("\\operatorname{%s}", text), false};
				VPUSH(macros, m);
				if (star)
					warn(i, "\\DeclareMathOperator* (limits under %s) is written as \\operatorname", text);
			}
			blank_range(i, j);
			i = j - 1;
		} else if (streq(name, "theoremstyle")) {
			if (!group(&j, N, &r))
				continue;
			char *st = str(r);
			style = streq(st, "definition") ? 1 : streq(st, "remark") ? 2 : 0;
			blank_range(i, j);
			i = j - 1;
		} else if (streq(name, "newtheorem")) {
			R e, t;
			if (!group(&j, N, &e))
				continue;
			bool shared = optional(&j, N, &o);
			if (!group(&j, N, &t))
				continue;
			char *within = NULL;
			if (!shared && optional(&j, N, &o))
				within = str(o);
			new_theorem(i, str(e), str(t), !star, style, within);
			blank_range(i, j);
			i = j - 1;
		} else if (streq(name, "numberwithin")) {
			R a, b;
			if (group(&j, N, &a) && group(&j, N, &b)) {
				if (streq(str(a), "equation") && streq(str(b), "section"))
					eq_by_section = true;
				blank_range(i, j);
				i = j - 1;
			}
		} else if (streq(name, "newenvironment") || streq(name, "renewenvironment")) {
			R nm;
			if (group(&j, N, &nm))
				warn(i, "\\newenvironment{%s} is not converted: its contents are kept as they are", str(nm));
		}
		free(name);
	}
	if (by_section != eq_by_section && (by_section || eq_by_section) && thms.n)
		warn(0, "tekx numbers theorems and equations alike: both will be numbered by section");
	by_section |= eq_by_section;
	/* a macro that names itself would expand for ever */
	for (size_t i = 0; i < macros.n; i++) {
		Macro *m = &macros.v[i];
		size_t n = strlen(m->name);
		for (const char *p = m->body; (p = strchr(p, '\\')) != NULL; p++)
			if (strncmp(p + 1, m->name, n) == 0 && !is_letter(p[1 + n])) {
				m->recursive = true;
				warn(0, "\\%s uses itself: it is left unexpanded", m->name);
				break;
			}
	}
}

static Macro *
find_macro(const char *name)
{
	for (size_t i = macros.n; i-- > 0;)
		if (streq(macros.v[i].name, name))
			return &macros.v[i];
	return NULL;
}

/* one pass of macro expansion; returns whether anything changed */
static bool
expand_pass(void)
{
	Buf o = {0};
	bool changed = false;
	size_t cap = 8 * N + (1 << 20);	/* macros that multiply each other stop here */

	for (size_t i = 0; i < N;) {
		if (o.n > cap) {
			warn(0, "macro expansion stopped: the document grew past %zu bytes", cap);
			bput(&o, S + i, N - i);
			S = bstr(&o);
			N = o.n;
			return false;
		}
		char *env = begin_at(S, i, N);
		if (env && verbatim_env(env)) {
			char *e = strstr(S + i, fmt("\\end{%s}", env));
			size_t stop = e ? (size_t)(e - S) : N;
			bput(&o, S + i, stop - i);
			i = stop;
			if (e) {
				bput(&o, S + i, 1);
				i++;
			}
			continue;
		}
		if (S[i] != '\\' || i + 1 >= N) {
			bputc(&o, S[i++]);
			continue;
		}
		size_t j;
		char *name = cmd_name(i, &j);
		Macro *m = find_macro(name);
		if (m == NULL || m->recursive) {
			bput(&o, S + i, j - i);
			i = j;
			continue;
		}
		char *args[10] = {0};
		int k = 0;
		R r;
		if (m->opt) {
			args[k++] = optional(&j, N, &r) ? str(r) : m->opt;
		}
		for (; k < m->nargs && k < 9; k++)
			args[k] = group(&j, N, &r) ? str(r) : "";
		for (const char *p = m->body; *p; p++) {
			if (*p == '#' && p[1] >= '1' && p[1] <= '9') {
				int n = p[1] - '1';
				bputs(&o, n < m->nargs && args[n] ? args[n] : "");
				p++;
			} else {
				bputc(&o, *p);
			}
		}
		/* \R x must not become \mathbb{R}x run together with a letter */
		if (m->nargs == 0 && j < N && is_letter(S[j]) && o.n && is_letter(o.s[o.n - 1]))
			bputc(&o, ' ');
		i = j;
		changed = true;
	}
	S = bstr(&o);
	N = o.n;
	return changed;
}

/* ---- output ---- */

static Buf body;		/* the converted body */
static Buf para;		/* the paragraph being gathered */
static bool last_blank = true;
static Names attached;		/* labels that have a target in the output */
static Names cite_keys;
static int auto_eq;

static void
emit(int ind, const char *s)
{
	bprintf(&body, "%*s%s\n", ind, "", s);
	last_blank = false;
}

static void
blank(void)
{
	if (!last_blank)
		bputc(&body, '\n');
	last_blank = true;
}

/* a label as tekx allows it: letters, digits, - _ :, not ending in - or : */
static char *
label_name(const char *l)
{
	Buf b = {0};

	for (const char *p = l; *p; p++)
		bputc(&b, isalnum((unsigned char)*p) || *p == '_' || *p == ':' || *p == '-' ? *p : '-');
	char *s = bstr(&b);
	size_t n = strlen(s);
	while (n > 0 && (s[n - 1] == '-' || s[n - 1] == ':'))
		s[--n] = 0;
	return *s ? s : xstrdup("label");
}

/* what each target is called, "Lemma", "Section", or NULL for an equation:
 * tekx's references say it, so "Lemma~\ref{x}" must not say it twice */
typedef struct {
	char *name, *kind;
} Target;

static VEC(Target) targets;

/* labels and references are resolved at the end, when every target is
 * known: \x01L name \x02 marks a target, \x01R name \x02 a reference */
static char *
label_mark(const char *l, const char *kind)
{
	char *n = label_name(l);

	if (!has(&attached, n)) {
		VPUSH(attached, n);
		Target t = {n, kind ? xstrdup(kind) : NULL};
		VPUSH(targets, t);
	}
	return fmt("\x01L%s\x02", n);
}

static const char *
target_kind(const char *n)
{
	for (size_t i = 0; i < targets.n; i++)
		if (streq(targets.v[i].name, n))
			return targets.v[i].kind;
	return NULL;
}

/* does o end with the word w (case aside), as a whole word and a space? */
static bool
drop_word(Buf *o, const char *w)
{
	size_t n = o->n, k = strlen(w);

	while (n > 0 && o->s[n - 1] == ' ')
		n--;
	if (n < k || strncasecmp(o->s + n - k, w, k) != 0)
		return false;
	if (n > k && (isalnum((unsigned char)o->s[n - k - 1]) || o->s[n - k - 1] == '*'))
		return false;
	o->n = n - k;
	return true;
}

/* ---- text ---- */

static bool in_emph;
static int plain_mode;		/* front matter: no markup, formulas as their source */

static void text(Buf *o, size_t a, size_t b);
static void blocks(size_t a, size_t b, int ind);

static void
put_char(Buf *o, char c)
{
	if (strchr("*@[]^<{}$\\`", c) && !plain_mode)
		bputc(o, '\\');
	bputc(o, c);
}

/* spaces collapse; the paragraph is trimmed when it is set */
static void
put_space(Buf *o)
{
	if (o->n && o->s[o->n - 1] != ' ')
		bputc(o, ' ');
}

static bool label_only;	/* looking, not converting: say nothing */

/* a formula's source, ready for tekx: one line, \label and \nonumber out */
static char *
math_source(size_t a, size_t b, char **label, bool *nonumber)
{
	Buf m = {0};

	if (label)
		*label = NULL;
	if (nonumber)
		*nonumber = false;
	for (size_t i = a; i < b; i++) {
		if (S[i] == '\\' && i + 1 < b && is_letter(S[i + 1])) {
			size_t j;
			char *name = cmd_name(i, &j);
			R r;
			if (streq(name, "label")) {
				if (group(&j, b, &r) && label && *label == NULL)
					*label = str(r);
				i = j - 1;
				continue;
			}
			if (streq(name, "nonumber") || streq(name, "notag")) {
				if (nonumber)
					*nonumber = true;
				i = j - 1;
				continue;
			}
			if (streq(name, "tag")) {
				size_t k = j;
				if (k < b && S[k] == '*')
					k++;
				if (group(&k, b, &r) && !label_only)
					warn(i, "\\tag{%s} is not supported: the equation gets tekx's own number", str(r));
				i = k - 1;
				continue;
			}
			if (streq(name, "qedhere")) {
				i = j - 1;
				continue;
			}
			bput(&m, S + i, j - i);
			i = j - 1;
			continue;
		}
		if (S[i] == '\\' && i + 1 < b) {
			bput(&m, S + i, 2);
			i++;
			continue;
		}
		if (isspace((unsigned char)S[i])) {
			put_space(&m);
			continue;
		}
		bputc(&m, S[i]);
	}
	char *s = trim(bstr(&m));
	size_t n = strlen(s);
	while (n > 0 && s[n - 1] == '\\' && (n < 2 || s[n - 2] != '\\'))
		s[--n] = 0;	/* "x\ " at the end of a formula */
	return trim(s);
}

/* check a formula as tekx will read it; warn in its words if it will not */
static void
check_math(size_t off, const char *src, bool display)
{
	Buf msgs = {0};
	int before = nerrors;

	diag_sink = &msgs;
	eqn_parse(src, line_of(off), col_of(off), display, NULL, 0);
	diag_sink = NULL;
	if (nerrors > before) {
		nerrors = before;
		warn(off, "tekx will not read this formula as it stands: %s", bstr(&msgs));
	}
}

static void
inline_math(Buf *o, size_t a, size_t b)
{
	char *m = math_source(a, b, NULL, NULL);

	if (!*m)
		return;
	if (plain_mode) {
		/* titles and alt text: the formula as readable Unicode, y = x² */
		Buf quiet = {0};
		int before = nerrors;
		diag_sink = &quiet;
		M *mm = eqn_parse(m, 0, 0, false, NULL, 0);
		diag_sink = NULL;
		bputs(o, nerrors > before ? m : eqn_linear(mm));
		nerrors = before;
		return;
	}
	check_math(a, m, false);
	bprintf(o, "$%s$", m);
	/* tekx's closing $ must not be followed by a digit */
	if (b + 1 < N && isdigit((unsigned char)S[b + 1]))
		bputc(o, '\\');
}

static void
reference(Buf *o, const char *labels, bool multi, size_t at_off)
{
	/* \cref{a,b}: "@a and @b" */
	char *t = xstrdup(labels);
	int n = 0;

	if (o->n && o->s[o->n - 1] == '~')
		o->s[o->n - 1] = ' ';	/* "Theorem~\ref" reads "Theorem @x" */
	if (o->n && (isalnum((unsigned char)o->s[o->n - 1]) || o->s[o->n - 1] == '_'))
		bputc(o, ' ');
	for (char *k = strtok(t, ","); k; k = strtok(NULL, ",")) {
		if (n++)
			bputs(o, " and ");
		/* where it stands, for a warning if it turns out to have no target */
		bprintf(o, "\x01R%s\x03%zu\x02", label_name(trim(k)), at_off);
		if (!multi)
			break;
	}
}

static void
citation(Buf *o, size_t at_off, const char *keys, const char *note)
{
	char *t = xstrdup(keys);
	int n = 0;

	if (plain_mode)
		return;
	if (o->n && o->s[o->n - 1] == '~')
		o->s[o->n - 1] = ' ';
	bputc(o, '[');
	for (char *k = strtok(t, ","); k; k = strtok(NULL, ",")) {
		k = trim(k);
		if (!*k)
			continue;
		bprintf(o, "%s@%s", n++ ? "; " : "", k);
		if (!has(&cite_keys, k))
			VPUSH(cite_keys, xstrdup(k));
	}
	if (note && *note)
		bprintf(o, ", %s", note);
	bputc(o, ']');
	(void)at_off;
}

static char *
text_str(size_t a, size_t b)
{
	Buf t = {0};

	text(&t, a, b);
	return trim(bstr(&t));
}

static char *
plain_str(R r)
{
	plain_mode++;
	char *s = text_str(r.a, r.b);
	plain_mode--;
	return s;
}

static const struct { const char *name, *text; } text_symbols[] = {
	{"ss", "ß"}, {"o", "ø"}, {"O", "Ø"}, {"ae", "æ"}, {"AE", "Æ"}, {"oe", "œ"}, {"OE", "Œ"},
	{"aa", "å"}, {"AA", "Å"}, {"l", "ł"}, {"L", "Ł"}, {"i", "ı"}, {"j", "ȷ"},
	{"S", "§"}, {"P", "¶"}, {"dag", "†"}, {"ddag", "‡"}, {"copyright", "©"},
	{"textregistered", "®"}, {"texttrademark", "™"}, {"textdegree", "°"},
	{"ldots", "..."}, {"dots", "..."}, {"textellipsis", "..."}, {"textendash", "–"},
	{"textemdash", "—"}, {"textquoteleft", "‘"}, {"textquoteright", "’"},
	{"textquotedblleft", "“"}, {"textquotedblright", "”"}, {"LaTeX", "LaTeX"}, {"TeX", "TeX"},
	{"BibTeX", "BibTeX"}, {"AmS", "AMS"}, {"textbackslash", "\\\\"}, {"textbar", "|"},
	{"textless", "<"}, {"textgreater", ">"}, {"textasciitilde", "~"},
};

/* commands that only steer layout: nothing to carry over */
static const char *ignored[] = {
	"noindent", "indent", "medskip", "smallskip", "bigskip", "centering", "raggedright",
	"raggedleft", "small", "large", "Large", "LARGE", "huge", "Huge", "footnotesize", "scriptsize",
	"tiny", "normalsize", "hfill", "vfill", "relax", "protect", "qed", "qedhere", "nolinebreak",
	"nopagebreak", "allowbreak", "sloppy", "fussy", "frenchspacing", "selectfont", "normalfont",
	"maketitle", "bibliographystyle", "label", "index", "phantomsection", "leavevmode", "null",
	"strut", "displaystyle", "hline", "toprule", "midrule", "bottomrule", "par", "/", "-",
	"clearpage", "newpage", "pagebreak", "linebreak", "newline", "\\", "bf", "em", "it", "sl",
	"sc", "tt", "rm", "sf", "upshape", "itshape", "bfseries", "mdseries", "scshape", "ttfamily",
	"rmfamily", "sffamily", "Huge", "boldmath", "unboldmath", "item",
};

static bool
is_ignored(const char *name)
{
	for (size_t i = 0; i < NELEM(ignored); i++)
		if (streq(ignored[i], name))
			return true;
	return false;
}

/* setup and spacing commands, with the number of {arguments} they take; -1 if not one */
static int
layout_args(const char *name)
{
	static const struct { const char *name; int n; } cmds[] = {
		{"hspace", 1}, {"vspace", 1}, {"setlength", 2}, {"addtolength", 2}, {"setcounter", 2},
		{"addtocounter", 2}, {"pagestyle", 1}, {"thispagestyle", 1}, {"graphicspath", 1},
		{"addcontentsline", 3}, {"markboth", 2}, {"markright", 1}, {"hypersetup", 1},
		{"usepackage", 1}, {"documentclass", 1}, {"RequirePackage", 1}, {"geometry", 1},
		{"newlength", 1}, {"newcounter", 1}, {"crefname", 3}, {"Crefname", 3},
		{"numberwithin", 2}, {"allowdisplaybreaks", 0}, {"renewenvironment", 3},
		{"newenvironment", 3}, {"vskip", 0}, {"hskip", 0}, {"kern", 0}, {"title", 1},
		{"settowidth", 2}, {"captionsetup", 1}, {"DeclareGraphicsExtensions", 1},
	};

	for (size_t i = 0; i < NELEM(cmds); i++)
		if (streq(cmds[i].name, name))
			return cmds[i].n;
	return -1;
}

/* a {braced} argument only, never a bare token */
static bool
brace(size_t *i, size_t b, R *r)
{
	size_t j = skip_ws(*i, b);

	if (j >= b || S[j] != '{')
		return false;
	*i = j;
	return group(i, b, r);
}

/* a command inside running text, at i; converted into o, and *i moved past it */
static void
inline_cmd(Buf *o, size_t *ip, size_t b)
{
	size_t i = *ip, j;
	char *name = cmd_name(i, &j);
	R r, opt;

	*ip = j;
	/* \H{o}, \v{s}, \c{c}: accents named by a letter */
	bool letter_accent = name[0] && !name[1] && strchr("uvHckrbd", name[0]);
	if (!is_letter(name[0]) || letter_accent) {
		char c = name[0];
		if (strchr("\"'`^~=.uvHckrbd", c)) {
			/* an accent: \"u, \'{e} */
			if (group(ip, b, &r)) {
				char *arg = str(r);
				u32 base = 0;
				if (streq(arg, "\\i") || streq(arg, "i"))
					base = streq(arg, "i") ? 'i' : 0x131;
				else
					utf8_dec(arg, strlen(arg), &base);
				u32 cp = tex_accent(c, base);
				if (cp)
					utf8_enc(o, cp);
				else
					bputs(o, arg);
			}
			return;
		}
		if (c == '\\' || c == ',' || c == ';' || c == ':' || c == ' ' || c == '>' || c == '\n') {
			put_space(o);
		} else if (c == '[' || c == '(') {
			/* \[ ... \] or \( ... \) inside running text: inline */
			const char *close = c == '[' ? "\\]" : "\\)";
			size_t k = *ip;
			while (k < b && !at(k, b, close))
				k++;
			inline_math(o, *ip, k);
			*ip = k + 2 <= b ? k + 2 : b;
		} else if (strchr("&%$#_{}", c)) {
			put_char(o, c);
		}
		return;
	}
	if (j < b && S[j] == '*' && (streq(name, "section") || streq(name, "cite") || streq(name, "url")))
		(*ip)++;
	for (size_t k = 0; k < NELEM(text_symbols); k++)
		if (streq(text_symbols[k].name, name)) {
			bputs(o, text_symbols[k].text);
			if (*ip + 1 < b && S[*ip] == '{' && S[*ip + 1] == '}')
				*ip += 2;
			else if (*ip < b && S[*ip] == ' ')
				bputc(o, ' ');
			return;
		}
	if (streq(name, "emph") || streq(name, "textit") || streq(name, "textsl")) {
		if (!group(ip, b, &r))
			return;
		/* tekx emphasis does not nest: the inner one is left plain */
		bool saved = in_emph, outer = !in_emph && !plain_mode;
		in_emph = true;
		char *t = text_str(r.a, r.b);
		in_emph = saved;
		bputs(o, outer && *t ? fmt("*%s*", t) : t);
	} else if (streq(name, "textbf")) {
		if (group(ip, b, &r)) {
			char *t = text_str(r.a, r.b);
			bputs(o, !plain_mode && *t ? fmt("**%s**", t) : t);
		}
	} else if (streq(name, "texttt") || streq(name, "verb")) {
		if (streq(name, "verb")) {
			if (*ip >= b)
				return;
			char d = S[*ip];
			size_t k = *ip + 1;
			while (k < b && S[k] != d)
				k++;
			r.a = *ip + 1;
			r.b = k;
			*ip = k < b ? k + 1 : b;
		} else if (!group(ip, b, &r)) {
			return;
		}
		char *t = plain_str(r);
		for (char *p = t; *p; p++)
			if (*p == '`')
				*p = '\'';
		bprintf(o, plain_mode ? "%s" : "`%s`", t);
	} else if (streq(name, "textsc") || streq(name, "textrm") || streq(name, "textup") ||
	    streq(name, "textnormal") || streq(name, "textsf") || streq(name, "mbox") || streq(name, "text") ||
	    streq(name, "textmd") || streq(name, "hbox") || streq(name, "underline") || streq(name, "uppercase")) {
		if (group(ip, b, &r))
			text(o, r.a, r.b);
	} else if (streq(name, "textsuperscript") || streq(name, "textsubscript")) {
		if (group(ip, b, &r)) {
			warn(i, "\\%s{%s} is set as plain text", name, str(r));
			text(o, r.a, r.b);
		}
	} else if (streq(name, "cite") || streq(name, "citep") || streq(name, "citet") || streq(name, "parencite") ||
	    streq(name, "autocite") || streq(name, "textcite") || streq(name, "citealp")) {
		R o2;
		bool has1 = optional(ip, b, &opt), has2 = has1 && optional(ip, b, &o2);
		if (!group(ip, b, &r))
			return;
		if (has2) {
			warn(i, "a citation's prenote is dropped: tekx citations have a note after the key");
			opt = o2;
		}
		if (streq(name, "citet") || streq(name, "textcite"))
			warn_once(i, "citet", "\\citet (author in text) is written as a numbered citation, [@key]");
		citation(o, i, str(r), has1 ? text_str(opt.a, opt.b) : NULL);
	} else if (streq(name, "ref") || streq(name, "eqref") || streq(name, "autoref") || streq(name, "cref") ||
	    streq(name, "Cref") || streq(name, "pageref") || streq(name, "nameref") || streq(name, "vref") ||
	    streq(name, "Autoref") || streq(name, "crefrange") || streq(name, "labelcref")) {
		if (!group(ip, b, &r))
			return;
		if (streq(name, "pageref") || streq(name, "nameref"))
			warn(i, "\\%s{%s} becomes an ordinary reference (\"Theorem 2\")", name, str(r));
		if (plain_mode)
			return;
		reference(o, str(r), name[0] == 'c' || name[0] == 'C' || name[0] == 'l', i);
	} else if (streq(name, "footnote")) {
		optional(ip, b, &opt);
		if (group(ip, b, &r) && !plain_mode)
			bprintf(o, "^[%s]", text_str(r.a, r.b));
	} else if (streq(name, "footnotemark") || streq(name, "footnotetext") || streq(name, "thanks")) {
		optional(ip, b, &opt);
		char *t = brace(ip, b, &r) ? plain_str(r) : "";
		if (*t)
			warn(i, "\\%s is not supported: \"%s\" is left out", name, t);
		else
			warn(i, "\\%s is not supported: it is left out", name);
	} else if (streq(name, "url") || streq(name, "nolinkurl")) {
		if (group(ip, b, &r)) {
			char *u = str(r);
			bprintf(o, plain_mode ? "%s" : startswith(u, "http") ? "<%s>" : "`%s`", u);
		}
	} else if (streq(name, "href")) {
		R t;
		if (group(ip, b, &r) && group(ip, b, &t)) {
			Buf u = {0};
			for (const char *p = str(r); *p; p++) {
				if (*p == '(' || *p == ')' || *p == ' ')
					bprintf(&u, "%%%02X", (unsigned char)*p);
				else if (*p != '\\')
					bputc(&u, *p);
			}
			char *tx = text_str(t.a, t.b);
			bprintf(o, plain_mode ? "%s" : "[%s](%s)", tx, bstr(&u));
		}
	} else if (streq(name, "ensuremath")) {
		if (group(ip, b, &r))
			inline_math(o, r.a, r.b);
	} else if (streq(name, "today")) {
		bputs(o, plain_mode ? "today" : "{today}");
	} else if (streq(name, "quad") || streq(name, "qquad") || streq(name, "enspace") ||
	    streq(name, "thinspace") || streq(name, "space") || streq(name, "nobreakspace")) {
		put_space(o);
	} else if (layout_args(name) >= 0) {
		/* layout and setup: their arguments are skipped */
		if (*ip < b && S[*ip] == '*')
			(*ip)++;
		optional(ip, b, &opt);
		for (int k = layout_args(name); k > 0 && brace(ip, b, &r); k--)
			;
		optional(ip, b, &opt);
	} else if (is_ignored(name)) {
		if (streq(name, "label") && group(ip, b, &r))
			warn(i, "\\label{%s} here has no target in tekx; references to it will read ??", str(r));
		if (streq(name, "\\") || streq(name, "newline") || streq(name, "linebreak"))
			put_space(o);
		if (*ip < b && S[*ip] == '*')
			(*ip)++;
	} else {
		/* said once per command: a document may use one a hundred times */
		warn_once(i, fmt("cmd:%s", name), fmt("unknown command \\%s: its text is kept", name));
		while (optional(ip, b, &opt))
			;
		while (brace(ip, b, &r))
			text(o, r.a, r.b);
	}
}

/* running text [a, b) into o */
static void
text(Buf *o, size_t a, size_t b)
{
	for (size_t i = a; i < b;) {
		char c = S[i];
		if (c == '\\') {
			inline_cmd(o, &i, b);
			continue;
		}
		if (c == '$') {
			bool dd = i + 1 < b && S[i + 1] == '$';
			size_t k = i + (dd ? 2 : 1);
			while (k < b && !(S[k] == '$' && S[k - 1] != '\\'))
				k++;
			inline_math(o, i + (dd ? 2 : 1), k);
			i = k + (dd ? 2 : 1);
			if (i > b)
				i = b;
			continue;
		}
		if (c == '{') {
			R r;
			size_t k = i;
			if (group(&k, b, &r)) {
				/* {\em x}, {\bf x}, {\it x}: old declarations */
				size_t s = skip_ws(r.a, r.b);
				if (at_cmd(s, r.b, "em") || at_cmd(s, r.b, "it") || at_cmd(s, r.b, "sl") ||
				    at_cmd(s, r.b, "itshape")) {
					size_t e;
					free(cmd_name(s, &e));
					char *t = text_str(e, r.b);
					bputs(o, !plain_mode && !in_emph && *t ? fmt("*%s*", t) : t);
				} else if (at_cmd(s, r.b, "bf") || at_cmd(s, r.b, "bfseries")) {
					size_t e;
					free(cmd_name(s, &e));
					char *t = text_str(e, r.b);
					bputs(o, !plain_mode && *t ? fmt("**%s**", t) : t);
				} else {
					text(o, r.a, r.b);
				}
				i = k;
				continue;
			}
		}
		if (c == '}') {
			i++;
			continue;
		}
		if (isspace((unsigned char)c)) {
			put_space(o);
			i++;
			continue;
		}
		if (c == '~') {
			bputc(o, '~');
			i++;
			continue;
		}
		if (at(i, b, "---")) {
			bputs(o, "—");
			i += 3;
			continue;
		}
		if (at(i, b, "--")) {
			bputs(o, "–");	/* written out: a reference may follow, @a–@b */
			i += 2;
			continue;
		}
		if (at(i, b, "``") || at(i, b, "''")) {
			bputc(o, '"');
			i += 2;
			continue;
		}
		if (c == '`') {
			bputc(o, '\'');
			i++;
			continue;
		}
		if (c == '/' && i + 1 < b && S[i + 1] == '/' && (o->n == 0 || o->s[o->n - 1] == ' ')) {
			bputs(o, "/\\/");	/* " //" would start a tekx comment */
			i += 2;
			continue;
		}
		put_char(o, c);
		i++;
	}
}

/* ---- paragraphs ---- */

static bool
risky_start(const char *w)
{
	static const char *kw[] = {"section", "subsection", "subsubsection", "section*", "subsection*",
	    "subsubsection*", "abstract", "proof", "quote", "define", "references", "bibliography",
	    "contents", "newpage", "pagebreak", "env", "figure", "let", "appendix", "title", "subtitle",
	    "author", "date", "language", "lang", "keywords", "subject", "paper", "size", "font", "msc",
	    "numbering", "notation", "document", "use", "do", "end"};
	size_t n = strcspn(w, " ");

	/* what tekx reads at the start of a line as a list, table, quote,
	 * heading, code or display */
	if ((strchr("-+*", w[0]) && w[1] == ' ') || (w[0] == '>' && (w[1] == ' ' || !w[1])) ||
	    w[0] == '|' || (w[0] == '#' && (w[1] == ' ' || w[1] == '#')) || startswith(w, "```") ||
	    startswith(w, "$$") || startswith(w, "//"))
		return true;
	size_t d = strspn(w, "0123456789");
	if (d > 0 && (w[d] == '.' || w[d] == ')') && w[d + 1] == ' ')
		return true;
	for (size_t i = 0; i < NELEM(kw); i++)
		if (strlen(kw[i]) == n && strncmp(kw[i], w, n) == 0)
			return true;
	for (size_t i = 0; i < NELEM(builtin); i++)
		if (strlen(builtin[i].kind) == n && strncmp(builtin[i].kind, w, n) == 0)
			return true;
	for (size_t i = 0; i < thms.n; i++)
		if (strlen(thms.v[i].kind) == n && strncmp(thms.v[i].kind, w, n) == 0)
			return true;
	return false;
}

/* set the gathered paragraph at indent ind, wrapped near 78 columns,
 * never inside a formula and never before a word tekx would read as the
 * start of a block */
static void
flush(int ind)
{
	char *s = trim(bstr(&para));
	Buf line = {0};

	if (!*s) {
		breset(&para);
		return;
	}
	if (risky_start(s))
		bputc(&line, '\\');
	bool math = false;
	size_t last_break = 0;
	for (const char *p = s; *p; p++) {
		if (*p == '$' && (p == s || p[-1] != '\\'))
			math = !math;
		if (*p == ' ' && !math && line.n > (size_t)(78 - ind) && !risky_start(p + 1) && last_break < line.n) {
			emit(ind, bstr(&line));
			breset(&line);
			last_break = 0;
			continue;
		}
		bputc(&line, *p);
	}
	if (line.n)
		emit(ind, bstr(&line));
	blank();
	breset(&para);
}

/* ---- front matter ---- */

typedef struct {
	char *name;
	Names affil;
	char *email, *url;
} Author;

static VEC(Author) authors;
static char *title, *date, *msc, *keywords;
static Names bibfiles;
static bool appendix_seen;

static char *
quoted(const char *s)
{
	return fmt("\"%s\"", s);
}

/* a value among Ruby-style arguments, where \ escapes the next character */
static char *
arg_quoted(const char *s)
{
	Buf b = {0};

	bputc(&b, '"');
	for (; *s; s++) {
		if (*s == '"' || *s == '\\')
			bputc(&b, '\\');
		bputc(&b, *s);
	}
	bputc(&b, '"');
	return bstr(&b);
}

/* \author{A \and B}, or article's \author{Name \\ Affiliation \\ email} */
static void
author_cmd(R r)
{
	char *raw = str(r);
	char *p = raw;

	for (;;) {
		char *and = strstr(p, "\\and");
		if (and)
			*and = 0;
		Author a = {NULL, {0}, NULL, NULL};
		int line = 0;
		for (char *q = p;; line++) {
			char *br = strstr(q, "\\\\");
			if (br)
				*br = 0;
			/* converted alone: the pieces are plain text */
			size_t saved_n = N;
			char *saved_s = S;
			S = q;
			N = strlen(q);
			R whole = {0, N};
			char *t = plain_str(whole);
			S = saved_s;
			N = saved_n;
			if (line == 0)
				a.name = t;
			else if (strchr(t, '@') && !strchr(t, ' '))
				a.email = t;
			else if (*t)
				VPUSH(a.affil, t);
			if (!br)
				break;
			q = br + 2;
		}
		if (a.name && *a.name)
			VPUSH(authors, a);
		if (!and)
			break;
		p = and + 4;
	}
}

static Author *
last_author(size_t off, const char *what)
{
	if (authors.n == 0) {
		warn(off, "\\%s before any \\author is left out", what);
		return NULL;
	}
	return &authors.v[authors.n - 1];
}

/* the commands of a title page, wherever they stand; true if handled */
static bool
front_cmd(const char *name, size_t i, size_t *ip, size_t b)
{
	R r, opt;

	if (streq(name, "title")) {
		optional(ip, b, &opt);
		if (group(ip, b, &r))
			title = plain_str(r);
	} else if (streq(name, "author")) {
		optional(ip, b, &opt);
		if (group(ip, b, &r))
			author_cmd(r);
	} else if (streq(name, "address") || streq(name, "curraddr") || streq(name, "affiliation") ||
	    streq(name, "affil") || streq(name, "institute")) {
		optional(ip, b, &opt);
		if (group(ip, b, &r)) {
			Author *a = last_author(i, name);
			char *t = plain_str(r);
			for (char *p = t; *p; p++)
				if (*p == '\n')
					*p = ' ';
			if (a)
				VPUSH(a->affil, t);
		}
	} else if (streq(name, "email")) {
		optional(ip, b, &opt);
		if (group(ip, b, &r)) {
			Author *a = last_author(i, name);
			if (a)
				a->email = trim(str(r));
		}
	} else if (streq(name, "urladdr")) {
		optional(ip, b, &opt);
		if (group(ip, b, &r)) {
			Author *a = last_author(i, name);
			if (a)
				a->url = trim(str(r));
		}
	} else if (streq(name, "date")) {
		if (group(ip, b, &r)) {
			char *t = trim(str(r));
			date = streq(t, "\\today") ? "today" : *t ? plain_str(r) : NULL;
		}
	} else if (streq(name, "subjclass")) {
		bool year = optional(ip, b, &opt);
		if (group(ip, b, &r)) {
			msc = plain_str(r);
			if (year && !streq(str(opt), "2020"))
				warn(i, "the %s Mathematics Subject Classification is printed as the 2020 one; "
				    "check the codes", str(opt));
		}
	} else if (streq(name, "keywords")) {
		if (group(ip, b, &r))
			keywords = plain_str(r);
	} else if (streq(name, "dedicatory") || streq(name, "thanks")) {
		if (group(ip, b, &r))
			warn(i, "\\%s is not supported: \"%s\" is left out", name, plain_str(r));
	} else if (streq(name, "bibliography") || streq(name, "addbibresource")) {
		optional(ip, b, &opt);
		if (group(ip, b, &r)) {
			char *t = xstrdup(str(r));
			for (char *k = strtok(t, ","); k; k = strtok(NULL, ",")) {
				char *f = trim(k);
				size_t n = strlen(f);
				char *file = n > 4 && streq(f + n - 4, ".bib") ? xstrdup(f) : fmt("%s.bib", f);
				if (!has(&bibfiles, file))
					VPUSH(bibfiles, file);
			}
		}
	} else {
		return false;
	}
	return true;
}

/* ---- blocks ---- */

/* the \label{...} of a block, wherever it stands at the block's own
 * level; it is blanked so that it is not seen again */
static char *
take_label(size_t a, size_t b)
{
	int depth = 0;
	bool math = false;

	for (size_t i = a; i < b; i++) {
		if (S[i] == '\\' && at(i, b, "\\begin{"))
			depth++;
		else if (S[i] == '\\' && at(i, b, "\\end{"))
			depth--;
		else if (S[i] == '$')
			math = !math;
		else if (S[i] == '\\' && depth == 0 && !math && at_cmd(i, b, "label")) {
			size_t j = i + 6;
			R r;
			if (group(&j, b, &r)) {
				char *l = str(r);
				blank_range(i, j);
				return l;
			}
		}
		if (S[i] == '\\' && i + 1 < b)
			i++;
	}
	return NULL;
}

/* the rows of an align-like body, split at \\ outside braces and environments */
typedef VEC(R) Rows;

static Rows
split_rows(size_t a, size_t b, const char *sep)
{
	Rows v = {0};
	int depth = 0, env = 0;
	size_t start = a;

	for (size_t i = a; i < b; i++) {
		if (S[i] == '{')
			depth++;
		else if (S[i] == '}')
			depth--;
		else if (at(i, b, "\\begin{"))
			env++;
		else if (at(i, b, "\\end{"))
			env--;
		if (depth == 0 && env == 0 && at(i, b, sep) && !(sep[0] == '&' && i > 0 && S[i - 1] == '\\')) {
			R r = {start, i};
			VPUSH(v, r);
			i += strlen(sep);
			if (sep[0] == '\\') {
				/* \\[3pt] and \\* */
				if (i < b && S[i] == '*')
					i++;
				R o;
				size_t k = i;
				if (optional(&k, b, &o))
					i = k;
			}
			start = i;
			i--;
			continue;
		}
		if (S[i] == '\\' && i + 1 < b && S[i + 1] != '\\')
			i++;
	}
	R r = {start, b};
	VPUSH(v, r);
	return v;
}

/* every label the document defines, so that generated ones never clash */
static Names reserved;

static void
reserve_labels(void)
{
	for (const char *p = S; (p = strstr(p, "\\label{")) != NULL; p += 7) {
		const char *e = strchr(p + 7, '}');
		if (e)
			VPUSH(reserved, label_name(xstrndup(p + 7, (size_t)(e - p - 7))));
	}
}

/* LaTeX numbers every equation; tekx numbers labelled ones, so an
 * unlabelled numbered equation gets a label to keep its number */
static char *
fresh_eq_label(void)
{
	for (;;) {
		char *l = fmt("eq%d", ++auto_eq);
		if (!has(&attached, l) && !has(&reserved, l))
			return l;
	}
}

/* a display environment: each row a line, labelled rows numbered */
static void
display(const char *env, size_t a, size_t b, int ind)
{
	bool star = env[strlen(env) - 1] == '*';
	bool numbered = !star && !streq(env, "displaymath") && !streq(env, "math") && !streq(env, "$$") &&
	    !streq(env, "\\[");
	bool aligned = startswith(env, "align") || startswith(env, "flalign") || startswith(env, "alignat") ||
	    startswith(env, "eqnarray");
	bool gather = startswith(env, "gather");
	bool single = !aligned && !gather;
	Rows rows;
	char *outer_label = NULL;
	bool outer_nonumber = false;

	if (single && !startswith(env, "multline")) {
		/* equation: a split or aligned inside gives the rows; the number
		 * goes on the last */
		size_t s = skip_ws(a, b);
		char *inner = begin_at(S, s, b);
		outer_label = take_label(a, b);
		if (inner && (streq(inner, "split") || streq(inner, "aligned") || streq(inner, "gathered"))) {
			size_t e, after;
			size_t ib = s + strlen(inner) + 8;
			R o;
			size_t k = ib;
			if (optional(&k, b, &o))
				ib = k;
			if (env_end(inner, ib, b, &e, &after)) {
				rows = split_rows(ib, e, "\\\\");
				aligned = !streq(inner, "gathered");
			} else {
				rows = split_rows(a, b, "\\\\");
			}
		} else {
			R whole = {a, b};
			rows = (Rows){0};
			VPUSH(rows, whole);
		}
	} else if (startswith(env, "multline")) {
		/* one long line */
		Rows parts = split_rows(a, b, "\\\\");
		Buf joined = {0};
		char *lab = NULL;
		bool nonum = false;
		for (size_t i = 0; i < parts.n; i++) {
			char *l = NULL;
			bool nn = false;
			bprintf(&joined, "%s%s", i ? " " : "", math_source(parts.v[i].a, parts.v[i].b, &l, &nn));
			if (l)
				lab = l;
			nonum |= nn;
		}
		char *m = trim(bstr(&joined));
		check_math(a, m, true);
		char *label = numbered && !nonum ? (lab ? label_mark(lab, NULL) : label_mark(fresh_eq_label(), NULL)) : NULL;
		emit(ind + 4, label ? fmt("%s   #%s", m, label) : m);
		blank();
		return;
	} else {
		rows = split_rows(a, b, "\\\\");
	}
	size_t last = rows.n;
	label_only = true;
	while (last > 0) {
		char *m = math_source(rows.v[last - 1].a, rows.v[last - 1].b, NULL, NULL);
		if (*m)
			break;
		last--;
	}
	label_only = false;
	bool warned_amp = false;
	for (size_t i = 0; i < last; i++) {
		char *lab = NULL;
		bool nonum = false;
		char *m = math_source(rows.v[i].a, rows.v[i].b, &lab, &nonum);
		if (!*m)
			continue;
		/* one alignment point per line: tekx aligns at the first & */
		Buf mm = {0};
		int amps = 0, depth = 0;
		for (const char *p = m; *p; p++) {
			if (*p == '\\' && p[1]) {
				/* the & of a cases or matrix inside the line are its own */
				if (strncmp(p, "\\begin{", 7) == 0)
					depth++;
				else if (strncmp(p, "\\end{", 5) == 0)
					depth--;
				bputc(&mm, *p++);
				bputc(&mm, *p);
				continue;
			}
			if (*p == '{')
				depth++;
			else if (*p == '}')
				depth--;
			if (*p == '&' && depth == 0) {
				amps++;
				if (gather || amps > 1 || !aligned) {
					if (amps > 1 && aligned && !warned_amp && !startswith(env, "eqnarray")) {
						warn(rows.v[i].a, "tekx aligns a display at one point: the extra & are dropped");
						warned_amp = true;
					}
					continue;
				}
			}
			bputc(&mm, *p);
		}
		char *line = trim(bstr(&mm));
		if (line[0] == '&')
			line = trim(line + 1);	/* "&= c" continues the line above */
		check_math(rows.v[i].a, line, true);
		bool last_row = i + 1 == last;
		char *label = NULL;
		if (single) {
			if (last_row && numbered && !outer_nonumber && !nonum)
				label = label_mark(outer_label ? outer_label : lab ? lab : fresh_eq_label(), NULL);
		} else if (numbered && !nonum) {
			label = label_mark(lab ? lab : fresh_eq_label(), NULL);
		}
		emit(ind + 4, label ? fmt("%s   #%s", line, label) : line);
	}
	blank();
}

static void
list_env(const char *env, size_t a, size_t b, int ind)
{
	bool ordered = streq(env, "enumerate");
	bool descr = streq(env, "description");
	const char *marker = ordered ? "1. " : "- ";
	int content = ind + (int)strlen(marker);
	VEC(size_t) starts = {0};
	int depth = 0;

	flush(ind);
	for (size_t i = a; i < b; i++) {
		if (at(i, b, "\\begin{"))
			depth++;
		else if (at(i, b, "\\end{"))
			depth--;
		else if (S[i] == '{')
			depth += 100;
		else if (S[i] == '}')
			depth -= 100;
		else if (depth == 0 && at_cmd(i, b, "item"))
			VPUSH(starts, i);
		if (S[i] == '\\' && i + 1 < b && S[i + 1] != 'b' && S[i + 1] != 'e')
			i++;
	}
	if (ordered) {
		R o;
		size_t k = a;
		if (optional(&k, b, &o))
			warn(a, "enumerate options [%s] are not kept: items are numbered 1, 2, 3", str(o));
	}
	for (size_t n = 0; n < starts.n; n++) {
		size_t i = starts.v[n] + 5, end = n + 1 < starts.n ? starts.v[n + 1] : b;
		R term;
		char *t = NULL;
		if (optional(&i, end, &term))
			t = text_str(term.a, term.b);
		size_t before = body.n;
		blocks(i, end, content);
		flush(content);
		/* the first line of the item's text takes the marker */
		char *rest = xstrdup(body.s ? body.s + before : "");
		body.n = before;
		Buf head = {0};
		bprintf(&head, "%*s%s", ind, "", marker);
		if (descr && t)
			bprintf(&head, "**%s** ", t);
		else if (t && !descr)
			bprintf(&head, "%s ", t);
		if ((int)strspn(rest, " ") == content && rest[content] != '\n' && rest[content]) {
			bprintf(&body, "%s%s", bstr(&head), rest + content);
		} else {
			if (*rest)
				warn(starts.v[n], "an item that starts with a display or a block: its marker stands alone");
			bprintf(&body, "%s\\\n%s", bstr(&head), rest);
		}
		last_blank = body.n > 1 && body.s[body.n - 1] == '\n' && body.s[body.n - 2] == '\n';
	}
	blank();
}

static void
code_env(size_t a, size_t b, int ind, const char *lang)
{
	flush(ind);
	emit(ind, fmt("```%s", lang ? lang : ""));
	size_t s = a;
	if (s < b && S[s] == '\n')
		s++;
	while (s < b) {
		size_t e = s;
		while (e < b && S[e] != '\n' && S[e] != '\f')
			e++;
		char *l = xstrndup(S + s, e - s);
		size_t n = strlen(l);
		while (n > 0 && isspace((unsigned char)l[n - 1]))
			l[--n] = 0;
		if (e < b || *l)
			emit(ind, l);
		s = e + 1;
	}
	emit(ind, "```");
	blank();
}

static char *
column_aligns(const char *spec)
{
	Buf b = {0};

	for (const char *p = spec; *p; p++) {
		if (*p == 'l' || *p == 'c' || *p == 'r') {
			bputc(&b, *p);
		} else if (*p == 'p' || *p == 'm' || *p == 'b' || *p == 'X') {
			bputc(&b, 'l');
			if (p[1] == '{') {
				const char *e = strchr(p, '}');
				p = e ? e : p + strlen(p) - 1;
			}
		} else if (*p == '@' || *p == '>' || *p == '<' || *p == '!') {
			const char *e = strchr(p, '}');
			p = e ? e : p + strlen(p) - 1;
		} else if (*p == '*' && p[1] == '{') {
			int n = atoi(p + 2);
			const char *e = strchr(p, '}');
			if (!e || e[1] != '{')
				continue;
			const char *e2 = strchr(e + 1, '}');
			if (!e2)
				continue;
			char *inner = column_aligns(xstrndup(e + 2, (size_t)(e2 - e - 2)));
			for (int k = 0; k < n && k < 64; k++)
				bputs(&b, inner);
			p = e2;
		}
	}
	return bstr(&b);
}

static bool
rule_cmd(size_t i, size_t b, size_t *after)
{
	static const char *rules[] = {"hline", "toprule", "midrule", "bottomrule", "cline", "cmidrule", "hhline"};

	for (size_t k = 0; k < NELEM(rules); k++)
		if (at_cmd(i, b, rules[k])) {
			size_t j = i + strlen(rules[k]) + 1;
			R r;
			if (k >= 4) {
				optional(&j, b, &r);
				group(&j, b, &r);
			}
			*after = j;
			return true;
		}
	return false;
}

static void
tabular_env(size_t a, size_t b, int ind)
{
	R spec;
	size_t i = a;

	flush(ind);
	if (!group(&i, b, &spec)) {
		warn(a, "a tabular without a column specification is left out");
		return;
	}
	char *al = column_aligns(str(spec));
	Rows rows = split_rows(i, b, "\\\\");
	VEC(char *) out = {0};
	int header = -1;
	for (size_t r = 0; r < rows.n; r++) {
		size_t s = rows.v[r].a, e = rows.v[r].b, k;
		bool rule_before = false;
		for (;;) {
			s = skip_ws(s, e);
			if (s < e && rule_cmd(s, e, &k)) {
				s = k;
				rule_before = true;
				continue;
			}
			break;
		}
		if (rule_before && out.n == 1 && header < 0)
			header = 1;
		if (skip_ws(s, e) >= e)
			continue;
		Rows cells = split_rows(s, e, "&");
		Buf line = {0};
		bputc(&line, '|');
		for (size_t c = 0; c < cells.n; c++) {
			size_t ca = cells.v[c].a, cb = cells.v[c].b;
			size_t cs = skip_ws(ca, cb);
			if (at_cmd(cs, cb, "multicolumn")) {
				size_t j = cs + 12;
				R n, sp, tx;
				if (group(&j, cb, &n) && group(&j, cb, &sp) && group(&j, cb, &tx)) {
					warn(cs, "\\multicolumn{%s} becomes one cell", str(n));
					ca = tx.a;
					cb = tx.b;
				}
			}
			char *t = text_str(ca, cb);
			Buf esc = {0};
			bool math = false;
			for (const char *p = t; *p; p++) {
				if (*p == '$')
					math = !math;
				if (*p == '|' && !math)
					bputc(&esc, '\\');
				bputc(&esc, *p);
			}
			bprintf(&line, " %s |", bstr(&esc));
		}
		VPUSH(out, bstr(&line));
	}
	Buf sep = {0};
	bputc(&sep, '|');
	for (const char *p = al; *p; p++)
		bputs(&sep, *p == 'c' ? ":-:|" : *p == 'r' ? "--:|" : ":--|");
	if (!*al)
		bputs(&sep, "---|");
	if (header < 0)
		emit(ind, bstr(&sep));
	for (size_t r = 0; r < out.n; r++) {
		emit(ind, out.v[r]);
		if ((int)r + 1 == header)
			emit(ind, bstr(&sep));
	}
	blank();
}

static bool
exists(const char *f)
{
	FILE *fp = fopen(f, "rb");

	if (fp)
		fclose(fp);
	return fp != NULL;
}

static void
figure_env(size_t a, size_t b, int ind)
{
	char *file = NULL, *width = NULL, *caption = NULL, *alt = NULL;
	size_t gfx = a;
	char *label = take_label(a, b);	/* often inside \caption{...} */

	flush(ind);
	for (size_t i = a; i < b; i++) {
		if (at_cmd(i, b, "includegraphics")) {
			size_t j = i + 16;
			R o, f;
			if (j < b && S[j] == '*')
				j++;
			bool opts = optional(&j, b, &o);
			if (group(&j, b, &f)) {
				if (file)
					warn(i, "a figure with several images keeps only the first");
				else {
					file = trim(str(f));
					gfx = i;
				}
			}
			if (opts && width == NULL) {
				char *op = str(o), *w = strstr(op, "width=");
				if (w && (w == op || w[-1] == ',' || w[-1] == ' ')) {
					w += 6;
					char *e = strchr(w, ',');
					char *val = trim(e ? xstrndup(w, (size_t)(e - w)) : xstrdup(w));
					double share = atof(val);
					if (strstr(val, "\\textwidth") || strstr(val, "\\linewidth") || strstr(val, "\\columnwidth"))
						width = fmt("%g%%", (share > 0 ? share : 1) * 100);
					else if (strstr(val, "cm") || strstr(val, "mm") || strstr(val, "in") || strstr(val, "pt"))
						width = val;
				} else if (strstr(op, "scale=")) {
					warn(i, "scale= is not kept: give the figure a width");
				}
			}
			i = j - 1;
		} else if (at_cmd(i, b, "caption")) {
			size_t j = i + 8;
			R o, c;
			optional(&j, b, &o);
			if (group(&j, b, &c)) {
				caption = text_str(c.a, c.b);
				alt = plain_str(c);
			}
			i = j - 1;
		} else if (at_cmd(i, b, "label") && label == NULL) {
			size_t j = i + 6;
			R l;
			if (group(&j, b, &l))
				label = str(l);
			i = j - 1;
		} else if (at(i, b, "\\begin{subfigure}") || at_cmd(i, b, "subfloat")) {
			warn_once(i, "subfigure", "subfigures are not supported: the first image is kept");
		}
	}
	if (file == NULL) {
		warn(a, "a figure without \\includegraphics is left out");
		return;
	}
	/* tekx places PNG and JPEG; LaTeX names often leave the extension out */
	const char *ext = strrchr(file, '.');
	char *use = file;
	if (!ext || strchr(ext, '/') || (strcasecmp(ext, ".png") && strcasecmp(ext, ".jpg") && strcasecmp(ext, ".jpeg"))) {
		char *stem = ext && !strchr(ext, '/') ? xstrndup(file, (size_t)(ext - file)) : file;
		static const char *exts[] = {".png", ".jpg", ".jpeg", ".PNG", ".JPG"};
		use = NULL;
		for (size_t k = 0; k < NELEM(exts) && !use; k++)
			if (exists(fmt("%s%s%s", dir, stem, exts[k])))
				use = fmt("%s%s", stem, exts[k]);
		if (use == NULL) {
			use = fmt("%s.png", stem);
			warn(gfx, "tekx places PNG and JPEG images: convert %s to %s", file, use);
		}
	} else if (!exists(fmt("%s%s", dir, file))) {
		warn(gfx, "image %s not found", file);
	}
	Buf head = {0};
	bprintf(&head, "figure %s", quoted(use));
	if (width)
		bprintf(&head, " width %s", width);
	if (label)
		bprintf(&head, " #%s", label_mark(label, "Figure"));
	emit(ind, bstr(&head));
	if (alt && *alt) {
		for (char *p = alt; *p; p++)
			if (*p == '\n')
				*p = ' ';
		emit(ind + 2, fmt("alt %s", quoted(alt)));
		warn(gfx, "the figure's alt text is copied from its caption: write what the image shows, "
		    "for readers who cannot see it");
	} else {
		emit(ind + 2, "alt \"Describe the image here.\"");
		warn(gfx, "the figure has no caption to take alt text from: write what the image shows");
	}
	if (caption && *caption) {
		Buf saved = para;
		para = (Buf){0};
		bputs(&para, caption);
		flush(ind + 2);
		para = saved;
	}
	blank();
}

static void
references_env(size_t a, size_t b, int ind)
{
	VEC(size_t) items = {0};

	flush(ind);
	for (size_t i = a; i < b; i++)
		if (at_cmd(i, b, "bibitem"))
			VPUSH(items, i);
	if (items.n == 0)
		return;
	emit(ind, "references");
	for (size_t n = 0; n < items.n; n++) {
		size_t i = items.v[n] + 8, end = n + 1 < items.n ? items.v[n + 1] : b;
		R o, key;
		optional(&i, end, &o);
		if (!group(&i, end, &key))
			continue;
		char *k = trim(str(key));
		if (!has(&cite_keys, k))
			VPUSH(cite_keys, xstrdup(k));
		emit(ind + 2, fmt("%s: %s", k, text_str(i, end)));
	}
	blank();
}

/* the name a theorem-like environment prints: "Main Theorem" */
static const char *
thm_title(const char *env)
{
	Thm *t = find_thm(env);

	if (t)
		return t->title;
	for (size_t i = 0; i < NELEM(builtin); i++)
		if (streq(builtin[i].kind, env))
			return builtin[i].title;
	return NULL;
}

static const char *
thm_kind(const char *env)
{
	Thm *t = find_thm(env);

	if (t)
		return t->kind;
	for (size_t i = 0; i < NELEM(builtin); i++)
		if (streq(builtin[i].kind, env))
			return builtin[i].kind;
	return NULL;
}

static void
environment(const char *env, size_t a, size_t b, int ind)
{
	const char *kind = thm_kind(env);

	if (kind) {
		R o;
		size_t i = a;
		char *t = optional(&i, b, &o) ? text_str(o.a, o.b) : NULL;
		char *label = take_label(i, b);
		flush(ind);
		Buf head = {0};
		bputs(&head, kind);
		if (t)
			bprintf(&head, " %s", quoted(t));
		if (label)
			bprintf(&head, " #%s", label_mark(label, thm_title(env)));
		emit(ind, bstr(&head));
		blocks(i, b, ind + 2);
		flush(ind + 2);
		blank();
	} else if (streq(env, "proof")) {
		R o;
		size_t i = a;
		char *t = optional(&i, b, &o) ? text_str(o.a, o.b) : NULL;
		flush(ind);
		emit(ind, t ? fmt("proof %s", quoted(t)) : "proof");
		blocks(i, b, ind + 2);
		flush(ind + 2);
		blank();
	} else if (streq(env, "abstract")) {
		flush(ind);
		emit(ind, "abstract");
		blocks(a, b, ind + 2);
		flush(ind + 2);
		blank();
	} else if (streq(env, "quote") || streq(env, "quotation") || streq(env, "verse")) {
		flush(ind);
		emit(ind, "quote");
		blocks(a, b, ind + 2);
		flush(ind + 2);
		blank();
	} else if (streq(env, "equation") || streq(env, "equation*") || streq(env, "align") ||
	    streq(env, "align*") || streq(env, "gather") || streq(env, "gather*") || streq(env, "multline") ||
	    streq(env, "multline*") || streq(env, "flalign") || streq(env, "flalign*") ||
	    streq(env, "eqnarray") || streq(env, "eqnarray*") || streq(env, "displaymath") ||
	    streq(env, "alignat") || streq(env, "alignat*") || streq(env, "math")) {
		if (startswith(env, "alignat")) {
			R n;
			size_t i = a;
			if (group(&i, b, &n))
				a = i;
		}
		if (streq(env, "math")) {
			inline_math(&para, a, b);
			return;
		}
		flush(ind);
		display(env, a, b, ind);
	} else if (streq(env, "itemize") || streq(env, "enumerate") || streq(env, "description")) {
		list_env(env, a, b, ind);
	} else if (streq(env, "tabular") || streq(env, "tabular*") || streq(env, "tabularx") ||
	    streq(env, "longtable") || streq(env, "array")) {
		if (streq(env, "tabular*") || streq(env, "tabularx")) {
			R w;
			size_t i = a;
			if (group(&i, b, &w))
				a = i;
		}
		tabular_env(a, b, ind);
	} else if (streq(env, "table") || streq(env, "table*")) {
		/* tekx tables have no captions: the caption becomes a paragraph */
		R o;
		size_t i = a;
		optional(&i, b, &o);
		char *cap = NULL;
		for (size_t k = i; k < b; k++)
			if (at_cmd(k, b, "caption")) {
				size_t j = k + 8;
				R c;
				optional(&j, b, &o);
				if (group(&j, b, &c)) {
					cap = text_str(c.a, c.b);
					blank_range(k, j);
				}
			}
		char *label = take_label(i, b);
		if (label)
			warn(a, "tekx tables are not numbered: references to \\label{%s} will read ??", label);
		blocks(i, b, ind);
		if (cap) {
			bprintf(&para, "**Table.** %s", cap);
			flush(ind);
		}
	} else if (streq(env, "figure") || streq(env, "figure*") || streq(env, "wrapfigure")) {
		R o;
		size_t i = a;
		optional(&i, b, &o);
		if (streq(env, "wrapfigure")) {
			R g;
			group(&i, b, &g);
			group(&i, b, &g);
		}
		figure_env(i, b, ind);
	} else if (streq(env, "verbatim") || streq(env, "verbatim*")) {
		code_env(a, b, ind, NULL);
	} else if (streq(env, "lstlisting")) {
		R o;
		size_t i = a;
		char *lang = NULL;
		if (optional(&i, b, &o)) {
			char *op = str(o), *l = strstr(op, "language=");
			if (l) {
				l += 9;
				lang = xstrndup(l, strcspn(l, ",] "));
			}
		}
		code_env(i, b, ind, lang);
	} else if (streq(env, "minted")) {
		R o, l;
		size_t i = a;
		optional(&i, b, &o);
		char *lang = group(&i, b, &l) ? str(l) : NULL;
		code_env(i, b, ind, lang);
	} else if (streq(env, "thebibliography")) {
		R w;
		size_t i = a;
		group(&i, b, &w);
		references_env(i, b, ind);
	} else if (streq(env, "center") || streq(env, "flushleft") || streq(env, "flushright") ||
	    streq(env, "document") || streq(env, "small") || streq(env, "footnotesize") || streq(env, "minipage") ||
	    streq(env, "frontmatter") || streq(env, "mainmatter") || streq(env, "backmatter")) {
		if (streq(env, "minipage")) {
			R o, w;
			size_t i = a;
			optional(&i, b, &o);
			group(&i, b, &w);
			a = i;
		}
		blocks(a, b, ind);
	} else {
		warn(a, "unknown environment %s: its contents are kept", env);
		blocks(a, b, ind);
	}
}

static void
heading(const char *cmd, bool star, size_t *ip, size_t b, int ind)
{
	R o, t;

	optional(ip, b, &o);
	if (!group(ip, b, &t))
		return;
	char *label = take_label(t.a, t.b);	/* \section{Title\label{x}} */
	char *htitle = text_str(t.a, t.b);
	size_t j = skip_ws(*ip, b);
	if (label == NULL && at_cmd(j, b, "label")) {
		size_t k = j + 6;
		R l;
		if (group(&k, b, &l)) {
			label = str(l);
			*ip = k;
		}
	}
	const char *kw = streq(cmd, "subsection") ? "subsection" : streq(cmd, "subsubsection") ? "subsubsection" : "section";
	flush(ind);
	if (ind > 0)
		warn(t.a, "a heading inside a block: tekx sets it as a paragraph");
	Buf h = {0};
	bprintf(&h, "%s%s %s", kw, star ? "*" : "", htitle);
	if (label)
		bprintf(&h, " #%s", label_mark(label, appendix_seen && streq(kw, "section") ? "Appendix" : "Section"));
	if (ind > 0) {
		bprintf(&para, "**%s**", htitle);
		flush(ind);
		return;
	}
	blank();
	emit(0, bstr(&h));
	blank();
}

/* block-level LaTeX in [a, b), written at indent ind */
static void
blocks(size_t a, size_t b, int ind)
{
	for (size_t i = a; i < b;) {
		char c = S[i];
		if (c == '\n') {
			/* a blank line ends a paragraph */
			size_t k = i + 1;
			while (k < b && (S[k] == ' ' || S[k] == '\t' || S[k] == '\r'))
				k++;
			if (k < b && S[k] == '\n')
				flush(ind);
			else
				put_space(&para);
			i = k;
			continue;
		}
		if (c == '$' && i + 1 < b && S[i + 1] == '$') {
			size_t k = i + 2;
			while (k + 1 < b && !(S[k] == '$' && S[k + 1] == '$'))
				k++;
			flush(ind);
			display("$$", i + 2, k, ind);
			i = k + 2 <= b ? k + 2 : b;
			continue;
		}
		if (c != '\\') {
			size_t k = i;
			/* plain text up to the next command, display or paragraph break */
			while (k < b && S[k] != '\\' && S[k] != '\n' && !(S[k] == '$' && k + 1 < b && S[k + 1] == '$')) {
				if (S[k] == '$') {
					k++;
					while (k < b && !(S[k] == '$' && S[k - 1] != '\\'))
						k++;
				} else if (S[k] == '{') {
					R r;
					size_t g = k;
					if (group(&g, b, &r)) {
						k = g;
						continue;
					}
				}
				k++;
			}
			text(&para, i, k > b ? b : k);
			i = k > b ? b : k;
			continue;
		}
		size_t j;
		char *name = cmd_name(i, &j);
		bool star = j < b && S[j] == '*';
		if (streq(name, "begin")) {
			R e;
			size_t k = j;
			if (!group(&k, b, &e)) {
				i = j;
				continue;
			}
			char *env = str(e);
			size_t end, after;
			if (!env_end(env, k, b, &end, &after)) {
				warn(i, "\\begin{%s} is never ended", env);
				end = after = b;
			}
			environment(env, k, end, ind);
			i = after;
			continue;
		}
		if (streq(name, "[")) {
			size_t k = j;
			while (k + 1 < b && !at(k, b, "\\]"))
				k++;
			flush(ind);
			display("\\[", j, k, ind);
			i = k + 2 <= b ? k + 2 : b;
			continue;
		}
		if (streq(name, "section") || streq(name, "subsection") || streq(name, "subsubsection") ||
		    streq(name, "chapter")) {
			if (streq(name, "chapter"))
				warn_once(i, "chapter", "\\chapter becomes a section");
			i = star ? j + 1 : j;
			heading(name, star, &i, b, ind);
			continue;
		}
		if (streq(name, "paragraph") || streq(name, "subparagraph")) {
			R t;
			i = star ? j + 1 : j;
			if (group(&i, b, &t))
				bprintf(&para, "**%s** ", text_str(t.a, t.b));
			continue;
		}
		if (front_cmd(name, i, &j, b)) {
			i = j;
			continue;
		}
		if (streq(name, "appendix")) {
			flush(ind);
			blank();
			emit(0, "appendix");
			blank();
			appendix_seen = true;
			i = j;
			continue;
		}
		if (streq(name, "tableofcontents")) {
			flush(ind);
			emit(ind, "contents");
			blank();
			i = j;
			continue;
		}
		if (streq(name, "newpage") || streq(name, "clearpage") || streq(name, "cleardoublepage")) {
			flush(ind);
			emit(ind, "newpage");
			blank();
			i = j;
			continue;
		}
		if (streq(name, "par")) {
			flush(ind);
			i = j;
			continue;
		}
		if (streq(name, "printbibliography") || streq(name, "bibliographystyle")) {
			R r;
			i = j;
			optional(&i, b, &r);
			if (streq(name, "bibliographystyle"))
				group(&i, b, &r);
			continue;
		}
		if (streq(name, "item")) {
			warn(i, "\\item outside a list is set as text");
			i = j;
			continue;
		}
		free(name);
		inline_cmd(&para, &i, b);
	}
}

/* ---- the document ---- */

static void
resolve(Buf *o, const char *s)
{
	/* targets and references, now that every target is known; a label
	 * that is also a citation key is renamed, since tekx would not know
	 * which of the two a bare @name means */
	for (const char *p = s; *p; p++) {
		if (*p != '\x01' || (p[1] != 'L' && p[1] != 'R')) {
			bputc(o, *p);
			continue;
		}
		const char *e = strchr(p, '\x02');
		if (!e) {
			bputc(o, *p);
			continue;
		}
		char *name = xstrndup(p + 2, (size_t)(e - p - 2));
		size_t where = 0;
		char *off = strchr(name, '\x03');
		if (off) {
			*off = 0;
			where = (size_t)strtoull(off + 1, NULL, 10);
		}
		char *final = has(&cite_keys, name) ? fmt("ref-%s", name) : name;
		if (p[1] == 'L') {
			bputs(o, final);
		} else if (has(&attached, name)) {
			/* "Theorem~\ref{x}": the reference says "Main Theorem 2" itself */
			static const char *words[] = {"Theorem", "Lemma", "Corollary", "Proposition", "Definition",
			    "Remark", "Example", "Section", "Subsection", "Appendix", "Figure", "Fig.", "Thm.",
			    "Lem.", "Cor.", "Prop.", "Def.", "Sec.", "Claim", "Conjecture", "Observation", "Note",
			    "Exercise", "Problem", "Notation", "Assumption", "Hypothesis", "Fact", "Axiom",
			    "Solution", "Chapter"};
			const char *kind = target_kind(name);
			if (kind && !drop_word(o, kind))
				for (size_t k = 0; k < NELEM(words) && !drop_word(o, words[k]); k++)
					;
			bprintf(o, "@%s", final);
		} else {
			warn(where, "\\ref{%s} has no target that tekx can number; it reads ??", name);
			bputs(o, "??");
		}
		p = e;
	}
}

int
latex_import(FILE *in, FILE *out, const char *file)
{
	size_t n;

	path = file;
	diag_file = file;
	const char *slash = strrchr(file, '/');
	if (slash)
		dir = xstrndup(file, (size_t)(slash - file + 1));
	S = read_all(in, &n);
	N = n;
	/* a BOM, and Windows line ends */
	if (N >= 3 && (unsigned char)S[0] == 0xEF && (unsigned char)S[1] == 0xBB && (unsigned char)S[2] == 0xBF) {
		S += 3;
		N -= 3;
	}
	for (size_t i = 0; i < N; i++)
		if (S[i] == '\r')
			S[i] = ' ';
	strip_comments(S, N);
	read_definitions();
	reserve_labels();
	int passes = 0;
	while (expand_pass())
		if (++passes == 20) {
			warn(0, "macro expansion goes on and on (a macro that uses itself?): stopped after 20 rounds");
			break;
		}

	eqn_set_notation_tex(true);
	for (size_t i = 0; i < operators.n; i++)
		eqn_define(fmt("operator %s", operators.v[i]), 0, 0);

	/* the title page may stand in the preamble (article) or the body (amsart) */
	size_t begin = 0, end = N;
	const char *bd = strstr(S, "\\begin{document}");
	if (bd) {
		for (size_t i = 0; i < (size_t)(bd - S); i++) {
			if (S[i] != '\\')
				continue;
			size_t j;
			char *name = cmd_name(i, &j);
			if (front_cmd(name, i, &j, (size_t)(bd - S)))
				i = j - 1;
			free(name);
		}
		begin = (size_t)(bd - S) + 16;
		const char *ed = strstr(bd, "\\end{document}");
		end = ed ? (size_t)(ed - S) : N;
	}
	blocks(begin, end, 0);
	flush(0);

	/* the cited keys of the .bib files, to rename labels that clash */
	for (size_t i = 0; i < bibfiles.n; i++) {
		BibEntries db = {0};
		char *p = fmt("%s%s", dir, bibfiles.v[i]);
		if (!bib_read(p, &db)) {
			warn(0, "bibliography %s not found next to %s", bibfiles.v[i], file);
			continue;
		}
		for (size_t k = 0; k < db.n; k++)
			if (!has(&cite_keys, db.v[k].key))
				VPUSH(cite_keys, db.v[k].key);
	}

	Buf doc = {0};
	bprintf(&doc, "// Imported from %s by tekx import.\n", slash ? slash + 1 : file);
	if (title)
		bprintf(&doc, "title %s\n", quoted(title));
	for (size_t i = 0; i < authors.n; i++) {
		Author *a = &authors.v[i];
		bool details = a->affil.n || a->email || a->url;
		bprintf(&doc, "author %s", details ? arg_quoted(a->name) : quoted(a->name));
		for (size_t k = 0; k < a->affil.n; k++)
			bprintf(&doc, ", affiliation: %s", arg_quoted(a->affil.v[k]));
		if (a->email)
			bprintf(&doc, ", email: %s", arg_quoted(a->email));
		if (a->url)
			bprintf(&doc, ", url: %s", arg_quoted(a->url));
		bputc(&doc, '\n');
	}
	if (date)
		bprintf(&doc, "date %s\n", streq(date, "today") ? "today" : quoted(date));
	if (msc)
		bprintf(&doc, "msc %s\n", quoted(msc));
	if (keywords)
		bprintf(&doc, "keywords %s\n", quoted(keywords));
	if (by_section)
		bputs(&doc, "numbering by section\n");
	bputs(&doc, "notation tex\n");
	for (size_t i = 0; i < bibfiles.n; i++)
		bprintf(&doc, "%s%s", i ? ", " : "bibliography ", quoted(bibfiles.v[i]));
	if (bibfiles.n)
		bputc(&doc, '\n');
	if (operators.n || defines.n) {
		bputc(&doc, '\n');
		for (size_t i = 0; i < operators.n; i++)
			bprintf(&doc, "define operator %s\n", operators.v[i]);
		bputs(&doc, bstr(&defines));
	}
	bputc(&doc, '\n');
	resolve(&doc, bstr(&body));
	/* one blank line at the end, none doubled */
	char *s = bstr(&doc);
	size_t len = strlen(s);
	while (len > 1 && s[len - 1] == '\n' && s[len - 2] == '\n')
		s[--len] = 0;
	fputs(s, out);
	return 0;
}
