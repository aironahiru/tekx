/*
 * eqn.c -- mathematics, the heir of Kernighan and Cherry's eqn.
 *
 * Formulas are written the way they are spoken:
 *
 *	sum k^2 for k from 1 to n = n(n + 1)(2n + 1)/6
 *	integral of e^(-x^2) dx from -infinity to infinity = sqrt pi
 *	limit of sin x / x as x approaches 0 = 1
 *
 * This file holds the tokenizer, the parser (Pratt's top-down operator
 * precedence, with juxtaposition as an operator) and the stage's main
 * loop.  Layout lives in eqnlayout.c; MathML, speech and the linear copy
 * text in eqnsem.c.
 */
#include "eqn.h"
#include "tree.h"

#include <math.h>

M *
mnew(int kind)
{
	M *m = NEW(M);

	m->kind = kind;
	m->open = m->close = "";
	m->text = "";
	return m;
}

M *
mrow(void)
{
	return mnew(M_ROW);
}

void
madd(M *row, M *kid)
{
	if (kid)
		VPUSH(row->kids, kid);
}

/* ---- symbols ---- */

typedef struct {
	const char *name, *uni;
	int cls, lbp;
} OpDef;

/* infix and prefix operators; lbp 0 means "not infix" */
static const OpDef ops[] = {
	/* relations */
	{"=", "=", REL, 20}, {"!=", "≠", REL, 20}, {"ne", "≠", REL, 20}, {"neq", "≠", REL, 20},
	{"≠", "≠", REL, 20}, {"<", "<", REL, 20}, {">", ">", REL, 20},
	{"<=", "≤", REL, 20}, {"le", "≤", REL, 20}, {"leq", "≤", REL, 20}, {"≤", "≤", REL, 20},
	{"⩽", "≤", REL, 20}, {">=", "≥", REL, 20}, {"ge", "≥", REL, 20}, {"geq", "≥", REL, 20},
	{"≥", "≥", REL, 20}, {"⩾", "≥", REL, 20}, {"<<", "≪", REL, 20}, {"ll", "≪", REL, 20},
	{"≪", "≪", REL, 20}, {">>", "≫", REL, 20}, {"gg", "≫", REL, 20}, {"≫", "≫", REL, 20},
	{"~", "∼", REL, 20}, {"sim", "∼", REL, 20}, {"∼", "∼", REL, 20},
	{"~=", "≅", REL, 20}, {"cong", "≅", REL, 20}, {"≅", "≅", REL, 20},
	{"approx", "≈", REL, 20}, {"≈", "≈", REL, 20}, {"equiv", "≡", REL, 20},
	{"≡", "≡", REL, 20}, {"propto", "∝", REL, 20}, {"∝", "∝", REL, 20},
	{"in", "∈", REL, 20}, {"∈", "∈", REL, 20}, {"notin", "∉", REL, 20},
	{"∉", "∉", REL, 20}, {"ni", "∋", REL, 20}, {"∋", "∋", REL, 20},
	{"subset", "⊂", REL, 20}, {"⊂", "⊂", REL, 20}, {"subseteq", "⊆", REL, 20},
	{"⊆", "⊆", REL, 20}, {"supset", "⊃", REL, 20}, {"⊃", "⊃", REL, 20},
	{"supseteq", "⊇", REL, 20}, {"⊇", "⊇", REL, 20},
	{"->", "→", REL, 20}, {"to", "→", REL, 20}, {"→", "→", REL, 20},
	{"approaches", "→", REL, 20}, {"<-", "←", REL, 20}, {"←", "←", REL, 20},
	{"<->", "↔", REL, 20}, {"↔", "↔", REL, 20}, {"|->", "↦", REL, 20},
	{"mapsto", "↦", REL, 20}, {"↦", "↦", REL, 20}, {":=", "≔", REL, 20},
	{"≔", "≔", REL, 20}, {"divides", "∣", REL, 20}, {"∣", "∣", REL, 20},
	{"perp", "⟂", REL, 20}, {"⟂", "⟂", REL, 20}, {"parallel", "∥", REL, 20},
	{"∥", "∥", REL, 20}, {"prec", "≺", REL, 20}, {"succ", "≻", REL, 20},
	{"vdash", "⊢", REL, 20}, {"models", "⊨", REL, 20}, {"⊢", "⊢", REL, 20},
	{"⊨", "⊨", REL, 20}, {"≃", "≃", REL, 20}, {"simeq", "≃", REL, 20},
	{"≐", "≐", REL, 20}, {"doteq", "≐", REL, 20}, {"≜", "≜", REL, 20},
	{"⟶", "⟶", REL, 20}, {"↪", "↪", REL, 20}, {"⇐", "⇐", REL, 12},
	{"=>", "⇒", REL, 12}, {"⇒", "⇒", REL, 12}, {"implies", "⟹", REL, 12},
	{"⟹", "⟹", REL, 12}, {"<=>", "⇔", REL, 10}, {"⇔", "⇔", REL, 10},
	{"iff", "⟺", REL, 10}, {"⟺", "⟺", REL, 10},
	/* logic */
	{"land", "∧", BIN, 16}, {"/\\", "∧", BIN, 16}, {"∧", "∧", BIN, 16},
	{"wedge", "∧", BIN, 50},
	{"lor", "∨", BIN, 14}, {"\\/", "∨", BIN, 14}, {"∨", "∨", BIN, 14},
	/* binary */
	{"+", "+", BIN, 40}, {"-", "−", BIN, 40}, {"−", "−", BIN, 40},
	{"+-", "±", BIN, 40}, {"pm", "±", BIN, 40}, {"±", "±", BIN, 40},
	{"-+", "∓", BIN, 40}, {"mp", "∓", BIN, 40}, {"∓", "∓", BIN, 40},
	{"union", "∪", BIN, 40}, {"cup", "∪", BIN, 40}, {"∪", "∪", BIN, 40},
	{"setminus", "∖", BIN, 40}, {"∖", "∖", BIN, 40}, {"oplus", "⊕", BIN, 40},
	{"⊕", "⊕", BIN, 40}, {"ominus", "⊖", BIN, 40},
	{"intersect", "∩", BIN, 50}, {"cap", "∩", BIN, 50}, {"∩", "∩", BIN, 50},
	{"*", "⋅", BIN, 50}, {"cdot", "⋅", BIN, 50}, {"⋅", "⋅", BIN, 50}, {"·", "⋅", BIN, 50},
	{"times", "×", BIN, 50}, {"xx", "×", BIN, 50}, {"cross", "×", BIN, 50},
	{"×", "×", BIN, 50}, {"div", "÷", BIN, 50}, {"÷", "÷", BIN, 50},
	{"circ", "∘", BIN, 50}, {"compose", "∘", BIN, 50}, {"∘", "∘", BIN, 50},
	{"otimes", "⊗", BIN, 50}, {"⊗", "⊗", BIN, 50}, {"odot", "⊙", BIN, 50},
	{"star", "⋆", BIN, 50}, {"ast", "∗", BIN, 50}, {"∗", "∗", BIN, 50},
	{"mod", "mod", BIN, 50},
};

static const OpDef *
find_op(const char *s)
{
	for (size_t i = 0; i < NELEM(ops); i++)
		if (streq(ops[i].name, s))
			return &ops[i];
	return NULL;
}

static const struct {
	const char *name, *uni;
} greek[] = {
	{"alpha", "α"}, {"beta", "β"}, {"gamma", "γ"}, {"delta", "δ"},
	{"epsilon", "ε"}, {"varepsilon", "ϵ"}, {"zeta", "ζ"}, {"eta", "η"},
	{"theta", "θ"}, {"vartheta", "ϑ"}, {"iota", "ι"}, {"kappa", "κ"},
	{"lambda", "λ"}, {"mu", "μ"}, {"nu", "ν"}, {"xi", "ξ"}, {"omicron", "ο"},
	{"pi", "π"}, {"varpi", "ϖ"}, {"rho", "ρ"}, {"varrho", "ϱ"}, {"sigma", "σ"},
	{"varsigma", "ς"}, {"tau", "τ"}, {"upsilon", "υ"}, {"phi", "φ"},
	{"varphi", "ϕ"}, {"chi", "χ"}, {"psi", "ψ"}, {"omega", "ω"},
	{"Gamma", "Γ"}, {"Delta", "Δ"}, {"Theta", "Θ"}, {"Lambda", "Λ"},
	{"Xi", "Ξ"}, {"Pi", "Π"}, {"Sigma", "Σ"}, {"Upsilon", "Υ"}, {"Phi", "Φ"},
	{"Psi", "Ψ"}, {"Omega", "Ω"},
};

static const char *
greek_char(const char *w)
{
	for (size_t i = 0; i < NELEM(greek); i++)
		if (streq(greek[i].name, w))
			return greek[i].uni;
	return NULL;
}

/* ordinary symbols written as words */
static const struct {
	const char *name, *uni;
} ords[] = {
	{"infinity", "∞"}, {"infty", "∞"}, {"oo", "∞"}, {"partial", "∂"},
	{"nabla", "∇"}, {"grad", "∇"}, {"emptyset", "∅"}, {"aleph", "ℵ"},
	{"hbar", "ℏ"}, {"ell", "ℓ"}, {"angle", "∠"}, {"triangle", "△"},
	{"degree", "°"}, {"cdots", "⋯"}, {"ldots", "…"}, {"vdots", "⋮"},
	{"ddots", "⋱"}, {"therefore", "∴"}, {"because", "∵"}, {"top", "⊤"},
	{"bot", "⊥"}, {"wp", "℘"}, {"Re", "ℜ"}, {"Im", "ℑ"},
};

static const char *
ord_char(const char *w)
{
	for (size_t i = 0; i < NELEM(ords); i++)
		if (streq(ords[i].name, w))
			return ords[i].uni;
	return NULL;
}

static const struct {
	const char *name;
	char letter;
} blackboard[] = {
	{"RR", 'R'}, {"NN", 'N'}, {"ZZ", 'Z'}, {"QQ", 'Q'}, {"CC", 'C'}, {"HH", 'H'},
	{"PP", 'P'}, {"FF", 'F'}, {"EE", 'E'}, {"KK", 'K'}, {"AA", 'A'}, {"TT", 'T'},
};

static const char *function_names[] = {
	"sin", "cos", "tan", "cot", "sec", "csc", "sinh", "cosh", "tanh", "coth", "sech", "csch",
	"arcsin", "arccos", "arctan", "arccot", "arsinh", "arcosh", "artanh", "log", "ln", "lg",
	"exp", "det", "dim", "ker", "deg", "gcd", "lcm", "hom", "arg", "Pr", "tr", "Tr", "rank",
	"sgn", "sign", "span", "diag", "id", "erf", "Var", "Cov", "cis", "lb", "im", "coker", "Aut",
	"End", "Hom", "Ker", "supp", "vol", "curl", "div",
};

