/*
 * eqnsem.c -- what a formula means, in three renderings:
 *
 *	MathML	for assistive technology and other software
 *	speech	the formula read aloud, used as the PDF /Alt text
 *	linear	plain Unicode text for copy and paste, which tekx
 *		itself reads back to the same formula
 */
#include "eqn.h"

/* ---- shared ---- */

static const struct {
	const char *op, *speech;
} opnames[] = {
	{"=", "equals"}, {"≠", "is not equal to"}, {"<", "is less than"}, {">", "is greater than"},
	{"≤", "is less than or equal to"}, {"≥", "is greater than or equal to"},
	{"≪", "is much less than"}, {"≫", "is much greater than"}, {"∼", "is similar to"},
	{"≅", "is congruent to"}, {"≈", "is approximately equal to"}, {"≡", "is equivalent to"},
	{"∝", "is proportional to"}, {"∈", "is in"}, {"∉", "is not in"}, {"∋", "contains"},
	{"⊂", "is a subset of"}, {"⊆", "is a subset of or equal to"}, {"⊃", "is a superset of"},
	{"⊇", "is a superset of or equal to"}, {"→", "to"}, {"←", "from"},
	{"↔", "if and only if"}, {"↦", "maps to"}, {"≔", "is defined as"},
	{"∣", "divides"}, {"⟂", "is perpendicular to"}, {"∥", "is parallel to"},
	{"⇒", "implies"}, {"⟹", "implies"}, {"⇔", "if and only if"},
	{"⟺", "if and only if"}, {"⇐", "is implied by"}, {"∧", "and"}, {"∨", "or"},
	{"+", "plus"}, {"−", "minus"}, {"±", "plus or minus"}, {"∓", "minus or plus"},
	{"∪", "union"}, {"∖", "set minus"}, {"⊕", "direct sum"}, {"⊖", "circled minus"},
	{"∩", "intersect"}, {"⋅", "times"}, {"×", "times"}, {"÷", "divided by"},
	{"∘", "composed with"}, {"⊗", "tensor"}, {"⊙", "circled dot"}, {"⋆", "star"},
	{"∗", "star"}, {"mod", "modulo"}, {"!", "factorial"}, {"′", "prime"},
	{"″", "double prime"}, {"∀", "for all"}, {"∃", "there exists"},
	{"∃!", "there exists a unique"}, {"¬", "not"}, {"∞", "infinity"}, {"∂", "partial"},
	{"∇", "nabla"}, {"∅", "the empty set"}, {"…", "dot dot dot"}, {"⋯", "dot dot dot"},
	{"⋮", "vertical dots"}, {"⋱", "diagonal dots"}, {"°", "degrees"},
	{"∴", "therefore"}, {"∵", "because"}, {"⊢", "proves"}, {"⊨", "models"},
	{"≃", "is asymptotically equal to"}, {"≺", "precedes"}, {"≻", "succeeds"},
	{"ℵ", "aleph"}, {"ℏ", "h bar"}, {"ℓ", "ell"}, {"∠", "angle"},
	{"⊤", "top"}, {"⊥", "bottom"}, {",", ","}, {";", ";"}, {".", "."},
};

const char *
op_speech(const char *op)
{
	for (size_t i = 0; i < NELEM(opnames); i++)
		if (streq(opnames[i].op, op))
			return opnames[i].speech;
	return op;
}

static void
xml_escape(Buf *b, const char *s)
{
	for (; *s; s++) {
		switch (*s) {
		case '&': bputs(b, "&amp;"); break;
		case '<': bputs(b, "&lt;"); break;
		case '>': bputs(b, "&gt;"); break;
		case '"': bputs(b, "&quot;"); break;
		case '\n': bputs(b, "&#10;"); break;
		default: bputc(b, *s);
		}
	}
}

