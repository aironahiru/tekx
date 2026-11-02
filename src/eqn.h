/*
 * eqn.h -- mathematics: the formula tree shared by the parser, the
 * layout engine and the generators of MathML, speech and linear text.
 */
#ifndef EQN_H
#define EQN_H

#include "tekx.h"

/* TeX's atom classes, which decide the spacing between neighbours */
enum { ORD, OPC, BIN, REL, OPEN, CLOSE, PUNCT, INNER };

enum {
	M_ROW,		/* sequence of kids */
	M_ID,		/* identifier: text */
	M_NUM,		/* number: text */
	M_OP,		/* operator: text, cls */
	M_TEXT,		/* words inside mathematics */
	M_FRAC,		/* a over b; thin = no rule (binomials) */
	M_SCRIPT,	/* base a, sub b, sup c; limits: under and over */
	M_SQRT,		/* a */
	M_ROOT,		/* a, index b */
	M_FENCE,	/* open, a, close */
	M_BIGOP,	/* text (operator), sub b, sup c, body a */
	M_ACCENT,	/* base a, text = accent character */
	M_OVERLINE,	/* a */
	M_TABLE,	/* kids row-major, rows x cols */
	M_SPACE,	/* width in em/1000: num */
	M_ALIGN,	/* & alignment point */
};

/* identifier styles */
enum { V_ITALIC, V_NORMAL, V_BOLD, V_BOLDITALIC, V_BB, V_CAL, V_FRAK, V_SANS, V_MONO, V_SCRIPT };

typedef struct M M;
struct M {
	int kind, cls, variant;
	char *text;
	M *a, *b, *c;
	VEC(M *) kids;
	int rows, cols;
	char *open, *close;	/* fences: "" for none */
	char *colalign;		/* tables: one of l c r per column */
	double num;
	bool limits;		/* scripts drawn under/over */
	bool thin;		/* fraction without rule */
	bool slashed;		/* inline fraction a/b */
	bool func;		/* function name, upright */
	bool apply;		/* row is a function application */
	bool group;		/* row is an explicit group (laid out as one box) */
	bool times;		/* row is an implicit product */
	bool largeop;		/* bigop: drawn larger in display style */
	bool prime;		/* superscript is a prime */
	bool unary;		/* operator used as a prefix */
	bool differential;	/* upright d of dx */
	char *speech;		/* override, e.g. from `read as` */
	char *sem;		/* semantic name: abs, norm, floor, ceil, binom, cases, matrix, set */
};

M *mnew(int kind);
M *mrow(void);
void madd(M *row, M *kid);

/* parsing: returns NULL and reports errors at line:col+offset */
void eqn_reset_declarations(void);
void eqn_declare_function(const char *name);
void eqn_define(const char *def, int line, int col);
M *eqn_parse(const char *src, int line, int col, bool display, char **rows, int nrows);

/* layout */
enum { F_MATH, F_TEXT };

typedef struct {
	int kind;		/* 'g' glyph, 'i' invisible text, 'r' rule */
	int font;
	double scale, x, y, w, h;	/* em/1000; y grows downwards */
	int gid;
	double adv;
	int node;		/* MathML element (index into nodes), -1 none */
	char *text;
} Item;

typedef struct {
	double w, h, d, italic;
	double align;		/* x of the alignment point, or NAN */
	VEC(Item) items;
} Box;

typedef struct {
	int parent;
	char *tag;
	char *actual;		/* text a reader should use instead of the glyphs */
} MNode;

typedef VEC(MNode) MNodes;

Box *eqn_layout(M *m, bool display, MNodes *nodes);

/* semantics */
char *eqn_mathml(M *m, bool display, const char *source);
char *eqn_speech(M *m);
char *eqn_linear(M *m);

/* shared helpers */
bool is_atom(M *m);
const char *op_speech(const char *op);

#endif