/* names that take limits like big operators */
static const char *limit_names[] = {
	"lim", "liminf", "limsup", "max", "min", "sup", "inf", "argmax", "argmin", "esssup",
};

static bool
in_list(const char **list, size_t n, const char *w)
{
	for (size_t i = 0; i < n; i++)
		if (streq(list[i], w))
			return true;
	return false;
}

/* ---- declarations and macros ---- */

typedef struct {
	char *name;
	VEC(char *) params;
	char *body, *readas;
	int line, col;
} MacroDef;

static VEC(MacroDef) mdefs;
static VEC(char *) user_functions;	/* f(x) style */
static VEC(char *) user_operators;	/* sin x style */
static VEC(char *) doc_functions;	/* declared by `let` or f(x) = ... */

void
eqn_declare_function(const char *name)
{
	for (size_t i = 0; i < doc_functions.n; i++)
		if (streq(doc_functions.v[i], name))
			return;
	VPUSH(doc_functions, xstrdup(name));
}

void
eqn_reset_declarations(void)
{
	doc_functions.n = 0;
}

static bool
is_user_function(const char *w)
{
	for (size_t i = 0; i < user_functions.n; i++)
		if (streq(user_functions.v[i], w))
			return true;
	for (size_t i = 0; i < doc_functions.n; i++)
		if (streq(doc_functions.v[i], w))
			return true;
	return false;
}

static bool
is_operator_name(const char *w)
{
	if (in_list(function_names, NELEM(function_names), w))
		return true;
	for (size_t i = 0; i < user_operators.n; i++)
		if (streq(user_operators.v[i], w))
			return true;
	return false;
}

static MacroDef *
find_mdef(const char *w)
{
	for (size_t i = 0; i < mdefs.n; i++)
		if (streq(mdefs.v[i].name, w))
			return &mdefs.v[i];
	return NULL;
}

static void
name_list(const char *s, void (*add)(const char *))
{
	char *t = xstrdup(s);

	for (char *w = strtok(t, ", "); w; w = strtok(NULL, ", "))
		add(w);
}

static void
add_function(const char *w)
{
	VPUSH(user_functions, xstrdup(w));
}

static void
add_operator(const char *w)
{
	VPUSH(user_operators, xstrdup(w));
}

void
eqn_define(const char *def, int line, int col)
{
	const char *s = def;

	if (startswith(s, "operator ") || startswith(s, "operators ")) {
		name_list(strchr(s, ' ') + 1, add_operator);
		return;
	}
	if (startswith(s, "function ") || startswith(s, "functions ")) {
		name_list(strchr(s, ' ') + 1, add_function);
		return;
	}
	MacroDef d;
	memset(&d, 0, sizeof d);
	d.line = line;
	d.col = col;
	const char *p = s;
	while (*p && (*p == '_' || ((*p | 32) >= 'a' && (*p | 32) <= 'z') || (*p >= '0' && *p <= '9')))
		p++;
	d.name = xstrndup(s, (size_t)(p - s));
	if (*d.name == 0) {
		error_at(line, col, "expected a name after 'define'");
		return;
	}
	if (*p == '(') {
		const char *e = strchr(p, ')');
		if (e == NULL) {
			error_at(line, col + (int)(p - s), "missing ')' in definition of %s", d.name);
			return;
		}
		char *ps = xstrndup(p + 1, (size_t)(e - p - 1));
		for (char *w = strtok(ps, ", "); w; w = strtok(NULL, ", "))
			VPUSH(d.params, xstrdup(w));
		p = e + 1;
	}
	while (*p == ' ')
		p++;
	if (!startswith(p, "as ")) {
		error_at(line, col + (int)(p - s), "expected 'as' in the definition of %s "
		    "(define NAME(params) as TEMPLATE, define operator NAME, or define function NAME)", d.name);
		return;
	}
	p += 3;
	const char *ra = strstr(p, " read as ");
	if (ra) {
		d.body = trim(xstrndup(p, (size_t)(ra - p)));
		char *r = trim(xstrdup(ra + 9));
		size_t n = strlen(r);
		if (n >= 2 && r[0] == '"' && r[n - 1] == '"') {
			r[n - 1] = 0;
			r++;
		}
		d.readas = r;
	} else {
		d.body = trim(xstrdup(p));
	}
	d.col = col + (int)(p - s);
	if (find_mdef(d.name))
		error_at(line, col, "%s is already defined", d.name);
	VPUSH(mdefs, d);
}

/* ---- tokens ---- */

enum { T_EOF, T_NUM, T_WORD, T_SYM, T_STR, T_SUP, T_SUB, T_ACC };

typedef struct {
	int kind;
	char *s;
	int pos;	/* byte offset in the source */
	bool sp;	/* preceded by a blank */
} Tok;

typedef VEC(Tok) TokVec;
typedef VEC(char *) StrVec;

static const char *ascii_syms[] = {
	"<=>", "<->", "|->", "...", "<==", "<=", ">=", "!=", "->", "=>", "<-", ":=", "~=", "+-", "-+",
	"||", "<<", ">>", "/\\", "\\/", "''",
};

static const char *
sup_ascii(u32 c)
{
	switch (c) {
	case 0x2070: return "0";
	case 0x00b9: return "1";
	case 0x00b2: return "2";
	case 0x00b3: return "3";
	case 0x2074: return "4";
	case 0x2075: return "5";
	case 0x2076: return "6";
	case 0x2077: return "7";
	case 0x2078: return "8";
	case 0x2079: return "9";
	case 0x207a: return "+";
	case 0x207b: return "-";
	case 0x207c: return "=";
	case 0x207d: return "(";
	case 0x207e: return ")";
	case 0x207f: return "n";
	case 0x2071: return "i";
	case 0x1d40: return "T";
	}
	return NULL;
}

static const char *
sub_ascii(u32 c)
{
	static const char *letters[] = {"a", "e", "o", "x", NULL, "h", "k", "l", "m", "n", "p", "s", "t"};

	if (c >= 0x2080 && c <= 0x2089)
		return fmt("%c", (char)('0' + (c - 0x2080)));
	if (c == 0x208a) return "+";
	if (c == 0x208b) return "-";
	if (c == 0x208c) return "=";
	if (c == 0x208d) return "(";
	if (c == 0x208e) return ")";
	if (c >= 0x2090 && c <= 0x209c)
		return letters[c - 0x2090];
	if (c == 0x1d62) return "i";
	if (c == 0x2c7c) return "j";
	if (c == 0x1d63) return "r";
	if (c == 0x1d64) return "u";
	if (c == 0x1d65) return "v";
	return NULL;
}

static bool
is_letter_cp(u32 c)
{
	return (c >= 0x391 && c <= 0x3a9) || (c >= 0x3b1 && c <= 0x3c9) || c == 0x3d1 || c == 0x3d5 ||
	    c == 0x3d6 || c == 0x3f0 || c == 0x3f1 || c == 0x3f5 || (c >= 0x1d400 && c <= 0x1d7ff) ||
	    c == 0x2102 || c == 0x210a || c == 0x210b || c == 0x210c || c == 0x210d || c == 0x210e ||
	    c == 0x2110 || c == 0x2111 || c == 0x2112 || c == 0x2113 || c == 0x2115 || c == 0x2119 ||
	    c == 0x211a || c == 0x211b || c == 0x211c || c == 0x211d || c == 0x2124 || c == 0x2128 ||
	    c == 0x212c || c == 0x212d || c == 0x212f || c == 0x2130 || c == 0x2131 || c == 0x2133 ||
	    c == 0x2134 || c == 0x2135 || c == 0x210f || c == 0x2118 || (c >= 0xc0 && c <= 0x24f && c != 0xd7 && c != 0xf7);
}

static bool
is_accent_cp(u32 c)
{
	return (c >= 0x300 && c <= 0x36f) || c == 0x20d7 || c == 0x20d6 || c == 0x20e1;
}

static TokVec
tokenize(const char *s, int line, int col)
{
	TokVec v = {0};
	size_t i = 0, n = strlen(s);
	bool sp = false;

	while (i < n) {
		Tok t = {0};
		char c = s[i];
		if (c == ' ') {
			sp = true;
			i++;
			continue;
		}
		t.pos = (int)i;
		t.sp = sp;
		sp = false;
		if (c >= '0' && c <= '9') {
			size_t j = i;
			while (j < n && s[j] >= '0' && s[j] <= '9')
				j++;
			if (j + 1 < n && s[j] == '.' && s[j + 1] >= '0' && s[j + 1] <= '9') {
				j++;
				while (j < n && s[j] >= '0' && s[j] <= '9')
					j++;
			}
			t.kind = T_NUM;
			t.s = xstrndup(s + i, j - i);
			i = j;
		} else if (c == '.' && i + 1 < n && s[i + 1] >= '0' && s[i + 1] <= '9') {
			size_t j = i + 1;
			while (j < n && s[j] >= '0' && s[j] <= '9')
				j++;
			t.kind = T_NUM;
			t.s = fmt("0%.*s", (int)(j - i), s + i);
			i = j;
		} else if ((c | 32) >= 'a' && (c | 32) <= 'z') {
			size_t j = i;
			while (j < n && (((s[j] | 32) >= 'a' && (s[j] | 32) <= 'z')))
				j++;
			t.kind = T_WORD;
			t.s = xstrndup(s + i, j - i);
			i = j;
		} else if (c == '"') {
			size_t j = i + 1;
			while (j < n && s[j] != '"')
				j++;
			if (j >= n)
				error_at(line, col + (int)i, "unterminated text in formula: missing closing \"");
			t.kind = T_STR;
			t.s = xstrndup(s + i + 1, j - i - 1);
			i = j < n ? j + 1 : j;
		} else if ((unsigned char)c >= 0x80) {
			u32 cp;
			int k = utf8_dec(s + i, n - i, &cp);
			const char *a;
			t.s = xstrndup(s + i, (size_t)k);
			if ((a = sup_ascii(cp)) != NULL) {
				t.kind = T_SUP;
				t.s = xstrdup(a);
			} else if ((a = sub_ascii(cp)) != NULL) {
				t.kind = T_SUB;
				t.s = xstrdup(a);
			} else if (is_accent_cp(cp)) {
				t.kind = T_ACC;
			} else if (is_letter_cp(cp)) {
				t.kind = T_WORD;
			} else {
				t.kind = T_SYM;
			}
			i += (size_t)k;
			/* merge runs of super/subscripts: x²³ */
			if ((t.kind == T_SUP || t.kind == T_SUB) && v.n && v.v[v.n - 1].kind == t.kind && !t.sp) {
				v.v[v.n - 1].s = fmt("%s%s", v.v[v.n - 1].s, t.s);
				continue;
			}
		} else {
			size_t k;
			for (k = 0; k < NELEM(ascii_syms); k++)
				if (strncmp(s + i, ascii_syms[k], strlen(ascii_syms[k])) == 0)
					break;
			t.kind = T_SYM;
			if (k < NELEM(ascii_syms)) {
				t.s = xstrdup(ascii_syms[k]);
				i += strlen(ascii_syms[k]);
			} else {
				t.s = xstrndup(s + i, 1);
				i++;
			}
		}
		VPUSH(v, t);
	}
	Tok e = {T_EOF, "", (int)n, sp};
	VPUSH(v, e);
	return v;
}