/* Mathematical Alphanumeric Symbols, with the holes filled from Letterlike Symbols */
static u32
alnum(u32 c, int variant)
{
	static const struct { u32 c; int v; u32 to; } holes[] = {
		{'h', V_ITALIC, 0x210e}, {'B', V_CAL, 0x212c}, {'E', V_CAL, 0x2130}, {'F', V_CAL, 0x2131},
		{'H', V_CAL, 0x210b}, {'I', V_CAL, 0x2110}, {'L', V_CAL, 0x2112}, {'M', V_CAL, 0x2133},
		{'R', V_CAL, 0x211b}, {'e', V_CAL, 0x212f}, {'g', V_CAL, 0x210a}, {'o', V_CAL, 0x2134},
		{'C', V_FRAK, 0x212d}, {'H', V_FRAK, 0x210c}, {'I', V_FRAK, 0x2111}, {'R', V_FRAK, 0x211c},
		{'Z', V_FRAK, 0x2128}, {'C', V_BB, 0x2102}, {'H', V_BB, 0x210d}, {'N', V_BB, 0x2115},
		{'P', V_BB, 0x2119}, {'Q', V_BB, 0x211a}, {'R', V_BB, 0x211d}, {'Z', V_BB, 0x2124},
	};
	static const struct { int v; u32 upper, lower, digit; } base[] = {
		{V_BOLD, 0x1d400, 0x1d41a, 0x1d7ce}, {V_ITALIC, 0x1d434, 0x1d44e, 0},
		{V_BOLDITALIC, 0x1d468, 0x1d482, 0x1d7ce}, {V_CAL, 0x1d49c, 0x1d4b6, 0},
		{V_SCRIPT, 0x1d49c, 0x1d4b6, 0}, {V_FRAK, 0x1d504, 0x1d51e, 0}, {V_BB, 0x1d538, 0x1d552, 0x1d7d8},
		{V_SANS, 0x1d5a0, 0x1d5ba, 0x1d7e2}, {V_MONO, 0x1d670, 0x1d68a, 0x1d7f6},
	};
	int v = variant == V_SCRIPT ? V_CAL : variant;

	if (variant == V_NORMAL)
		return c;
	for (size_t i = 0; i < NELEM(holes); i++)
		if (holes[i].c == c && holes[i].v == v)
			return holes[i].to;
	for (size_t i = 0; i < NELEM(base); i++) {
		if (base[i].v != variant)
			continue;
		if (c >= 'A' && c <= 'Z')
			return base[i].upper + (c - 'A');
		if (c >= 'a' && c <= 'z')
			return base[i].lower + (c - 'a');
		if (c >= '0' && c <= '9' && base[i].digit)
			return base[i].digit + (c - '0');
	}
	/* Greek */
	if (variant == V_ITALIC || variant == V_BOLD || variant == V_BOLDITALIC) {
		u32 cap = variant == V_ITALIC ? 0x1d6e2 : variant == V_BOLD ? 0x1d6a8 : 0x1d71c;
		u32 low = cap + 26;
		if (c >= 0x391 && c <= 0x3a9 && c != 0x3a2)
			return cap + (c - 0x391);
		if (c >= 0x3b1 && c <= 0x3c9)
			return low + (c - 0x3b1);
		static const u32 extra[] = {0x2202, 0x3f5, 0x3d1, 0x3f0, 0x3d5, 0x3f1, 0x3d6};
		for (size_t i = 0; i < NELEM(extra); i++)
			if (c == extra[i])
				return low + 25 + (u32)i;
	}
	return c;
}

/* the character an identifier is drawn with */
char *id_char(M *m);
char *
id_char(M *m)
{
	if (utf8_len(m->text) != 1)
		return m->text;
	u32 c = utf8_first(m->text);
	Buf b = {0};
	utf8_enc(&b, alnum(c, m->variant));
	return bstr(&b);
}

/* ---- MathML ---- */

static const char *
accent_spacing(const char *mark)
{
	static const char *map[][2] = {
		{"̂", "^"}, {"̃", "~"}, {"̅", "¯"}, {"̄", "¯"}, {"⃗", "→"},
		{"̇", "˙"}, {"̈", "¨"}, {"̌", "ˇ"}, {"̆", "˘"},
		{"́", "´"}, {"̀", "`"}, {"̊", "˚"},
	};
	for (size_t i = 0; i < NELEM(map); i++)
		if (streq(map[i][0], mark))
			return map[i][1];
	return mark;
}

static void mml(Buf *b, M *m);

static void
mml_row_kids(Buf *b, M *m)
{
	for (size_t i = 0; i < m->kids.n; i++) {
		if (i > 0 && m->times)
			bputs(b, "<mo>⁢</mo>");
		if (i > 0 && m->apply)
			bputs(b, "<mo>⁡</mo>");
		mml(b, m->kids.v[i]);
	}
}