/* ---- parser ---- */

enum {
	STOP_TO = 1, STOP_FROM = 2, STOP_OF = 4, STOP_AS = 8, STOP_DIFF = 16, STOP_FOR = 32,
	STOP_IF = 64, STOP_BAR = 128, STOP_NORM = 256,
};

typedef struct Bind Bind;
struct Bind {
	const char *name;
	M *val;
	Bind *next;
};

typedef struct {
	const char *src;
	int line, col;
	Tok *t;
	int n, i;
	bool display;
	char **rows;
	int nrows;
	int stop;
	int braces;
	bool func_arg;
	Bind *env;
	int depth;
} P;

static M *expr(P *p, int min_bp);
static M *nud(P *p);

static Tok *
peek(P *p)
{
	return &p->t[p->i];
}

static Tok *
peek2(P *p)
{
	return &p->t[p->i + 1 < p->n ? p->i + 1 : p->i];
}

static Tok *
advance(P *p)
{
	Tok *t = &p->t[p->i];

	if (p->i < p->n - 1)
		p->i++;
	return t;
}

static bool
is(Tok *t, const char *s)
{
	return (t->kind == T_SYM || t->kind == T_WORD) && streq(t->s, s);
}

static bool
accept(P *p, const char *s)
{
	if (is(peek(p), s)) {
		advance(p);
		return true;
	}
	return false;
}

static void
perr(P *p, Tok *t, const char *msg)
{
	/* columns count characters, not bytes */
	int c = 0;
	for (int k = 0; k < t->pos && p->src[k];) {
		u32 cp;
		k += utf8_dec(p->src + k, strlen(p->src + k), &cp);
		c++;
	}
	error_at(p->line, p->col + c, "%s", msg);
}

static M *
op(const char *uni, int cls)
{
	M *m = mnew(M_OP);

	m->text = xstrdup(uni);
	m->cls = cls;
	return m;
}

static M *
ident(const char *s, int variant)
{
	M *m = mnew(M_ID);

	m->text = xstrdup(s);
	m->variant = variant;
	return m;
}

static M *
text_node(const char *s)
{
	M *m = mnew(M_TEXT);

	m->text = xstrdup(s);
	return m;
}

static M *
infix(M *a, M *o, M *b)
{
	M *r = mrow();

	madd(r, a);
	madd(r, o);
	madd(r, b);
	return r;
}

static M *
fence(const char *open, M *body, const char *close)
{
	M *m = mnew(M_FENCE);

	m->open = xstrdup(open);
	m->close = xstrdup(close);
	m->a = body ? body : mrow();
	return m;
}

/* remove grouping parentheses that the layout makes redundant */
static M *
strip(M *m)
{
	if (m && m->kind == M_FENCE && streq(m->open, "(") && streq(m->close, ")") && m->sem == NULL)
		return m->a;
	return m;
}

bool
is_atom(M *m)
{
	if (m == NULL)
		return true;
	if (m->kind == M_ROW)
		return m->kids.n == 1 && is_atom(m->kids.v[0]);
	if (m->kind == M_ID || m->kind == M_NUM || m->kind == M_TEXT || m->kind == M_FENCE)
		return true;
	if (m->kind == M_OP)
		return m->cls == ORD;
	if (m->kind == M_SCRIPT || m->kind == M_ACCENT)
		return is_atom(m->a);
	return false;
}

static bool
is_funclike(M *m)
{
	if (m == NULL)
		return false;
	if (m->kind == M_ID)
		return m->func;
	if (m->kind == M_SCRIPT && !m->limits)
		return is_funclike(m->a);
	return false;
}

static bool
starts_operand(P *p, Tok *t)
{
	switch (t->kind) {
	case T_NUM: case T_STR:
		return true;
	case T_WORD: {
		if (find_op(t->s) || streq(t->s, "over") || streq(t->s, "choose") || streq(t->s, "if") ||
		    streq(t->s, "otherwise") || streq(t->s, "when") || streq(t->s, "where") ||
		    streq(t->s, "for") || streq(t->s, "and") || streq(t->s, "or") || streq(t->s, "squared") ||
		    streq(t->s, "cubed") || streq(t->s, "factorial") || streq(t->s, "from") || streq(t->s, "of") ||
		    streq(t->s, "as") || streq(t->s, "dot") || streq(t->s, "goes") || streq(t->s, "tends") ||
		    streq(t->s, "inverse") || streq(t->s, "transpose") || streq(t->s, "is") || streq(t->s, "unless"))
			return false;
		if (streq(t->s, "not") && is(&p->t[p->i + 1 < p->n ? p->i + 1 : p->i], "in"))
			return false;
		if ((p->stop & STOP_DIFF) && t->s[0] == 'd' && t->s[1] && (t->s[2] == 0 || greek_char(t->s + 1)))
			return false;
		if (p->func_arg && (is_operator_name(t->s) || in_list(limit_names, NELEM(limit_names), t->s)))
			return false;
		return true;
	}
	case T_SYM: {
		const char *s = t->s;
		if (streq(s, "(") || streq(s, "[") || streq(s, "{") || streq(s, "⟨"))
			return true;
		if (streq(s, "|"))
			return !(p->stop & STOP_BAR);
		if (streq(s, "||") || streq(s, "‖"))
			return !(p->stop & STOP_NORM);
		if (streq(s, "√") || streq(s, "∛") || streq(s, "∜") || streq(s, "∑") ||
		    streq(s, "∏") || streq(s, "∫") || streq(s, "∬") || streq(s, "∭") ||
		    streq(s, "∮") || streq(s, "∞") || streq(s, "∂") || streq(s, "∇") ||
		    streq(s, "∅") || streq(s, "∀") || streq(s, "∃") || streq(s, "¬") ||
		    streq(s, "⋃") || streq(s, "⋂") || streq(s, "∐") || streq(s, "…") ||
		    streq(s, "⋯") || streq(s, "⌊") || streq(s, "⌈"))
			return true;
		return false;
	}
	}
	return false;
}

static bool
is_differential(P *p, Tok *t)
{
	return (p->stop & STOP_DIFF) && t->kind == T_WORD && t->s[0] == 'd' && t->s[1] &&
	    (t->s[2] == 0 || greek_char(t->s + 1));
}

/* binding power of the token in infix/postfix position; 0 stops */
static int
led_bp(P *p)
{
	Tok *t = peek(p);
	const OpDef *o;

	if (t->kind == T_EOF)
		return 0;
	if (t->kind == T_SUP || t->kind == T_SUB || t->kind == T_ACC)
		return 70;
	if (t->kind == T_SYM) {
		const char *s = t->s;
		if (streq(s, ")") || streq(s, "]") || streq(s, "}") || streq(s, "⟩"))
			return 0;
		if (streq(s, "^") || streq(s, "_") || streq(s, "!") || streq(s, "'") || streq(s, "''") ||
		    streq(s, "′") || streq(s, "″"))
			return 70;
		if (streq(s, "/"))
			return t->sp && peek2(p)->sp ? 42 : 50;	/* "a / b c" groups loosely */
		if (streq(s, ","))
			return 3;
		if (streq(s, ";"))
			return 2;
		if (streq(s, "&"))
			return 1;
		if (streq(s, ":"))
			return 8;
		if (streq(s, "|")) {
			if (p->stop & STOP_BAR)
				return 0;
			return p->braces > 0 ? 8 : 20;
		}
		if ((streq(s, "||") || streq(s, "‖")) && (p->stop & STOP_NORM))
			return 0;
	}
	if (t->kind == T_WORD) {
		const char *w = t->s;
		if (((p->stop & STOP_TO) && streq(w, "to")) || ((p->stop & STOP_FROM) && streq(w, "from")) ||
		    ((p->stop & STOP_OF) && streq(w, "of")) || ((p->stop & STOP_AS) && streq(w, "as")) ||
		    ((p->stop & STOP_FOR) && streq(w, "for")) || ((p->stop & STOP_IF) && (streq(w, "if") ||
		    streq(w, "otherwise") || streq(w, "when") || streq(w, "unless"))))
			return 0;
		if (is_differential(p, t))
			return 0;
		if (streq(w, "squared") || streq(w, "cubed") || streq(w, "factorial") || streq(w, "inverse") ||
		    streq(w, "transpose"))
			return 70;
		if (streq(w, "over"))
			return 30;
		if (streq(w, "choose"))
			return 50;
		if (streq(w, "and"))
			return 16;
		if (streq(w, "or"))
			return 14;
		if (streq(w, "if") || streq(w, "otherwise") || streq(w, "when") || streq(w, "where") ||
		    streq(w, "for") || streq(w, "unless"))
			return 5;
		if (streq(w, "dot"))
			return 50;
		if (streq(w, "not") && is(peek2(p), "in"))
			return 20;
		if ((streq(w, "goes") || streq(w, "tends")) && is(peek2(p), "to"))
			return 20;
		if (streq(w, "is") || streq(w, "from") || streq(w, "of") || streq(w, "as"))
			return 0;
	}
	if ((o = find_op(t->s)) != NULL && (t->kind == T_SYM || t->kind == T_WORD))
		return o->lbp;
	/* juxtaposition: "2x" binds tighter than "/", "1/2 x" looser */
	if (starts_operand(p, t))
		return t->sp ? 45 : 60;
	return 0;
}

/* superscript, subscript and limit arguments: one atom, grouping removed */
static M *
script_arg(P *p)
{
	Tok *t = peek(p);
	int saved = p->stop;

	p->stop = 0;
	if (is(t, "(") || is(t, "{")) {
		M *m = strip(nud(p));
		if (m && m->kind == M_FENCE && streq(m->open, "{") && m->sem && streq(m->sem, "set") && is(t, "{"))
			m = m->a;
		p->stop = saved;
		return m;
	}
	if (is(t, "-") || is(t, "+") || is(t, "−") || is(t, "+-") || is(t, "±")) {
		advance(p);
		M *r = mrow();
		const OpDef *o = find_op(t->s);
		M *u = op(o ? o->uni : t->s, ORD);
		u->unary = true;
		madd(r, u);
		madd(r, script_arg(p));
		p->stop = saved;
		return r;
	}
	if (is(t, "*") || is(t, "∗")) {
		advance(p);
		p->stop = saved;
		return op("∗", ORD);
	}
	if (t->kind == T_WORD && strlen(t->s) >= 3 && !greek_char(t->s) && !ord_char(t->s) &&
	    !find_mdef(t->s) && !find_op(t->s) && !is_operator_name(t->s) &&
	    !streq(t->s, "infinity") && !streq(t->s, "sqrt") && !streq(t->s, "sum") && !streq(t->s, "hat") &&
	    !streq(t->s, "bar") && !streq(t->s, "vec") && !streq(t->s, "text")) {
		/* a word: x_max, T_eff */
		bool plain = true;
		for (const char *c = t->s; *c; c++)
			if (!((*c | 32) >= 'a' && (*c | 32) <= 'z'))
				plain = false;
		if (plain) {
			advance(p);
			p->stop = saved;
			return ident(t->s, V_NORMAL);
		}
	}
	if (t->kind == T_EOF || (t->kind == T_SYM && (is(t, ")") || is(t, "]") || is(t, "}") || is(t, ",")))) {
		perr(p, t, "expected a script after ^ or _");
		p->stop = saved;
		return mrow();
	}
	M *m = nud(p);
	p->stop = saved;
	return m;
}

static M *
attach_script(P *p, M *left, bool sup, M *arg, Tok *t)
{
	if (left && left->kind == M_BIGOP) {
		M **slot = sup ? &left->c : &left->b;
		if (*slot)
			perr(p, t, sup ? "this operator already has an upper limit" : "this operator already has a lower limit");
		*slot = arg;
		return left;
	}
	if (left && left->kind == M_SCRIPT && !(sup ? left->c : left->b)) {
		if (sup)
			left->c = arg;
		else
			left->b = arg;
		return left;
	}
	if (left && left->kind == M_SCRIPT && sup && left->prime && left->c) {
		/* f'^2 */
		M *r = mrow();
		madd(r, left->c);
		madd(r, arg);
		left->c = r;
		left->prime = false;
		return left;
	}
	if (left && left->kind == M_SCRIPT && (sup ? left->c : left->b))
		perr(p, t, sup ? "double superscript: group with parentheses, e.g. x^(a^b)" :
		    "double subscript: group with parentheses, e.g. x_(i_j)");
	M *s = mnew(M_SCRIPT);
	s->a = left;
	if (sup)
		s->c = arg;
	else
		s->b = arg;
	s->limits = left && left->kind == M_ID && left->func &&
	    in_list(limit_names, NELEM(limit_names), left->text);
	return s;
}

static M *
num_node(const char *s)
{
	M *m = mnew(M_NUM);

	m->text = xstrdup(s);
	return m;
}

/* parse "(a, b; c, d)" contents after the opening token; returns cells */
typedef struct {
	VEC(M *) cells;
	VEC(int) seps;	/* separator after each cell: ',' ';' or 0 */
} Items;

static Items
group_items(P *p, const char *open, const char **closer)
{
	Items it = {0};
	int saved = p->stop, sb = p->braces;
	bool sf = p->func_arg;
	Tok *opener = p->i > 0 ? &p->t[p->i - 1] : peek(p);

	p->stop = 0;
	p->func_arg = false;
	if (streq(open, "{"))
		p->braces++;
	else
		p->braces = 0;
	for (;;) {
		Tok *t = peek(p);
		if (is(t, ")") || is(t, "]") || is(t, "}") || is(t, "⟩") || t->kind == T_EOF)
			break;
		M *e = expr(p, 3);
		VPUSH(it.cells, e);
		if (is(peek(p), ",")) {
			advance(p);
			VPUSH(it.seps, ',');
		} else if (is(peek(p), ";")) {
			advance(p);
			VPUSH(it.seps, ';');
		} else {
			VPUSH(it.seps, 0);
			if (!(is(peek(p), ")") || is(peek(p), "]") || is(peek(p), "}") || is(peek(p), "⟩") ||
			    peek(p)->kind == T_EOF)) {
				perr(p, peek(p), fmt("unexpected '%s' in group", peek(p)->s));
				advance(p);
			}
		}
	}
	Tok *c = peek(p);
	*closer = "";
	if (c->kind == T_EOF) {
		perr(p, opener, fmt("this '%s' is never closed", open));
	} else {
		*closer = c->s;
		advance(p);
	}
	p->stop = saved;
	p->braces = sb;
	p->func_arg = sf;
	return it;
}

static M *
table_from_items(Items *it, const char *sem, const char *open, const char *close)
{
	M *t = mnew(M_TABLE);
	int cols = 0, cur = 0, rows = 1;

	for (size_t i = 0; i < it->cells.n; i++) {
		cur++;
		if (it->seps.v[i] == ';' || i + 1 == it->cells.n) {
			cols = MAX(cols, cur);
			cur = 0;
			if (it->seps.v[i] == ';' && i + 1 < it->cells.n)
				rows++;
		}
	}
	t->rows = rows;
	t->cols = cols;
	cur = 0;
	VEC(M *) cells = {0};
	for (size_t i = 0; i < it->cells.n; i++) {
		VPUSH(cells, it->cells.v[i]);
		cur++;
		if (it->seps.v[i] == ';' || i + 1 == it->cells.n) {
			while (cur < cols) {
				VPUSH(cells, mrow());
				cur++;
			}
			cur = 0;
		}
	}
	t->kids.v = cells.v;
	t->kids.n = cells.n;
	t->kids.cap = cells.cap;
	t->sem = xstrdup(sem);
	t->colalign = xcalloc((size_t)cols + 1, 1);
	memset(t->colalign, 'c', (size_t)cols);
	if (*open || *close)
		return fence(open, t, close);
	return t;
}

/* Split a case row "x if x >= 0" into value and condition cells. */
static void
case_cells(M *row, M **value, M **cond)
{
	*value = row;
	*cond = mrow();
	if (row->kind != M_ROW)
		return;
	for (size_t i = 0; i < row->kids.n; i++) {
		M *k = row->kids.v[i];
		bool sep = k->kind == M_TEXT && (streq(k->text, "if") || streq(k->text, "otherwise") ||
		    streq(k->text, "when") || streq(k->text, "unless") || streq(k->text, "for"));
		bool comma = k->kind == M_OP && streq(k->text, ",") && i > 0;
		if (!sep && !comma)
			continue;
		M *v = mrow(), *c = mrow();
		for (size_t j = 0; j < i; j++)
			madd(v, row->kids.v[j]);
		for (size_t j = comma ? i + 1 : i; j < row->kids.n; j++)
			madd(c, row->kids.v[j]);
		*value = v->kids.n == 1 ? v->kids.v[0] : v;
		*cond = c->kids.n == 1 ? c->kids.v[0] : c;
		/* nested rows from infix parsing: "x if c" parses as ((x) if c) */
		return;
	}
	if (row->kids.n == 3 && row->kids.v[1]->kind == M_TEXT) {
		*value = row->kids.v[0];
		M *c = mrow();
		madd(c, row->kids.v[1]);
		madd(c, row->kids.v[2]);
		*cond = c;
	}
}

typedef VEC(M *) MVec;

static M *
cases_table(MVec *rows)
{
	M *t = mnew(M_TABLE);

	t->rows = (int)rows->n;
	t->cols = 2;
	for (size_t i = 0; i < rows->n; i++) {
		M *v, *c;
		case_cells(rows->v[i], &v, &c);
		VPUSH(t->kids, v);
		VPUSH(t->kids, c);
	}
	t->sem = "cases";
	t->colalign = "ll";
	M *f = fence("{", t, "");
	f->sem = "cases";
	return f;
}

static M *sub_parse(P *p, const char *src, int min_bp, int col_off);

/* split a matrix row written on its own line into cells */
static StrVec
row_cells(const char *s)
{
	StrVec v = {0};
	bool has_amp = strchr(s, '&') != NULL;
	int depth = 0;
	const char *start = s;
	bool has_comma = false, wide = false;

	for (const char *q = s; *q; q++) {
		if (*q == '(' || *q == '[' || *q == '{')
			depth++;
		else if (*q == ')' || *q == ']' || *q == '}')
			depth--;
		else if (*q == ',' && depth == 0)
			has_comma = true;
		else if (*q == ' ' && q[1] == ' ' && depth == 0)
			wide = true;
	}
	depth = 0;
	for (const char *q = s;; q++) {
		bool split = false;
		if (*q == '(' || *q == '[' || *q == '{')
			depth++;
		else if (*q == ')' || *q == ']' || *q == '}')
			depth--;
		if (*q == 0)
			split = true;
		else if (depth == 0 && has_amp)
			split = *q == '&';
		else if (depth == 0 && has_comma)
			split = *q == ',';
		else if (depth == 0 && wide)
			split = *q == ' ' && q[1] == ' ' && q > start && q[-1] != ' ';
		else if (depth == 0 && *q == ' ' && q > start && q[-1] != ' ')
			split = true;
		if (split) {
			char *cell = trim(xstrndup(start, (size_t)(q - start)));
			if (*cell)
				VPUSH(v, cell);
			if (*q == 0)
				break;
			start = q + 1;
		}
	}
	return v;
}

static M *
pending_rows(P *p, const char *kind, Tok *t)
{
	if (p->nrows == 0) {
		bool cases = streq(kind, "cases") || streq(kind, "system");
		perr(p, t, fmt("'%s' needs rows: write them on the following lines, indented further, "
		    "or inline as %s", kind, cases ? "cases(x if x >= 0; -x otherwise)" : fmt("%s(a, b; c, d)", kind)));
		return mrow();
	}
	char **rows = p->rows;
	int n = p->nrows;
	p->rows = NULL;
	p->nrows = 0;
	if (streq(kind, "cases") || streq(kind, "system")) {
		MVec rs = {0};
		for (int i = 0; i < n; i++) {
			/* "x    if x >= 0": two or more blanks separate value and condition */
			const char *gap = strstr(rows[i], "  ");
			M *r;
			if (strchr(rows[i], '&')) {
				char *c = strchr(rows[i], '&');
				M *row = mrow();
				madd(row, sub_parse(p, xstrndup(rows[i], (size_t)(c - rows[i])), 0, 0));
				M *cond = sub_parse(p, c + 1, 0, 0);
				M *tc = mrow();
				madd(tc, cond);
				r = row;
				M *t2 = mnew(M_ROW);
				madd(t2, row);
				madd(t2, text_node(","));
				madd(t2, tc);
				r = t2;
			} else if (gap) {
				M *row = mrow();
				madd(row, sub_parse(p, xstrndup(rows[i], (size_t)(gap - rows[i])), 0, 0));
				madd(row, text_node(","));
				madd(row, sub_parse(p, gap, 0, 0));
				r = row;
			} else {
				r = sub_parse(p, rows[i], 0, 0);
			}
			/* normalize the "," marker used above into a split point */
			if (r->kind == M_ROW && r->kids.n == 3 && r->kids.v[1]->kind == M_TEXT && streq(r->kids.v[1]->text, ",")) {
				M *v = r->kids.v[0], *c = r->kids.v[2];
				r = mrow();
				madd(r, v);
				M *sep = op(",", PUNCT);
				madd(r, sep);
				madd(r, c);
			}
			VPUSH(rs, r);
		}
		M *f = cases_table(&rs);
		return f;
	}
	Items it = {0};
	for (int i = 0; i < n; i++) {
		StrVec cells = row_cells(rows[i]);
		for (size_t k = 0; k < cells.n; k++) {
			VPUSH(it.cells, sub_parse(p, cells.v[k], 0, 0));
			VPUSH(it.seps, k + 1 == cells.n ? ';' : ',');
		}
	}
	const char *o = "(", *c = ")";
	if (streq(kind, "bmatrix")) o = "[", c = "]";
	else if (streq(kind, "vmatrix")) o = "|", c = "|";
	else if (streq(kind, "Vmatrix")) o = "‖", c = "‖";
	else if (streq(kind, "Bmatrix")) o = "{", c = "}";
	else if (streq(kind, "aligned")) o = "", c = "";
	return table_from_items(&it, "matrix", o, c);
}