static void
mml(Buf *b, M *m)
{
	if (m == NULL) {
		bputs(b, "<mrow/>");
		return;
	}
	switch (m->kind) {
	case M_ROW: {
		size_t real = 0;
		for (size_t i = 0; i < m->kids.n; i++)
			real += m->kids.v[i]->kind != M_ALIGN;
		if (real == 1 && m->kids.n == 1 && !m->times && !m->apply) {
			mml(b, m->kids.v[0]);
			return;
		}
		if (m->kids.n == 0) {
			bputs(b, "<mrow/>");
			return;
		}
		bputs(b, "<mrow>");
		mml_row_kids(b, m);
		bputs(b, "</mrow>");
		return;
	}
	case M_ID: {
		const char *c = id_char(m);
		u32 f = utf8_first(c);
		bool upright1 = utf8_len(c) == 1 && m->variant == V_NORMAL &&
		    (((f | 32) >= 'a' && (f | 32) <= 'z') || (f >= 0x391 && f <= 0x3c9) || f == 0x2202 || f == 0x2207);
		bputs(b, upright1 ? "<mi mathvariant=\"normal\">" : "<mi>");
		xml_escape(b, c);
		bputs(b, "</mi>");
		return;
	}
	case M_NUM:
		bputs(b, "<mn>");
		xml_escape(b, m->text);
		bputs(b, "</mn>");
		return;
	case M_OP:
		bputs(b, m->unary ? "<mo form=\"prefix\">" : "<mo>");
		xml_escape(b, m->text);
		bputs(b, "</mo>");
		return;
	case M_TEXT:
		bputs(b, "<mtext>");
		xml_escape(b, m->text);
		bputs(b, "</mtext>");
		return;
	case M_FRAC:
		bputs(b, m->thin ? "<mfrac linethickness=\"0\">" : "<mfrac>");
		mml(b, m->a);
		mml(b, m->b);
		bputs(b, "</mfrac>");
		return;
	case M_SCRIPT: {
		const char *tag = m->limits ? (m->b && m->c ? "munderover" : m->b ? "munder" : "mover") :
		    (m->b && m->c ? "msubsup" : m->b ? "msub" : "msup");
		bprintf(b, "<%s>", tag);
		mml(b, m->a);
		if (m->b)
			mml(b, m->b);
		if (m->c)
			mml(b, m->c);
		bprintf(b, "</%s>", tag);
		return;
	}
	case M_SQRT:
		bputs(b, "<msqrt>");
		mml(b, m->a);
		bputs(b, "</msqrt>");
		return;
	case M_ROOT:
		bputs(b, "<mroot>");
		mml(b, m->a);
		mml(b, m->b);
		bputs(b, "</mroot>");
		return;
	case M_FENCE:
		bputs(b, "<mrow>");
		if (*m->open) {
			bputs(b, "<mo fence=\"true\">");
			xml_escape(b, m->open);
			bputs(b, "</mo>");
		}
		mml(b, m->a);
		if (*m->close) {
			bputs(b, "<mo fence=\"true\">");
			xml_escape(b, m->close);
			bputs(b, "</mo>");
		}
		bputs(b, "</mrow>");
		return;
	case M_BIGOP: {
		const char *tag = m->limits ? (m->b && m->c ? "munderover" : m->b ? "munder" : m->c ? "mover" : NULL) :
		    (m->b && m->c ? "msubsup" : m->b ? "msub" : m->c ? "msup" : NULL);
		bputs(b, "<mrow>");
		if (tag)
			bprintf(b, "<%s>", tag);
		bputs(b, m->largeop ? "<mo largeop=\"true\">" : "<mo>");
		xml_escape(b, m->text);
		bputs(b, "</mo>");
		if (tag) {
			if (m->b)
				mml(b, m->b);
			if (m->c)
				mml(b, m->c);
			bprintf(b, "</%s>", tag);
		}
		if (m->a)
			mml(b, m->a);
		bputs(b, "</mrow>");
		return;
	}
	case M_ACCENT:
		bputs(b, "<mover accent=\"true\">");
		mml(b, m->a);
		bputs(b, "<mo>");
		xml_escape(b, accent_spacing(m->text));
		bputs(b, "</mo></mover>");
		return;
	case M_OVERLINE:
		bputs(b, "<mover accent=\"true\">");
		mml(b, m->a);
		bputs(b, "<mo>¯</mo></mover>");
		return;
	case M_TABLE: {
		bool left = m->colalign && m->colalign[0] == 'l';
		bputs(b, left ? "<mtable columnalign=\"left\">" : "<mtable>");
		for (int r = 0; r < m->rows; r++) {
			bputs(b, "<mtr>");
			for (int c = 0; c < m->cols; c++) {
				bputs(b, "<mtd>");
				mml(b, m->kids.v[(size_t)(r * m->cols + c)]);
				bputs(b, "</mtd>");
			}
			bputs(b, "</mtr>");
		}
		bputs(b, "</mtable>");
		return;
	}
	case M_SPACE:
		bputs(b, "<mspace width=\"");
		bnum(b, m->num / 1000);
		bputs(b, "em\"/>");
		return;
	case M_ALIGN:
		return;
	}
}

char *
eqn_mathml(M *m, bool display, const char *source)
{
	Buf b = {0};

	bprintf(&b, "<math xmlns=\"http://www.w3.org/1998/Math/MathML\" display=\"%s\" alttext=\"",
	    display ? "block" : "inline");
	xml_escape(&b, eqn_speech(m));
	bputs(&b, "\"><semantics>");
	Buf body = {0};
	mml(&body, m);
	if (startswith(bstr(&body), "<mrow>") || startswith(bstr(&body), "<mrow/>"))
		bputs(&b, bstr(&body));
	else
		bprintf(&b, "<mrow>%s</mrow>", bstr(&body));
	bputs(&b, "<annotation encoding=\"application/x-tekx\">");
	xml_escape(&b, source);
	bputs(&b, "</annotation></semantics></math>");
	return bstr(&b);
}

/* ---- speech ---- */

static void say(Buf *b, M *m);

static void
word(Buf *b, const char *w)
{
	if (b->n && b->s[b->n - 1] != ' ')
		bputc(b, ' ');
	bputs(b, w);
}

static const struct {
	const char *name, *speech;
} fnames[] = {
	{"sin", "sine"}, {"cos", "cosine"}, {"tan", "tangent"}, {"cot", "cotangent"}, {"sec", "secant"},
	{"csc", "cosecant"}, {"sinh", "hyperbolic sine"}, {"cosh", "hyperbolic cosine"},
	{"tanh", "hyperbolic tangent"}, {"arcsin", "arc sine"}, {"arccos", "arc cosine"},
	{"arctan", "arc tangent"}, {"ln", "natural log"}, {"log", "log"}, {"exp", "exp"},
	{"det", "determinant"}, {"dim", "dimension"}, {"ker", "kernel"}, {"deg", "degree"},
	{"lim", "the limit"}, {"max", "the maximum"}, {"min", "the minimum"}, {"sup", "the supremum"},
	{"inf", "the infimum"}, {"liminf", "the limit inferior"}, {"limsup", "the limit superior"},
	{"argmax", "the arg max"}, {"argmin", "the arg min"}, {"gcd", "the gcd"}, {"lcm", "the lcm"},
	{"Pr", "the probability"}, {"sgn", "sign"}, {"tr", "trace"}, {"Tr", "trace"},
};

static const char *
fname(const char *f)
{
	for (size_t i = 0; i < NELEM(fnames); i++)
		if (streq(fnames[i].name, f))
			return fnames[i].speech;
	return f;
}

static const char *
greek_name(u32 c)
{
	static const char *low[] = {"alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta",
	    "iota", "kappa", "lambda", "mu", "nu", "xi", "omicron", "pi", "rho", "final sigma", "sigma",
	    "tau", "upsilon", "phi", "chi", "psi", "omega"};
	if (c >= 0x3b1 && c <= 0x3c9)
		return low[c - 0x3b1];
	if (c >= 0x391 && c <= 0x3a9 && c != 0x3a2)
		return fmt("capital %s", low[c - 0x391]);
	switch (c) {
	case 0x3d1: return "theta";
	case 0x3d5: return "phi";
	case 0x3d6: return "pi";
	case 0x3f1: return "rho";
	case 0x3f5: return "epsilon";
	}
	return NULL;
}

static void
say_id(Buf *b, M *m)
{
	if (m->func) {
		word(b, fname(m->text));
		return;
	}
	if (utf8_len(m->text) > 1) {
		word(b, m->text);
		return;
	}
	u32 c = utf8_first(m->text);
	const char *g = greek_name(c);
	if (m->variant == V_BB) {
		static const char *sets[][2] = {{"R", "the real numbers"}, {"N", "the natural numbers"},
		    {"Z", "the integers"}, {"Q", "the rational numbers"}, {"C", "the complex numbers"},
		    {"E", "the expected value"}};
		for (size_t i = 0; i < NELEM(sets); i++)
			if (streq(sets[i][0], m->text)) {
				word(b, sets[i][1]);
				return;
			}
		word(b, "double-struck");
	} else if (m->variant == V_CAL || m->variant == V_SCRIPT) {
		word(b, "script");
	} else if (m->variant == V_FRAK) {
		word(b, "fraktur");
	} else if (m->variant == V_BOLD || m->variant == V_BOLDITALIC) {
		word(b, "bold");
	}
	if (g)
		word(b, g);
	else if (c >= 'A' && c <= 'Z' && m->variant != V_BB)
		word(b, fmt("capital %s", m->text));
	else {
		const char *o = op_speech(m->text);
		word(b, o);
	}
}

static bool
simple(M *m)
{
	if (m == NULL)
		return true;
	if (m->kind == M_ROW && m->kids.n == 1)
		return simple(m->kids.v[0]);
	if (m->kind == M_ROW && m->kids.n == 2 && m->kids.v[0]->kind == M_OP && m->kids.v[0]->unary)
		return simple(m->kids.v[1]);
	return m->kind == M_ID || m->kind == M_NUM || (m->kind == M_OP && m->cls == ORD);
}

static void
say_power(Buf *b, M *sup, M *base)
{
	char *s = eqn_speech(sup);

	if (streq(s, "2"))
		word(b, "squared");
	else if (streq(s, "3"))
		word(b, "cubed");
	else if (sup->kind == M_OP && streq(sup->text, "′"))
		word(b, "prime");
	else if (sup->kind == M_OP && streq(sup->text, "″"))
		word(b, "double prime");
	else if (base && base->kind == M_ID && base->func && streq(s, "negative 1"))
		word(b, "inverse");
	else if (sup->kind == M_ID && streq(sup->text, "T"))
		word(b, "transpose");
	else if (simple(sup))
		word(b, fmt("to the power %s", s));
	else
		word(b, fmt("raised to the power %s, end power", s));
}