/* function application: name already parsed; handles f(x), sin x, log of x */
static M *
apply(P *p, M *f, bool prefix_ok)
{
	Tok *t = peek(p);

	if (is(t, "(")) {
		M *arg = nud(p);
		M *r = mrow();
		madd(r, f);
		madd(r, arg);
		r->apply = true;
		return r;
	}
	if (is(t, "of") && prefix_ok) {
		advance(p);
		bool sf = p->func_arg;
		p->func_arg = true;
		M *arg = expr(p, 44);
		p->func_arg = sf;
		M *r = mrow();
		madd(r, f);
		madd(r, arg);
		r->apply = true;
		return r;
	}
	if (prefix_ok && starts_operand(p, t) && !is(t, "|")) {
		bool sf = p->func_arg;
		p->func_arg = true;
		M *arg = expr(p, 44);
		p->func_arg = sf;
		M *r = mrow();
		madd(r, f);
		madd(r, arg);
		r->apply = true;
		return r;
	}
	return f;
}

/* postfix scripts and primes directly on a function name: f', f^(-1), log_2 */
static M *
func_scripts(P *p, M *f)
{
	for (;;) {
		Tok *t = peek(p);
		if (t->sp)
			break;
		if (is(t, "'") || is(t, "′") || is(t, "''") || is(t, "″")) {
			advance(p);
			M *pr = op(strlen(t->s) == 2 || streq(t->s, "″") ? "″" : "′", ORD);
			f = attach_script(p, f, true, pr, t);
			f->prime = true;
		} else if (is(t, "^") || is(t, "_")) {
			advance(p);
			f = attach_script(p, f, is(t, "^"), script_arg(p), t);
		} else if (t->kind == T_SUP || t->kind == T_SUB) {
			advance(p);
			f = attach_script(p, f, t->kind == T_SUP, sub_parse(p, t->s, 0, 0), t);
		} else {
			break;
		}
	}
	return f;
}

/* limits clause "for k from 1 to n", "for x in S", "as x approaches 0" */
static void
limits_clause(P *p, M *big)
{
	int saved = p->stop;

	for (;;) {
		Tok *t = peek(p);
		if (is(t, "for") && !(is(peek2(p), "all") || is(peek2(p), "every") || is(peek2(p), "each"))) {
			advance(p);
			p->stop = STOP_TO | STOP_FROM | STOP_FOR | STOP_AS;
			M *v = expr(p, 4);
			if (accept(p, "from")) {
				M *a = expr(p, 20);
				big->b = infix(v, op("=", REL), a);
				if (accept(p, "to"))
					big->c = expr(p, 20);
			} else {
				big->b = v;
				if (accept(p, "to"))
					big->c = expr(p, 20);
			}
		} else if (is(t, "from") && big->kind == M_BIGOP) {
			advance(p);
			p->stop = STOP_TO | STOP_FROM | STOP_FOR | STOP_AS;
			big->b = expr(p, 20);
			if (accept(p, "to"))
				big->c = expr(p, 20);
		} else if (is(t, "as")) {
			advance(p);
			p->stop = STOP_FROM | STOP_FOR | STOP_AS | STOP_TO;
			M *v = expr(p, 21);
			Tok *a = peek(p);
			if (accept(p, "approaches") || accept(p, "->") || accept(p, "→") || accept(p, "to") ||
			    ((is(a, "goes") || is(a, "tends")) && (advance(p), accept(p, "to")))) {
				M *x = expr(p, 21);
				if (is(peek(p), "from")) {
					advance(p);
					accept(p, "the");
					Tok *side = advance(p);
					bool minus = is(side, "left") || is(side, "below");
					M *s = mnew(M_SCRIPT);
					s->a = x;
					s->c = op(minus ? "−" : "+", ORD);
					x = s;
				}
				big->b = infix(v, op("→", REL), x);
			} else {
				big->b = v;
			}
		} else {
			break;
		}
	}
	p->stop = saved;
}

static M *
bigop(P *p, const char *glyph, int kind)
{
	/* kind: 0 sum-like, 1 integral, 2 limit-like (word) */
	M *m = mnew(M_BIGOP);
	int saved = p->stop;

	m->text = xstrdup(glyph);
	m->limits = kind != 1;
	m->largeop = kind != 2;
	m->cls = OPC;
	for (;;) {
		Tok *t = peek(p);
		if (is(t, "_")) {
			advance(p);
			m->b = script_arg(p);
		} else if (is(t, "^")) {
			advance(p);
			m->c = script_arg(p);
		} else if (t->kind == T_SUP || t->kind == T_SUB) {
			advance(p);
			M *s = sub_parse(p, t->s, 0, 0);
			if (t->kind == T_SUP)
				m->c = s;
			else
				m->b = s;
		} else {
			break;
		}
	}
	if (is(peek(p), "from")) {
		advance(p);
		p->stop = STOP_TO | STOP_OF | STOP_FROM;
		m->b = expr(p, 4);
		if (accept(p, "to"))
			m->c = expr(p, 20);
		p->stop = saved;
	} else if (is(peek(p), "over") && kind == 0) {
		advance(p);
		p->stop = STOP_OF;
		m->b = expr(p, 4);
		p->stop = saved;
	} else if (is(peek(p), "as") && kind == 2) {
		limits_clause(p, m);
	}
	accept(p, "of");
	p->stop = (kind == 1 ? STOP_DIFF : 0) | STOP_FOR | STOP_AS | (saved & (STOP_BAR | STOP_NORM));
	Tok *bt = peek(p);
	M *body = NULL;
	if (starts_operand(p, bt) || is(bt, "-") || is(bt, "−"))
		body = expr(p, 41);	/* below a spaced / (42), above + (40) */
	if (kind == 1) {
		M *r = mrow();
		madd(r, body);
		while (is_differential(p, peek(p))) {
			Tok *d = advance(p);
			M *sp = mnew(M_SPACE);
			sp->num = 167;
			madd(r, sp);
			M *dd = ident("d", V_NORMAL);
			dd->differential = true;
			madd(r, dd);
			const char *g = greek_char(d->s + 1);
			madd(r, ident(g ? g : d->s + 1, V_ITALIC));
		}
		body = r;
	}
	p->stop = saved;
	m->a = body;
	limits_clause(p, m);
	return m;
}

static M *
apply_variant(P *p, M *m, int variant, Tok *t)
{
	if (m == NULL)
		return m;
	if (m->kind == M_ID || m->kind == M_NUM) {
		m->variant = variant;
		if (m->kind == M_NUM) {
			M *i = ident(m->text, variant);
			return i;
		}
		return m;
	}
	if (m->kind == M_ROW) {
		for (size_t i = 0; i < m->kids.n; i++)
			m->kids.v[i] = apply_variant(p, m->kids.v[i], variant, t);
		return m;
	}
	if (m->kind == M_SCRIPT || m->kind == M_ACCENT) {
		m->a = apply_variant(p, m->a, variant, t);
		return m;
	}
	if (m->kind == M_FENCE) {
		m->a = apply_variant(p, m->a, variant, t);
		return m;
	}
	return m;
}

static M *
prefix_arg(P *p)
{
	/* argument of sqrt, hat, abs ...: a group (parentheses removed) or an atom */
	Tok *t = peek(p);

	if (is(t, "(")) {
		M *g = nud(p);
		return strip(g);
	}
	if (is(t, "of")) {
		advance(p);
		return expr(p, 55);
	}
	return expr(p, 69);
}

static M *
accent(P *p, const char *acc)
{
	M *m = mnew(M_ACCENT);

	m->a = prefix_arg(p);
	m->text = xstrdup(acc);
	return m;
}

static const struct {
	const char *name, *mark;
} accents[] = {
	{"hat", "̂"}, {"widehat", "̂"}, {"tilde", "̃"}, {"widetilde", "̃"},
	{"bar", "̅"}, {"vec", "⃗"}, {"dot", "̇"}, {"ddot", "̈"}, {"check", "̌"},
	{"breve", "̆"}, {"acute", "́"}, {"grave", "̀"}, {"ring", "̊"},
};

static const struct {
	const char *name;
	int variant;
} styles[] = {
	{"bb", V_BB}, {"cal", V_CAL}, {"script", V_SCRIPT}, {"frak", V_FRAK}, {"bold", V_BOLD},
	{"bf", V_BOLD}, {"bolditalic", V_BOLDITALIC}, {"sans", V_SANS}, {"mono", V_MONO},
	{"tt", V_MONO}, {"upright", V_NORMAL}, {"rm", V_NORMAL}, {"italic", V_ITALIC},
};

static M *
word(P *p, Tok *t)
{
	const char *w = t->s;
	const char *u;

	for (Bind *b = p->env; b; b = b->next)
		if (streq(b->name, w))
			return b->val;

	MacroDef *md = find_mdef(w);
	if (md) {
		VEC(M *) args = {0};
		if (md->params.n) {
			if (is(peek(p), "(")) {
				advance(p);
				const char *closer;
				Items it = group_items(p, "(", &closer);
				for (size_t i = 0; i < it.cells.n; i++)
					VPUSH(args, it.cells.v[i]);
			} else {
				VPUSH(args, expr(p, 69));
			}
			if (args.n != md->params.n) {
				perr(p, t, fmt("%s takes %zu argument%s, but %zu %s given", md->name, md->params.n,
				    md->params.n == 1 ? "" : "s", args.n, args.n == 1 ? "was" : "were"));
				while (args.n < md->params.n)
					VPUSH(args, mrow());
			}
		}
		if (p->depth > 32) {
			perr(p, t, fmt("definition of %s uses itself", md->name));
			return mrow();
		}
		P q = *p;
		Bind *env = NULL;
		for (size_t i = 0; i < md->params.n; i++) {
			Bind *b = NEW(Bind);
			b->name = md->params.v[i];
			b->val = args.v[i];
			b->next = env;
			env = b;
		}
		TokVec toks = tokenize(md->body, md->line, md->col);
		q.src = md->body;
		q.line = md->line;
		q.col = md->col;
		q.t = toks.v;
		q.n = (int)toks.n;
		q.i = 0;
		q.env = env;
		q.depth = p->depth + 1;
		q.stop = 0;
		q.rows = NULL;
		q.nrows = 0;
		M *body = expr(&q, 0);
		if (peek(&q)->kind != T_EOF)
			perr(&q, peek(&q), fmt("unexpected '%s' in the definition of %s", peek(&q)->s, md->name));
		M *g = mrow();
		madd(g, body);
		g->group = true;
		if (md->readas) {
			Buf sp = {0};
			for (const char *c = md->readas; *c;) {
				if (*c == '{') {
					const char *e = strchr(c, '}');
					size_t k;
					for (k = 0; e && k < md->params.n; k++)
						if (strlen(md->params.v[k]) == (size_t)(e - c - 1) &&
						    strncmp(md->params.v[k], c + 1, (size_t)(e - c - 1)) == 0)
							break;
					if (e && k < md->params.n) {
						bputs(&sp, eqn_speech(args.v[k]));
						c = e + 1;
						continue;
					}
				}
				bputc(&sp, *c++);
			}
			g->speech = bstr(&sp);
		}
		return g;
	}

	/* letters with digits: x1 -> x sub 1 */
	if ((strlen(w) == 1 || greek_char(w)) && peek(p)->kind == T_NUM && !peek(p)->sp) {
		Tok *d = advance(p);
		M *base = ident(greek_char(w) ? greek_char(w) : w, greek_char(w) && w[0] >= 'A' && w[0] <= 'Z' ? V_NORMAL : V_ITALIC);
		M *s = mnew(M_SCRIPT);
		s->a = base;
		s->b = num_node(d->s);
		return s;
	}

	if ((u = greek_char(w)) != NULL)
		return ident(u, w[0] >= 'A' && w[0] <= 'Z' ? V_NORMAL : V_ITALIC);
	if ((u = ord_char(w)) != NULL) {
		if (streq(w, "Re") || streq(w, "Im")) {
			M *f = ident(w, V_NORMAL);
			f->func = true;
			return apply(p, func_scripts(p, f), true);
		}
		return ident(u, V_NORMAL);
	}
	for (size_t i = 0; i < NELEM(blackboard); i++)
		if (streq(blackboard[i].name, w)) {
			char l[2] = {blackboard[i].letter, 0};
			M *m = ident(l, V_BB);
			if (streq(w, "EE")) {
				m->func = true;
				return apply(p, func_scripts(p, m), false);
			}
			return m;
		}
	for (size_t i = 0; i < NELEM(styles); i++)
		if (streq(styles[i].name, w)) {
			M *arg = prefix_arg(p);
			return apply_variant(p, arg, styles[i].variant, t);
		}
	for (size_t i = 0; i < NELEM(accents); i++)
		if (streq(accents[i].name, w)) {
			if (streq(w, "bar")) {
				M *a = prefix_arg(p);
				if (!is_atom(a) || (a->kind == M_ID && utf8_len(a->text) > 1)) {
					M *o = mnew(M_OVERLINE);
					o->a = a;
					return o;
				}
				M *m = mnew(M_ACCENT);
				m->a = a;
				m->text = "̅";
				return m;
			}
			return accent(p, accents[i].mark);
		}
	if (streq(w, "overline") || streq(w, "conj")) {
		M *o = mnew(M_OVERLINE);
		o->a = prefix_arg(p);
		return o;
	}
	if (streq(w, "text")) {
		Tok *s = peek(p);
		if (s->kind == T_STR) {
			advance(p);
			return text_node(s->s);
		}
		perr(p, s, "expected quoted text after 'text', e.g. text \"if\"");
		return mrow();
	}
	if (streq(w, "sqrt") || (streq(w, "square") && is(peek(p), "root"))) {
		if (streq(w, "square")) {
			advance(p);
			accept(p, "of");
			M *m = mnew(M_SQRT);
			m->a = strip(expr(p, 55));
			return m;
		}
		M *m = mnew(M_SQRT);
		m->a = prefix_arg(p);
		return m;
	}
	if ((streq(w, "cube") || streq(w, "fourth")) && is(peek(p), "root")) {
		advance(p);
		accept(p, "of");
		M *m = mnew(M_ROOT);
		m->a = strip(expr(p, 55));
		m->b = num_node(streq(w, "cube") ? "3" : "4");
		return m;
	}
	if (streq(w, "root")) {
		M *m = mnew(M_ROOT);
		if (is(peek(p), "(")) {
			advance(p);
			const char *closer;
			Items it = group_items(p, "(", &closer);
			if (it.cells.n != 2) {
				perr(p, t, "root takes two arguments: root(n, x)");
				return mrow();
			}
			m->b = it.cells.v[0];
			m->a = it.cells.v[1];
			return m;
		}
		int saved = p->stop;
		p->stop |= STOP_OF;
		m->b = expr(p, 69);
		p->stop = saved;
		if (!accept(p, "of"))
			perr(p, peek(p), "expected 'of': root n of x");
		m->a = strip(expr(p, 55));
		return m;
	}
	if (streq(w, "abs") || streq(w, "norm") || streq(w, "floor") || streq(w, "ceil") ||
	    (streq(w, "absolute") && is(peek(p), "value"))) {
		if (streq(w, "absolute")) {
			advance(p);
			accept(p, "of");
		}
		M *a = prefix_arg(p);
		M *f;
		if (streq(w, "norm"))
			f = fence("‖", a, "‖");
		else if (streq(w, "floor"))
			f = fence("⌊", a, "⌋");
		else if (streq(w, "ceil"))
			f = fence("⌈", a, "⌉");
		else
			f = fence("|", a, "|");
		f->sem = streq(w, "absolute") ? "abs" : xstrdup(w);
		return f;
	}
	if (streq(w, "binom")) {
		if (!accept(p, "(")) {
			perr(p, peek(p), "binom takes two arguments: binom(n, k)");
			return mrow();
		}
		const char *closer;
		Items it = group_items(p, "(", &closer);
		if (it.cells.n != 2) {
			perr(p, t, "binom takes two arguments: binom(n, k)");
			return mrow();
		}
		M *fr = mnew(M_FRAC);
		fr->a = it.cells.v[0];
		fr->b = it.cells.v[1];
		fr->thin = true;
		M *f = fence("(", fr, ")");
		f->sem = "binom";
		return f;
	}
	if (streq(w, "cases") || streq(w, "system") || streq(w, "matrix") || streq(w, "pmatrix") ||
	    streq(w, "bmatrix") || streq(w, "vmatrix") || streq(w, "Vmatrix") || streq(w, "Bmatrix") ||
	    streq(w, "aligned")) {
		if (is(peek(p), "(")) {
			advance(p);
			if (streq(w, "cases") || streq(w, "system")) {
				int saved = p->stop;
				MVec rows = {0};
				for (;;) {
					p->stop = 0;
					M *r = expr(p, 2);
					VPUSH(rows, r);
					if (!accept(p, ";"))
						break;
				}
				p->stop = saved;
				if (!accept(p, ")"))
					perr(p, peek(p), "missing ')' after cases");
				return cases_table(&rows);
			}
			const char *closer;
			Items it = group_items(p, "(", &closer);
			const char *o = "(", *c = ")";
			if (streq(w, "bmatrix")) o = "[", c = "]";
			else if (streq(w, "vmatrix")) o = "|", c = "|";
			else if (streq(w, "Vmatrix")) o = "‖", c = "‖";
			else if (streq(w, "Bmatrix")) o = "{", c = "}";
			return table_from_items(&it, "matrix", o, c);
		}
		return pending_rows(p, w, t);
	}
	if (streq(w, "sum")) return bigop(p, "∑", 0);
	if (streq(w, "prod") || streq(w, "product")) return bigop(p, "∏", 0);
	if (streq(w, "coprod")) return bigop(p, "∐", 0);
	if (streq(w, "bigcup") || streq(w, "Union") || streq(w, "union")) return bigop(p, "⋃", 0);
	if (streq(w, "bigcap") || streq(w, "Intersection") || streq(w, "intersection")) return bigop(p, "⋂", 0);
	if (streq(w, "integral") || streq(w, "int")) return bigop(p, "∫", 1);
	if (streq(w, "iint")) return bigop(p, "∬", 1);
	if (streq(w, "iiint")) return bigop(p, "∭", 1);
	if (streq(w, "oint")) return bigop(p, "∮", 1);
	if (streq(w, "limit")) return bigop(p, "lim", 2);
	if (in_list(limit_names, NELEM(limit_names), w)) {
		Tok *n = peek(p);
		if (is(n, "(") && !streq(w, "lim")) {
			M *f = ident(w, V_NORMAL);
			f->func = true;
			return apply(p, f, true);
		}
		return bigop(p, w, 2);
	}
	if (streq(w, "for") && (accept(p, "all") || accept(p, "every") || accept(p, "each")))
		return op("∀", ORD);
	if (streq(w, "forall"))
		return op("∀", ORD);
	if (streq(w, "exists") || (streq(w, "there") && (accept(p, "exists") || accept(p, "exist") ||
	    (is(peek(p), "is") && (advance(p), true))))) {
		if (accept(p, "unique") || accept(p, "!"))
			return op("∃!", ORD);
		return op("∃", ORD);
	}
	if (streq(w, "not") || streq(w, "neg") || streq(w, "lnot")) {
		M *r = mrow();
		madd(r, op("¬", ORD));
		madd(r, expr(p, 18));
		return r;
	}
	if (streq(w, "if") || streq(w, "otherwise") || streq(w, "and") || streq(w, "or") ||
	    streq(w, "when") || streq(w, "where") || streq(w, "unless") || streq(w, "prime") ||
	    streq(w, "odd") || streq(w, "even"))
		return text_node(w);

	/* function names */
	if (is_operator_name(w)) {
		M *f = ident(w, V_NORMAL);
		f->func = true;
		return apply(p, func_scripts(p, f), true);
	}
	if (is_user_function(w) || (strlen(w) == 1 && strchr("fghFGH", w[0]))) {
		M *f = ident(w, strlen(w) == 1 ? V_ITALIC : V_NORMAL);
		f->func = true;
		f = func_scripts(p, f);
		Tok *n = peek(p);
		if (is(n, "(") && !n->sp)
			return apply(p, f, false);
		if (is(n, "(") && is_user_function(w))
			return apply(p, f, false);
		return f;
	}
	if (strlen(w) > 1 && (unsigned char)w[0] < 0x80) {
		/* an unknown word: a product of single-letter variables */
		M *r = mrow();
		r->times = true;
		for (const char *c = w; *c; c++) {
			char l[2] = {*c, 0};
			madd(r, ident(l, V_ITALIC));
		}
		return r;
	}
	/* single letters, Unicode letters */
	u32 cp = utf8_first(w);
	bool upright_default = (cp >= 0x391 && cp <= 0x3a9) || (cp >= 0x1d400 && cp <= 0x1d7ff) ||
	    (cp >= 0x2100 && cp <= 0x214f);
	return ident(w, upright_default ? V_NORMAL : V_ITALIC);
}