static void
say_fence_body(Buf *b, M *m)
{
	say(b, m->a);
}

static void
say(Buf *b, M *m)
{
	if (m == NULL)
		return;
	if (m->speech) {
		word(b, m->speech);
		return;
	}
	switch (m->kind) {
	case M_ROW:
		if (m->apply && m->kids.n == 2) {
			M *f = m->kids.v[0], *arg = m->kids.v[1];
			say(b, f);
			if (arg->kind == M_FENCE && streq(arg->open, "(")) {
				word(b, "of");
				say_fence_body(b, arg);
			} else {
				say(b, arg);
			}
			return;
		}
		for (size_t i = 0; i < m->kids.n; i++)
			say(b, m->kids.v[i]);
		return;
	case M_ID:
		say_id(b, m);
		return;
	case M_NUM:
		word(b, m->text);
		return;
	case M_OP:
		if (m->unary && streq(m->text, "−"))
			word(b, "negative");
		else if (streq(m->text, ":") && m->cls == PUNCT)
			word(b, "from");
		else if (streq(m->text, ":"))
			word(b, "such that");
		else if (streq(m->text, ",") || streq(m->text, ";")) {
			bputs(b, m->text);
		} else
			word(b, op_speech(m->text));
		return;
	case M_TEXT:
		word(b, m->text);
		return;
	case M_FRAC:
		if (m->thin) {
			say(b, m->a);
			word(b, "choose");
			say(b, m->b);
		} else if (simple(m->a) && simple(m->b)) {
			say(b, m->a);
			word(b, "over");
			say(b, m->b);
		} else {
			word(b, "the fraction with numerator");
			say(b, m->a);
			word(b, "and denominator");
			say(b, m->b);
			word(b, "end fraction");
		}
		return;
	case M_SCRIPT:
		say(b, m->a);
		if (m->b) {
			if (m->limits) {
				word(b, "as");
				say(b, m->b);
			} else if (simple(m->b)) {
				word(b, "sub");
				say(b, m->b);
			} else {
				word(b, "sub");
				say(b, m->b);
				word(b, "end sub");
			}
		}
		if (m->c)
			say_power(b, m->c, m->a);
		return;
	case M_SQRT:
		word(b, "the square root of");
		say(b, m->a);
		if (!simple(m->a))
			word(b, "end root");
		return;
	case M_ROOT: {
		char *i = eqn_speech(m->b);
		if (streq(i, "3"))
			word(b, "the cube root of");
		else if (streq(i, "4"))
			word(b, "the fourth root of");
		else
			word(b, fmt("the %s-th root of", i));
		say(b, m->a);
		if (!simple(m->a))
			word(b, "end root");
		return;
	}
	case M_FENCE:
		if (m->sem && streq(m->sem, "abs")) {
			word(b, "the absolute value of");
			say(b, m->a);
		} else if (m->sem && streq(m->sem, "norm")) {
			word(b, "the norm of");
			say(b, m->a);
		} else if (m->sem && streq(m->sem, "floor")) {
			word(b, "the floor of");
			say(b, m->a);
		} else if (m->sem && streq(m->sem, "ceil")) {
			word(b, "the ceiling of");
			say(b, m->a);
		} else if (m->sem && streq(m->sem, "binom")) {
			say(b, m->a);
		} else if (m->sem && streq(m->sem, "set")) {
			M *body = m->a;
			size_t bar = 0;
			for (size_t i = 0; body && body->kind == M_ROW && i < body->kids.n; i++) {
				M *k = body->kids.v[i];
				if (k->kind == M_OP && k->cls == REL && (streq(k->text, ":") || streq(k->text, "\u2223")))
					bar = i;
			}
			if (bar) {
				word(b, "the set of all");
				Buf l = {0};
				for (size_t i = 0; i < bar; i++)
					say(&l, body->kids.v[i]);
				char *ls = bstr(&l), *in = strstr(ls, " is in ");
				if (in) {
					*in = 0;
					word(b, fmt("%s in %s", trim(ls), in + 7));
				} else {
					word(b, trim(ls));
				}
				word(b, "such that");
				for (size_t i = bar + 1; i < body->kids.n; i++)
					say(b, body->kids.v[i]);
			} else {
				word(b, "the set");
				say(b, m->a);
			}
		} else if (m->sem && streq(m->sem, "cases")) {
			say(b, m->a);
		} else if (m->a && m->a->kind == M_TABLE) {
			say(b, m->a);
		} else if (simple(m->a) && streq(m->open, "(")) {
			say(b, m->a);
		} else {
			const char *o = streq(m->open, "(") ? "open paren" : streq(m->open, "[") ? "open bracket" :
			    streq(m->open, "⟨") ? "open angle" : "open";
			const char *c = streq(m->close, ")") ? "close paren" : streq(m->close, "]") ? "close bracket" :
			    streq(m->close, "⟩") ? "close angle" : "close";
			word(b, o);
			say(b, m->a);
			word(b, c);
		}
		return;
	case M_BIGOP: {
		const char *what = streq(m->text, "∑") ? "the sum" : streq(m->text, "∏") ? "the product" :
		    streq(m->text, "∫") ? "the integral" : streq(m->text, "∬") ? "the double integral" :
		    streq(m->text, "∭") ? "the triple integral" : streq(m->text, "∮") ? "the contour integral" :
		    streq(m->text, "⋃") ? "the union" : streq(m->text, "⋂") ? "the intersection" :
		    streq(m->text, "∐") ? "the coproduct" : fname(m->text);
		word(b, what);
		if (streq(m->text, "lim") && m->b) {
			word(b, "as");
			Buf s = {0};
			say(&s, m->b);
			char *t = bstr(&s);
			char *to = strstr(t, " to ");
			if (to) {
				*to = 0;
				word(b, fmt("%s approaches %s", t, to + 4));
			} else {
				word(b, t);
			}
		} else if (m->b && m->c) {
			word(b, "from");
			say(b, m->b);
			word(b, "to");
			say(b, m->c);
		} else if (m->b) {
			word(b, "over");
			say(b, m->b);
		} else if (m->c) {
			word(b, "up to");
			say(b, m->c);
		}
		if (m->a) {
			word(b, "of");
			say(b, m->a);
		}
		return;
	}
	case M_ACCENT: {
		const char *a = m->text;
		if (streq(a, "⃗")) {
			word(b, "vector");
			say(b, m->a);
			return;
		}
		say(b, m->a);
		word(b, streq(a, "̂") ? "hat" : streq(a, "̃") ? "tilde" : streq(a, "̅") ? "bar" :
		    streq(a, "̇") ? "dot" : streq(a, "̈") ? "double dot" : streq(a, "̌") ? "check" :
		    streq(a, "̆") ? "breve" : "accent");
		return;
	}
	case M_OVERLINE:
		say(b, m->a);
		word(b, "bar");
		return;
	case M_TABLE:
		if (m->sem && streq(m->sem, "cases")) {
			for (int r = 0; r < m->rows; r++) {
				if (r)
					bputc(b, ';');
				for (int c = 0; c < m->cols; c++)
					say(b, m->kids.v[(size_t)(r * m->cols + c)]);
			}
			return;
		}
		word(b, fmt("the %d by %d matrix", m->rows, m->cols));
		for (int r = 0; r < m->rows; r++) {
			bputc(b, ';');
			word(b, fmt("row %d:", r + 1));
			for (int c = 0; c < m->cols; c++) {
				if (c)
					bputc(b, ',');
				say(b, m->kids.v[(size_t)(r * m->cols + c)]);
			}
		}
		return;
	case M_SPACE:
	case M_ALIGN:
		return;
	}
}