static M *
nud(P *p)
{
	Tok *t = advance(p);

	switch (t->kind) {
	case T_NUM:
		return num_node(t->s);
	case T_STR:
		return text_node(t->s);
	case T_WORD:
		return word(p, t);
	case T_SUP: case T_SUB: {
		M *s = mnew(M_SCRIPT);
		s->a = mrow();
		if (t->kind == T_SUP)
			s->c = sub_parse(p, t->s, 0, 0);
		else
			s->b = sub_parse(p, t->s, 0, 0);
		return s;
	}
	case T_EOF:
		perr(p, t, "expected an expression");
		return mrow();
	case T_ACC:
		perr(p, t, "an accent needs something to sit on");
		return mrow();
	}
	const char *s = t->s;
	if (streq(s, "(") || streq(s, "[") || streq(s, "{") || streq(s, "⟨")) {
		const char *closer;
		Items it = group_items(p, s, &closer);
		bool semi = false;
		for (size_t i = 0; i < it.seps.n; i++)
			semi |= it.seps.v[i] == ';';
		const char *close = *closer ? closer : (streq(s, "(") ? ")" : streq(s, "[") ? "]" : streq(s, "{") ? "}" : "⟩");
		if (semi && !streq(s, "{"))
			return table_from_items(&it, "matrix", s, close);
		M *body;
		if (it.cells.n == 1 && streq(s, "(") && it.cells.v[0]->kind == M_FENCE && it.cells.v[0]->sem &&
		    streq(it.cells.v[0]->sem, "binom"))
			return it.cells.v[0];	/* (n choose k) */
		if (it.cells.n == 1) {
			body = it.cells.v[0];
		} else {
			body = mrow();
			for (size_t i = 0; i < it.cells.n; i++) {
				madd(body, it.cells.v[i]);
				if (it.seps.v[i])
					madd(body, op(it.seps.v[i] == ',' ? "," : ";", PUNCT));
			}
		}
		M *f = fence(s, body, close);
		if (streq(s, "{"))
			f->sem = "set";
		return f;
	}
	if (streq(s, "|") || streq(s, "||") || streq(s, "‖")) {
		bool norm = !streq(s, "|");
		int saved = p->stop;
		p->stop = (p->stop & ~(STOP_BAR | STOP_NORM)) | (norm ? STOP_NORM : STOP_BAR);
		M *a = expr(p, 0);
		p->stop = saved;
		if (!accept(p, s))
			perr(p, peek(p), norm ? "missing closing ‖ (or ||)" : "missing closing |");
		M *f = fence(norm ? "‖" : "|", a, norm ? "‖" : "|");
		f->sem = norm ? "norm" : "abs";
		return f;
	}
	if (streq(s, "⌊") || streq(s, "⌈")) {
		const char *cl = streq(s, "⌊") ? "⌋" : "⌉";
		M *a = expr(p, 0);
		if (!accept(p, cl))
			perr(p, peek(p), fmt("missing closing %s", cl));
		M *f = fence(s, a, cl);
		f->sem = streq(s, "⌊") ? "floor" : "ceil";
		return f;
	}
	if (streq(s, "-") || streq(s, "−") || streq(s, "+") || streq(s, "+-") || streq(s, "±") ||
	    streq(s, "-+") || streq(s, "∓")) {
		const OpDef *o = find_op(s);
		M *r = mrow();
		M *u = op(o->uni, ORD);
		u->unary = true;
		madd(r, u);
		Tok *n = peek(p);
		if (n->kind == T_EOF || is(n, ")") || is(n, "]") || is(n, ","))
			return r;
		madd(r, expr(p, 55));
		return r;
	}
	if (streq(s, "∑")) return bigop(p, s, 0);
	if (streq(s, "∏") || streq(s, "∐") || streq(s, "⋃") || streq(s, "⋂") ||
	    streq(s, "⨁") || streq(s, "⨂"))
		return bigop(p, s, 0);
	if (streq(s, "∫") || streq(s, "∬") || streq(s, "∭") || streq(s, "∮"))
		return bigop(p, s, 1);
	if (streq(s, "√") || streq(s, "∛") || streq(s, "∜")) {
		M *m = mnew(streq(s, "√") ? M_SQRT : M_ROOT);
		m->a = prefix_arg(p);
		if (m->kind == M_ROOT)
			m->b = num_node(streq(s, "∛") ? "3" : "4");
		return m;
	}
	if (streq(s, "¬")) {
		M *r = mrow();
		madd(r, op(s, ORD));
		madd(r, expr(p, 18));
		return r;
	}
	if (streq(s, "...")) {
		Tok *prev = p->i >= 2 ? &p->t[p->i - 2] : NULL;
		bool low = (prev && is(prev, ",")) || is(peek(p), ",") || peek(p)->kind == T_EOF ||
		    is(peek(p), ")") || is(peek(p), "]") || is(peek(p), "}");
		return op(low ? "…" : "⋯", ORD);
	}
	if (streq(s, "&"))
		return mnew(M_ALIGN);
	if (streq(s, "'") || streq(s, "′")) {
		return op("′", ORD);
	}
	const OpDef *o = find_op(s);
	if (o && (o->cls == REL || o->cls == BIN)) {
		/* a line that starts with a relation: "= b^2 + 2ab" */
		M *r = mrow();
		M *empty = mrow();
		empty->group = true;	/* an empty left operand keeps the spacing */
		madd(r, empty);
		madd(r, op(o->uni, o->cls));
		Tok *n = peek(p);
		if (n->kind != T_EOF && !is(n, ")") && !is(n, "]"))
			madd(r, expr(p, o->lbp));
		return r;
	}
	if (streq(s, ",") || streq(s, ";") || streq(s, ":") || streq(s, ".") || streq(s, "!"))
		return op(s, streq(s, "!") ? CLOSE : PUNCT);
	if (streq(s, ")") || streq(s, "]") || streq(s, "}")) {
		perr(p, t, fmt("unmatched '%s'", s));
		return mrow();
	}
	if (streq(s, "^") || streq(s, "_")) {
		/* a script with no base, as in ^14C */
		M *sc = mnew(M_SCRIPT);
		sc->a = mrow();
		if (streq(s, "^"))
			sc->c = script_arg(p);
		else
			sc->b = script_arg(p);
		return sc;
	}
	/* other symbols: ordinary characters (∞, ∂, ∀, ∃, ∅ ...) */
	u32 cp = utf8_first(s);
	if (cp == 0x2200 || cp == 0x2203 || cp == 0x2204)
		return op(s, ORD);
	if (cp >= 0x80)
		return ident(s, V_NORMAL);
	perr(p, t, fmt("unexpected '%s'", s));
	return mrow();
}

static M *
led(P *p, M *left, int bp)
{
	Tok *t = peek(p);
	const char *s = t->s;

	if (t->kind == T_SUP || t->kind == T_SUB) {
		advance(p);
		return attach_script(p, left, t->kind == T_SUP, sub_parse(p, t->s, 0, 0), t);
	}
	if (t->kind == T_ACC) {
		advance(p);
		M *m = mnew(M_ACCENT);
		m->a = left;
		m->text = xstrdup(s);
		return m;
	}
	if (is(t, "^") || is(t, "_")) {
		advance(p);
		return attach_script(p, left, is(t, "^"), script_arg(p), t);
	}
	if (is(t, "'") || is(t, "′") || is(t, "''") || is(t, "″")) {
		advance(p);
		M *pr = op(strlen(s) == 2 || streq(s, "″") ? "″" : "′", ORD);
		M *r = attach_script(p, left, true, pr, t);
		r->prime = true;
		return r;
	}
	if (is(t, "!") || is(t, "factorial")) {
		advance(p);
		M *r = mrow();
		madd(r, left);
		madd(r, op("!", CLOSE));
		return r;
	}
	if (is(t, "squared") || is(t, "cubed")) {
		advance(p);
		return attach_script(p, left, true, num_node(is(t, "squared") ? "2" : "3"), t);
	}
	if (is(t, "inverse")) {
		advance(p);
		M *r = mrow();
		M *u = op("−", ORD);
		u->unary = true;
		madd(r, u);
		madd(r, num_node("1"));
		return attach_script(p, left, true, r, t);
	}
	if (is(t, "transpose")) {
		advance(p);
		return attach_script(p, left, true, ident("T", V_ITALIC), t);
	}
	if (is(t, "/")) {
		advance(p);
		M *right = expr(p, bp);
		M *f = mnew(M_FRAC);
		f->a = strip(left);
		f->b = strip(right);
		return f;
	}
	if (is(t, "over")) {
		advance(p);
		M *right = expr(p, 30);
		M *f = mnew(M_FRAC);
		f->a = strip(left);
		f->b = strip(right);
		return f;
	}
	if (is(t, "choose")) {
		advance(p);
		M *right = expr(p, 50);
		M *fr = mnew(M_FRAC);
		fr->a = strip(left);
		fr->b = strip(right);
		fr->thin = true;
		M *f = fence("(", fr, ")");
		f->sem = "binom";
		return f;
	}
	if (is(t, ",") || is(t, ";")) {
		advance(p);
		Tok *n = peek(p);
		M *r = mrow();
		madd(r, left);
		madd(r, op(s, PUNCT));
		if (n->kind != T_EOF && !is(n, ")") && !is(n, "]") && !is(n, "}"))
			madd(r, expr(p, bp));
		return r;
	}
	if (is(t, "&")) {
		advance(p);
		M *r = mrow();
		madd(r, left);
		madd(r, mnew(M_ALIGN));
		if (peek(p)->kind != T_EOF)
			madd(r, expr(p, 1));
		return r;
	}
	if (is(t, ":")) {
		advance(p);
		/* f: A -> B uses a colon; {x : x > 0} a relation */
		bool colon = left && (left->kind == M_ID) && p->braces == 0;
		M *r = infix(left, op(":", colon ? PUNCT : REL), expr(p, 8));
		return r;
	}
	if (is(t, "|")) {
		advance(p);
		return infix(left, op("∣", REL), expr(p, bp));
	}
	if (t->kind == T_WORD && (streq(s, "and") || streq(s, "or") || streq(s, "if") || streq(s, "when") ||
	    streq(s, "where") || streq(s, "unless") || streq(s, "otherwise") || streq(s, "for"))) {
		advance(p);
		Buf w = {0};
		bputs(&w, s);
		if (streq(s, "for")) {
			Tok *q = peek(p);
			if (is(q, "all") || is(q, "every") || is(q, "each") || is(q, "some") || is(q, "any")) {
				bprintf(&w, " %s", q->s);
				advance(p);
			}
		}
		M *r = mrow();
		madd(r, left);
		madd(r, text_node(bstr(&w)));
		if (!streq(s, "otherwise") && peek(p)->kind != T_EOF && !is(peek(p), ";") && !is(peek(p), ")"))
			madd(r, expr(p, bp));
		return r;
	}
	if (is(t, "not") && is(peek2(p), "in")) {
		advance(p);
		advance(p);
		return infix(left, op("∉", REL), expr(p, 20));
	}
	if ((is(t, "goes") || is(t, "tends")) && is(peek2(p), "to")) {
		advance(p);
		advance(p);
		return infix(left, op("→", REL), expr(p, 20));
	}
	if (is(t, "dot")) {
		advance(p);
		return infix(left, op("⋅", BIN), expr(p, 50));
	}
	const OpDef *o = find_op(s);
	if (o && o->lbp) {
		advance(p);
		Tok *n = peek(p);
		if (n->kind == T_EOF || is(n, ")") || is(n, "]") || is(n, "}") || is(n, ",") || is(n, "&")) {
			/* trailing operator, as in a split line: a + */
			return infix(left, op(o->uni, o->cls), NULL);
		}
		return infix(left, op(o->uni, o->cls), expr(p, o->lbp));
	}
	/* juxtaposition */
	if (is_funclike(left) && is(t, "(")) {
		M *arg = nud(p);
		M *r = mrow();
		madd(r, left);
		madd(r, arg);
		r->apply = true;
		return r;
	}
	M *right = expr(p, bp);
	M *r;
	if (left && left->kind == M_ROW && left->times && !left->group) {
		r = left;
	} else {
		r = mrow();
		r->times = true;
		madd(r, left);
	}
	if (right && right->kind == M_ROW && right->times && !right->group) {
		/* one product, however it was spelled: 4ac, 4 a c */
		for (size_t i = 0; i < right->kids.n; i++)
			madd(r, right->kids.v[i]);
	} else {
		madd(r, right);
	}
	return r;
}