char *
eqn_speech(M *m)
{
	Buf b = {0};

	say(&b, m);
	char *s = trim(bstr(&b));
	/* tidy punctuation: "a , b" -> "a, b" */
	Buf o = {0};
	for (const char *p = s; *p; p++) {
		if (*p == ' ' && (p[1] == ',' || p[1] == ';'))
			continue;
		if ((*p == ',' || *p == ';') && p[1] && p[1] != ' ') {
			bputc(&o, *p);
			bputc(&o, ' ');
			continue;
		}
		bputc(&o, *p);
	}
	return bstr(&o);
}

/* ---- linear text ---- */

static void lin(Buf *b, M *m, bool compact);

static bool
letterish(u32 c)
{
	return ((c | 32) >= 'a' && (c | 32) <= 'z') || (c >= '0' && c <= '9');
}

static u32
last_cp(Buf *b)
{
	if (b->n == 0)
		return 0;
	size_t k = b->n - 1;
	while (k > 0 && ((unsigned char)b->s[k] & 0xC0) == 0x80)
		k--;
	u32 c;
	utf8_dec(b->s + k, b->n - k, &c);
	return c;
}

static char *
lin_str(M *m, bool compact)
{
	Buf b = {0};

	lin(&b, m, compact);
	return bstr(&b);
}

static bool
lin_atom(M *m)
{
	if (m == NULL)
		return true;
	if (m->kind == M_ROW && m->kids.n == 2 && m->kids.v[1]->kind == M_OP && streq(m->kids.v[1]->text, "!"))
		return lin_atom(m->kids.v[0]);
	if (m->kind == M_ROW)
		return m->kids.n == 1 ? lin_atom(m->kids.v[0]) : (m->apply);
	if (m->kind == M_SCRIPT)
		return lin_atom(m->a);
	return m->kind == M_ID || m->kind == M_NUM || m->kind == M_FENCE || m->kind == M_SQRT ||
	    (m->kind == M_OP && m->cls == ORD && !m->unary) || m->kind == M_ACCENT || m->kind == M_TABLE;
}

static void
lin_paren(Buf *b, M *m, bool compact)
{
	if (lin_atom(m)) {
		lin(b, m, compact);
	} else {
		bputc(b, '(');
		lin(b, m, compact);
		bputc(b, ')');
	}
}

static const char *
to_super(const char *s)
{
	static const char *sup[] = {"⁰", "¹", "²", "³", "⁴", "⁵", "⁶",
	    "⁷", "⁸", "⁹"};
	Buf b = {0};

	for (const char *p = s; *p; p++) {
		if (*p >= '0' && *p <= '9')
			bputs(&b, sup[*p - '0']);
		else if (*p == '-' || startswith(p, "−")) {
			bputs(&b, "⁻");
			if (*p != '-')
				p += 2;
		} else if (*p == '+')
			bputs(&b, "⁺");
		else if (*p == 'n')
			bputs(&b, "ⁿ");
		else if (*p == 'i')
			bputs(&b, "ⁱ");
		else
			return NULL;
	}
	return bstr(&b);
}

static const char *
to_sub(const char *s)
{
	Buf b = {0};

	for (const char *p = s; *p; p++) {
		if (*p >= '0' && *p <= '9')
			utf8_enc(&b, 0x2080 + (u32)(*p - '0'));
		else
			return NULL;
	}
	return bstr(&b);
}

static void
lin_sep(Buf *b, const char *next)
{
	/* keep neighbouring words and numbers apart: "x sin y", "2 3" */
	u32 l = last_cp(b), r = utf8_first(next);

	if (letterish(l) && letterish(r) && !(l >= '0' && l <= '9' && !(r >= '0' && r <= '9')))
		bputc(b, ' ');
}