static M *
expr(P *p, int min_bp)
{
	if (++p->depth > 200) {
		perr(p, peek(p), "formula nested too deeply");
		p->depth--;
		return mrow();
	}
	M *left = nud(p);
	for (;;) {
		int bp = led_bp(p);
		if (bp <= min_bp)
			break;
		left = led(p, left, bp);
	}
	p->depth--;
	return left;
}

static M *
sub_parse(P *p, const char *src, int min_bp, int col_off)
{
	TokVec toks = tokenize(src, p->line, p->col + col_off);
	P q = *p;

	q.src = src;
	q.t = toks.v;
	q.n = (int)toks.n;
	q.i = 0;
	q.stop = 0;
	q.rows = NULL;
	q.nrows = 0;
	q.col = p->col + col_off;
	M *m = expr(&q, min_bp);
	if (peek(&q)->kind != T_EOF)
		perr(&q, peek(&q), fmt("unexpected '%s'", peek(&q)->s));
	return m;
}

/* f(x) = ... and f: A -> B declare f as a function */
static void
scan_declarations(Tok *t, int n)
{
	if (n >= 3 && t[0].kind == T_WORD && utf8_len(t[0].s) <= 1 + (greek_char(t[0].s) ? 10 : 0) &&
	    is(&t[1], "(") && !t[1].sp) {
		int depth = 0, k;
		for (k = 1; k < n; k++) {
			if (is(&t[k], "("))
				depth++;
			else if (is(&t[k], ")") && --depth == 0)
				break;
		}
		if (k + 1 < n && (is(&t[k + 1], "=") || is(&t[k + 1], ":=") || is(&t[k + 1], "≔")))
			eqn_declare_function(t[0].s);
	}
	if (n >= 3 && t[0].kind == T_WORD && is(&t[1], ":")) {
		for (int k = 2; k < n; k++)
			if (is(&t[k], "->") || is(&t[k], "to") || is(&t[k], "→")) {
				eqn_declare_function(t[0].s);
				break;
			}
	}
}

M *
eqn_parse(const char *src, int line, int col, bool display, char **rows, int nrows)
{
	TokVec toks = tokenize(src, line, col);
	P p;

	memset(&p, 0, sizeof p);
	p.src = src;
	p.line = line;
	p.col = col;
	p.t = toks.v;
	p.n = (int)toks.n;
	p.display = display;
	p.rows = rows;
	p.nrows = nrows;
	scan_declarations(p.t, p.n);
	if (p.t[0].kind == T_EOF)
		return mrow();
	M *m = expr(&p, 0);
	while (peek(&p)->kind != T_EOF) {
		Tok *t = peek(&p);
		perr(&p, t, fmt("unexpected '%s'", t->s));
		advance(&p);
		if (peek(&p)->kind != T_EOF) {
			M *r = mrow();
			madd(r, m);
			madd(r, expr(&p, 0));
			m = r;
		}
	}
	if (p.nrows)
		error_at(line, col, "these indented rows belong to no cases or matrix "
		    "(end the line with 'cases' or 'matrix')");
	return m;
}

/* ---- the stage ---- */

static void
emit_layout(Node *n, M *m, bool display, const char *source)
{
	MNodes nodes = {0};
	Box *b = eqn_layout(m, display, &nodes);
	Buf l = {0};
	bnum(&l, b->w);
	bputc(&l, ' ');
	bnum(&l, b->h);
	bputc(&l, ' ');
	bnum(&l, b->d);
	bputc(&l, ' ');
	if (isnan(b->align))
		bputs(&l, "-");
	else
		bnum(&l, b->align);
	node_add(n, "box", 0, 0, bstr(&l));
	for (size_t i = 0; !display && i + 2 < b->brk.n; i += 3) {
		Buf k = {0};
		bnum(&k, b->brk.v[i]);
		bputc(&k, ' ');
		bnum(&k, b->brk.v[i + 1]);
		bprintf(&k, " %d", (int)b->brk.v[i + 2]);
		node_add(n, "brk", 0, 0, bstr(&k));
	}
	for (size_t i = 0; i < nodes.n; i++) {
		Buf a = {0};
		bprintf(&a, "%zu ", i);
		if (nodes.v[i].parent < 0)
			bputs(&a, "-");
		else
			bprintf(&a, "%d", nodes.v[i].parent);
		bprintf(&a, " %s", nodes.v[i].tag);
		if (nodes.v[i].actual) {
			bputc(&a, ' ');
			pct_escape(&a, nodes.v[i].actual);
		}
		node_add(n, "n", 0, 0, bstr(&a));
	}
	for (size_t i = 0; i < b->items.n; i++) {
		Item *it = &b->items.v[i];
		Buf a = {0};
		if (it->kind == 'r') {
			bnum(&a, it->x);
			bputc(&a, ' ');
			bnum(&a, it->y);
			bputc(&a, ' ');
			bnum(&a, it->w);
			bputc(&a, ' ');
			bnum(&a, it->h);
			node_add(n, "r", 0, 0, bstr(&a));
			continue;
		}
		bprintf(&a, "%s ", it->font == F_TEXT ? "text" : "math");
		bnum(&a, it->scale * 1000);
		bputc(&a, ' ');
		bnum(&a, it->x);
		bputc(&a, ' ');
		bnum(&a, it->y);
		bputc(&a, ' ');
		if (it->kind == 'g') {
			bprintf(&a, "%d ", it->gid);
			bnum(&a, it->adv);
			bputc(&a, ' ');
			if (it->node < 0)
				bputs(&a, "-");
			else
				bprintf(&a, "%d", it->node);
			bputc(&a, ' ');
		}
		pct_escape(&a, it->text);
		node_add(n, it->kind == 'g' ? "g" : "i", 0, 0, bstr(&a));
	}
	node_add(n, "mathml", 0, 0, eqn_mathml(m, display, source));
	node_add(n, "speech", 0, 0, eqn_speech(m));
	node_add(n, "linear", 0, 0, eqn_linear(m));
}

/* the source text of a math node (rest of line, after the label for display lines) */
static void
walk(Node *n)
{
	if (streq(n->kind, "meta") && startswith(n->rest, "font ")) {
		char *v = xstrdup(n->rest + 5);
		char *role = next_field(&v);
		logical_font_override(role, trim(v));
		return;
	}
	if (streq(n->kind, "define")) {
		eqn_define(n->rest, n->line, n->col);
		return;
	}
	if (streq(n->kind, "math")) {
		M *m = eqn_parse(n->rest, n->line, n->col, false, NULL, 0);
		emit_layout(n, m, false, n->rest);
		return;
	}
	if (streq(n->kind, "let")) {
		M *m = eqn_parse(n->rest, n->line, n->col, false, NULL, 0);
		emit_layout(n, m, false, n->rest);
		for (size_t i = 0; i < n->kids.n; i++)
			walk(n->kids.v[i]);
		return;
	}
	if (streq(n->kind, "line")) {
		char *label = field(n, 0);
		char *src = field_rest(n, 1);
		VEC(char *) rows = {0};
		Buf full = {0};
		bputs(&full, src);
		for (size_t i = 0; i < n->kids.n; i++)
			if (streq(n->kids.v[i]->kind, "cont")) {
				VPUSH(rows, n->kids.v[i]->rest);
				bprintf(&full, "\n%s", n->kids.v[i]->rest);
			}
		int col = n->col + (streq(label, "-") ? 0 : 0);
		M *m = eqn_parse(src, n->line, col, true, rows.v, (int)rows.n);
		emit_layout(n, m, true, bstr(&full));
		return;
	}
	for (size_t i = 0; i < n->kids.n; i++)
		walk(n->kids.v[i]);
}

int
eqn_main(FILE *in, FILE *out, const char *file)
{
	diag_file = file;
	Node *doc = tree_read(in);
	walk(doc);
	tree_write(out, doc, 0);
	return nerrors ? 1 : 0;
}