static void
lin(Buf *b, M *m, bool compact)
{
	if (m == NULL)
		return;
	switch (m->kind) {
	case M_ROW:
		for (size_t i = 0; i < m->kids.n; i++) {
			M *k = m->kids.v[i];
			char *s = lin_str(k, compact);
			if (*s == 0)
				continue;
			bool spaced_op = !compact && k->kind == M_OP && !k->unary && (k->cls == REL || k->cls == BIN);
			if (spaced_op || k->kind == M_TEXT) {
				while (b->n && b->s[b->n - 1] == ' ')
					b->n--;
				if (b->n)
					bputc(b, ' ');
				bputs(b, trim(s));
				bputc(b, ' ');
				continue;
			}
			if (m->apply && i > 0 && !(k->kind == M_FENCE && streq(k->open, "("))) {
				bputc(b, ' ');
			} else if (m->times && i > 0) {
				M *prev = m->kids.v[i - 1];
				bool tight_prev = (prev->kind == M_NUM || (prev->kind == M_ID && !prev->func &&
				    utf8_len(prev->text) == 1) || prev->kind == M_FENCE || prev->kind == M_SCRIPT);
				bool tight_next = (k->kind == M_ID && !k->func && utf8_len(k->text) == 1) ||
				    k->kind == M_FENCE || k->kind == M_SQRT || (k->kind == M_SCRIPT && k->a &&
				    k->a->kind == M_ID && !k->a->func);
				bool letters = prev->kind == M_ID && k->kind == M_ID && !prev->func && !k->func &&
				    utf8_len(prev->text) == 1 && utf8_len(k->text) == 1;
				if (letters || prev->differential)
					;	/* ab, dx */
				else if (tight_prev && tight_next && !(prev->kind == M_NUM && k->kind == M_NUM))
					lin_sep(b, s);
				else if (b->n && b->s[b->n - 1] != ' ')
					bputc(b, ' ');
			} else if (i > 0 && m->kids.v[i - 1]->differential) {
				/* dx */
			} else if (i > 0) {
				lin_sep(b, s);
			}
			bputs(b, s);
		}
		return;
	case M_ID:
		bputs(b, m->variant == V_ITALIC && utf8_len(m->text) == 1 ? m->text : id_char(m));
		return;
	case M_NUM:
		bputs(b, m->text);
		return;
	case M_OP:
		bputs(b, m->text);
		if (m->cls == PUNCT && !compact)
			bputc(b, ' ');
		return;
	case M_TEXT: {
		static const char *kw[] = {"if", "otherwise", "and", "or", "when", "where", "unless", "for all",
		    "for every", "for each", "for some", "for any", "for"};
		bool known = false;
		for (size_t i = 0; i < NELEM(kw); i++)
			known |= streq(kw[i], m->text);
		if (known)
			bputs(b, m->text);
		else
			bprintf(b, "\"%s\"", m->text);
		return;
	}
	case M_FRAC:
		if (m->thin) {
			lin_paren(b, m->a, compact);
			bputs(b, " choose ");
			lin_paren(b, m->b, compact);
			return;
		}
		lin_paren(b, m->a, compact);
		bputc(b, '/');
		lin_paren(b, m->b, compact);
		return;
	case M_SCRIPT: {
		if (m->a && !(m->a->kind == M_ROW && m->a->kids.n == 0))
			lin_paren(b, m->a, compact);
		if (m->b) {
			char *s = lin_str(m->b, true);
			const char *u = to_sub(s);
			if (u && !m->limits)
				bputs(b, u);
			else {
				bputc(b, '_');
				lin_paren(b, m->b, true);
			}
		}
		if (m->c) {
			char *s = lin_str(m->c, true);
			const char *u = to_super(s);
			if (m->prime || (m->c->kind == M_OP && (streq(m->c->text, "′") || streq(m->c->text, "″"))))
				bputs(b, s);
			else if (u && !m->limits)
				bputs(b, u);
			else {
				bputc(b, '^');
				lin_paren(b, m->c, true);
			}
		}
		return;
	}
	case M_SQRT:
		bputs(b, "√");
		lin_paren(b, m->a, compact);
		return;
	case M_ROOT: {
		char *i = lin_str(m->b, true);
		if (streq(i, "3") || streq(i, "4")) {
			bputs(b, streq(i, "3") ? "∛" : "∜");
			lin_paren(b, m->a, compact);
		} else {
			bprintf(b, "root(%s, ", i);
			lin(b, m->a, compact);
			bputc(b, ')');
		}
		return;
	}
	case M_FENCE:
		if (m->sem && streq(m->sem, "binom")) {
			bputc(b, '(');
			lin(b, m->a, compact);
			bputc(b, ')');
			return;
		}
		if (m->sem && streq(m->sem, "cases")) {
			M *t = m->a;
			bputs(b, "cases(");
			for (int r = 0; t && r < t->rows; r++) {
				if (r)
					bputs(b, "; ");
				lin(b, t->kids.v[(size_t)(r * 2)], compact);
				char *c = lin_str(t->kids.v[(size_t)(r * 2 + 1)], compact);
				c = trim(c);
				if (*c) {
					if (!startswith(c, "if ") && !startswith(c, "otherwise") && !startswith(c, "when ") &&
					    !startswith(c, "unless ") && !startswith(c, "for "))
						bputs(b, ", ");
					else
						bputc(b, ' ');
					bputs(b, c);
				}
			}
			bputc(b, ')');
			return;
		}
		if (m->a && m->a->kind == M_TABLE && (streq(m->open, "|") || streq(m->open, "‖") || streq(m->open, "{"))) {
			bputs(b, streq(m->open, "|") ? "vmatrix" : streq(m->open, "{") ? "Bmatrix" : "Vmatrix");
			bputc(b, '(');
			lin(b, m->a, compact);
			bputc(b, ')');
			return;
		}
		bputs(b, m->open);
		lin(b, m->a, compact);
		while (b->n && b->s[b->n - 1] == ' ')
			b->n--;
		bputs(b, m->close);
		return;
	case M_BIGOP:
		bputs(b, m->text);
		if (m->b) {
			bputc(b, '_');
			lin_paren(b, m->b, true);
		}
		if (m->c) {
			bputc(b, '^');
			lin_paren(b, m->c, true);
		}
		if (m->a) {
			char *s = lin_str(m->a, compact);
			if (*s) {
				bputc(b, ' ');
				bputs(b, s);
			}
		}
		return;
	case M_ACCENT:
		if (lin_atom(m->a) && m->a->kind == M_ID) {
			lin(b, m->a, compact);
			bputs(b, m->text);
		} else {
			const char *n = streq(m->text, "̂") ? "hat" : streq(m->text, "̃") ? "tilde" :
			    streq(m->text, "⃗") ? "vec" : streq(m->text, "̇") ? "dot" :
			    streq(m->text, "̈") ? "ddot" : streq(m->text, "̅") ? "bar" : "hat";
			bprintf(b, "%s(", n);
			lin(b, m->a, compact);
			bputc(b, ')');
		}
		return;
	case M_OVERLINE:
		bputs(b, "overline(");
		lin(b, m->a, compact);
		bputc(b, ')');
		return;
	case M_TABLE:
		for (int r = 0; r < m->rows; r++) {
			if (r)
				bputs(b, "; ");
			for (int c = 0; c < m->cols; c++) {
				if (c)
					bputs(b, ", ");
				lin(b, m->kids.v[(size_t)(r * m->cols + c)], compact);
			}
		}
		return;
	case M_SPACE:
		bputc(b, ' ');
		return;
	case M_ALIGN:
		return;
	}
}

char *
eqn_linear(M *m)
{
	Buf b = {0};

	lin(&b, m, false);
	char *s = trim(bstr(&b));
	/* collapse runs of blanks */
	Buf o = {0};
	for (const char *p = s; *p; p++)
		if (!(*p == ' ' && p[1] == ' '))
			bputc(&o, *p);
	return bstr(&o);
}
