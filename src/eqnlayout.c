/*
 * eqnlayout.c -- formula layout with the OpenType MATH table.
 *
 * The rules are TeX's (Appendix G of The TeXbook) as restated for
 * OpenType by the MATH table specification and MathML Core: atoms and
 * their classes decide spacing; fractions, scripts, radicals and limits
 * are placed with the font's own constants; delimiters and big
 * operators grow through size variants and glyph assemblies.
 *
 * Units are thousandths of an em at the formula's base size; y grows
 * downwards from the baseline, as on the page.
 */
#include "eqn.h"
#include "font.h"
#include "tree.h"

#include <math.h>

enum { D, T, S, SS };

typedef struct {
	int style;
	bool cramped;
} St;

static Font *mf, *tf;
static MNodes *nodes;

char *id_char(M *m);	/* eqnsem.c */

static double
scale(St s)
{
	if (s.style == S)
		return mf->mc[ScriptPercentScaleDown] / 100.0;
	if (s.style == SS)
		return mf->mc[ScriptScriptPercentScaleDown] / 100.0;
	return 1.0;
}

static double
K(St s)
{
	return 1000.0 / mf->upem * scale(s);
}

static double
C(St s, int which)
{
	return mf->mc[which] * K(s);
}

static St
sup_style(St s)
{
	St r = {s.style <= T ? S : SS, s.cramped};
	return r;
}

static St
sub_style(St s)
{
	St r = sup_style(s);
	r.cramped = true;
	return r;
}

static St
num_style(St s)
{
	St r = {s.style == D ? T : s.style == T ? S : SS, s.cramped};
	return r;
}

static St
den_style(St s)
{
	St r = num_style(s);
	r.cramped = true;
	return r;
}

static St
cramp(St s)
{
	s.cramped = true;
	return s;
}

static int
new_node(int parent, const char *tag, const char *actual)
{
	MNode n = {parent, xstrdup(tag), actual ? xstrdup(actual) : NULL};

	VPUSH(*nodes, n);
	return (int)nodes->n - 1;
}

/* ---- boxes ---- */

static Box *
box_new(void)
{
	Box *b = NEW(Box);

	b->align = NAN;
	return b;
}

static void
put(Box *dst, Box *src, double dx, double dy)
{
	for (size_t i = 0; i < src->items.n; i++) {
		Item it = src->items.v[i];
		it.x += dx;
		it.y += dy;
		VPUSH(dst->items, it);
	}
}

static void
helper(Box *b, const char *text, double x, double y, St s)
{
	Item it;

	memset(&it, 0, sizeof it);
	it.kind = 'i';
	it.font = F_TEXT;
	it.scale = scale(s);
	it.x = x;
	it.y = y;
	it.text = xstrdup(text);
	it.node = -1;
	VPUSH(b->items, it);
}

static void
rule(Box *b, double x, double y, double w, double h)
{
	Item it;

	memset(&it, 0, sizeof it);
	it.kind = 'r';
	it.x = x;
	it.y = y;
	it.w = w;
	it.h = h;
	it.node = -1;
	VPUSH(b->items, it);
}

static int
ssty(int gid, St s)
{
	if (s.style >= S)
		return gsub_single(mf, "ssty", gid, s.style == SS ? 1 : 0);
	return gid;
}

static Box *
glyph_box(Font *f, int font, int gid, double sc, int node, const char *text)
{
	Box *b = box_new();
	double k = 1000.0 / f->upem * sc;
	BBox bb = font_bbox(f, gid);
	Item it;

	b->w = font_adv(f, gid) * k;
	b->h = MAX(0, bb.y1 * k);
	b->d = MAX(0, -bb.y0 * k);
	b->italic = f == mf ? math_italic(f, gid) * k : 0;
	memset(&it, 0, sizeof it);
	it.kind = 'g';
	it.font = font;
	it.scale = sc;
	it.gid = gid;
	it.adv = b->w;
	it.node = node;
	it.text = xstrdup(text);
	VPUSH(b->items, it);
	return b;
}

/* a string of characters in the math font (falling back to the text font) */
static Box *
chars_box(const char *s, St st, int node, const char *copy, int *last_gid)
{
	Box *b = box_new();
	size_t n = strlen(s);
	double x = 0;
	bool first = true;

	for (size_t i = 0; i < n;) {
		u32 cp;
		int k = utf8_dec(s + i, n - i, &cp);
		int g = font_glyph(mf, cp);
		Font *f = mf;
		int font = F_MATH;
		if (g == 0 && (g = font_glyph(tf, cp)) != 0) {
			f = tf;
			font = F_TEXT;
		}
		if (g == 0) {
			warn_at(0, 0, "no glyph for U+%04X in the math or text font", (unsigned)cp);
			i += (size_t)k;
			continue;
		}
		if (f == mf)
			g = ssty(g, st);
		/* the copy text: the whole string on the first glyph if requested */
		char *t = copy ? (first ? (char *)copy : "") : xstrndup(s + i, (size_t)k);
		if (copy && !first)
			t = "";
		Box *gb = glyph_box(f, font, g, scale(st), node, t);
		if (*t == 0) {
			/* every glyph must carry text: repeat the character itself */
			gb->items.v[0].text = xstrndup(s + i, (size_t)k);
		}
		put(b, gb, x, 0);
		x += gb->w;
		b->h = MAX(b->h, gb->h);
		b->d = MAX(b->d, gb->d);
		b->italic = gb->italic;
		if (last_gid)
			*last_gid = f == mf ? g : -1;
		first = false;
		i += (size_t)k;
	}
	b->w = x;
	return b;
}

static Box *
text_box(const char *s, St st, int node)
{
	Box *b = box_new();
	size_t n = strlen(s);
	double x = 0, k = 1000.0 / tf->upem * scale(st);
	int prev = -1;

	for (size_t i = 0; i < n;) {
		u32 cp;
		int len = utf8_dec(s + i, n - i, &cp);
		int g = font_glyph(tf, cp);
		if (g == 0) {
			i += (size_t)len;
			continue;
		}
		if (prev >= 0)
			x += font_kern(tf, prev, g) * k;
		Box *gb = glyph_box(tf, F_TEXT, g, scale(st), node, xstrndup(s + i, (size_t)len));
		put(b, gb, x, 0);
		x += gb->w;
		b->h = MAX(b->h, gb->h);
		b->d = MAX(b->d, gb->d);
		prev = g;
		i += (size_t)len;
	}
	b->w = x;
	return b;
}

/* ---- stretchy glyphs ---- */

/* Grow glyph g vertically to at least `size`; returns a box with the
 * glyph (or assembly) positioned with its baseline at y = 0, unshifted. */
static Box *
vstretch(int g, double size, St s, int node, const char *text, bool *assembled)
{
	Construction c;
	double k = K(s);

	*assembled = false;
	if (!math_construction(mf, g, true, &c))
		return glyph_box(mf, F_MATH, g, scale(s), node, text);
	for (int i = 0; i < c.nvariants; i++)
		if (c.advance[i] * k >= size || (i == c.nvariants - 1 && c.nparts == 0))
			return glyph_box(mf, F_MATH, c.variant[i], scale(s), node, text);
	if (c.nparts == 0)
		return glyph_box(mf, F_MATH, g, scale(s), node, text);

	/* assembly: parts bottom to top */
	double minov = math_min_overlap(mf) * k;
	double sne = 0, se = 0;
	int nne = 0, ne = 0;
	for (int i = 0; i < c.nparts; i++) {
		if (c.part[i].extender) {
			se += c.part[i].full * k;
			ne++;
		} else {
			sne += c.part[i].full * k;
			nne++;
		}
	}
	int reps = 0;
	if (ne > 0)
		while (reps < 100 && sne + reps * se - (nne + reps * ne - 1) * minov < size)
			reps++;
	int count = nne + reps * ne;
	double total = sne + reps * se;
	double ov = count > 1 ? (total - size) / (count - 1) : 0;
	if (ov < minov)
		ov = minov;
	/* do not overlap more than the connectors allow */
	double maxov = 1e9;
	for (int i = 0; i < c.nparts; i++) {
		if (c.part[i].start)
			maxov = MIN(maxov, c.part[i].start * k);
		if (c.part[i].end)
			maxov = MIN(maxov, c.part[i].end * k);
	}
	if (ov > maxov && maxov >= minov)
		ov = maxov;
	Box *b = box_new();
	double pos = 0;	/* from the bottom, upwards */
	bool first = true;
	for (int i = 0; i < c.nparts; i++) {
		int r = c.part[i].extender ? reps : 1;
		for (int j = 0; j < r; j++) {
			if (!first)
				pos -= ov;
			BBox bb = font_bbox(mf, c.part[i].glyph);
			Box *pb = glyph_box(mf, F_MATH, c.part[i].glyph, scale(s), node, text);
			/* align the part's ink bottom with pos */
			put(b, pb, 0, -(pos - bb.y0 * k));
			b->w = MAX(b->w, pb->w);
			pos += c.part[i].full * k;
			first = false;
		}
	}
	b->h = pos;
	b->d = 0;
	*assembled = true;
	return b;
}

/* center a box on the math axis */
static void
center_on_axis(Box *b, St s)
{
	double axis = C(s, AxisHeight);
	double shift = (b->h - b->d) / 2 - axis;	/* move down by shift */
	for (size_t i = 0; i < b->items.n; i++)
		b->items.v[i].y += shift;
	b->h -= shift;
	b->d += shift;
}

static Box *
delimiter(const char *ch, double content_h, double content_d, St s, int parent)
{
	if (*ch == 0) {
		Box *b = box_new();
		b->w = 120 * scale(s);	/* \nulldelimiterspace */
		return b;
	}
	u32 cp = utf8_first(ch);
	int g = font_glyph(mf, cp);
	int node = new_node(parent, "mo", NULL);
	if (g == 0)
		return chars_box(ch, s, node, NULL, NULL);
	double axis = C(s, AxisHeight);
	double delta = MAX(content_h - axis, content_d + axis);
	double size = MAX(delta * 2 * 0.901, delta * 2 - 500 * scale(s));
	BBox bb = font_bbox(mf, g);
	double natural = (bb.y1 - bb.y0) * K(s);
	if (size <= natural)
		return glyph_box(mf, F_MATH, g, scale(s), node, ch);
	bool assembled;
	Box *b = vstretch(g, size, s, node, ch, &assembled);
	if (assembled)
		nodes->v[node].actual = xstrdup(ch);
	center_on_axis(b, s);
	return b;
}

/* ---- atoms and spacing ---- */

typedef struct {
	Box *b;
	int lcls, rcls;
	bool space;	/* explicit space: invisible to the spacing rules */
	bool align;
	bool text;	/* keyword text such as "if": surrounded by spaces */
	M *m;
} Atom;

typedef VEC(Atom) Atoms;

static Box *lay(M *m, St s, int parent);

/* 0 none, 1 thin, 2 medium, 3 thick; negative: not in script styles */
static const int spacing[8][8] = {
	/*          ORD OP  BIN REL OPEN CLOSE PUNCT INNER */
	/* ORD */  {0,  1,  -2, -3, 0,   0,    0,    -1},
	/* OP  */  {1,  1,  0,  -3, 0,   0,    0,    -1},
	/* BIN */  {-2, -2, 0,  0,  -2,  0,    0,    -2},
	/* REL */  {-3, -3, 0,  0,  -3,  0,    0,    -3},
	/* OPEN */ {0,  0,  0,  0,  0,   0,    0,    0},
	/* CLOSE */{0,  1,  -2, -3, 0,   0,    0,    -1},
	/* PUNCT */{-1, -1, 0,  -1, -1,  -1,   -1,   -1},
	/* INNER */{-1, 1,  -2, -3, -1,  0,    -1,   -1},
};

static double
space_between(int l, int r, St s)
{
	int k = spacing[l][r];
	if (k < 0) {
		if (s.style >= S)
			return 0;
		k = -k;
	}
	static const double mu[] = {0, 3, 4, 5};
	return mu[k] * 1000.0 / 18 * scale(s);
}

static int
class_of_m(M *m)
{
	switch (m->kind) {
	case M_OP:
		return m->unary ? ORD : m->cls;
	case M_ID:
		return m->func && utf8_len(m->text) > 1 ? OPC : ORD;
	case M_BIGOP:
		return OPC;
	case M_FRAC:
		return INNER;
	case M_SCRIPT:
		return m->a ? class_of_m(m->a) : ORD;
	case M_ROW:
		return m->kids.n == 1 ? class_of_m(m->kids.v[0]) : ORD;
	default:
		return ORD;
	}
}

static void flatten(M *m, St s, int parent, Atoms *out);

static void
add_atom(M *k, St s, int parent, Atoms *out)
{
	Atom a;

	memset(&a, 0, sizeof a);
	a.m = k;
	if (k->kind == M_ROW && !k->group && !k->apply) {
		flatten(k, s, parent, out);
		return;
	}
	if (k->kind == M_ALIGN) {
		a.align = true;
		a.space = true;
		a.b = box_new();
		VPUSH(*out, a);
		return;
	}
	if (k->kind == M_SPACE) {
		a.space = true;
		a.b = box_new();
		a.b->w = k->num * scale(s);
		VPUSH(*out, a);
		return;
	}
	a.b = lay(k, s, parent);
	a.lcls = a.rcls = class_of_m(k);
	if (k->kind == M_FENCE) {
		a.lcls = OPEN;
		a.rcls = CLOSE;
		if (k->a && k->a->kind == M_TABLE)
			a.lcls = a.rcls = INNER;
	}
	if (k->kind == M_BIGOP && k->a) {
		a.rcls = ORD;
	}
	if (k->kind == M_ROW && k->apply)
		a.lcls = class_of_m(k->kids.v[0]) == OPC ? OPC : ORD, a.rcls = CLOSE;
	if (k->kind == M_TEXT) {
		static const char *kw[] = {"if", "otherwise", "and", "or", "when", "where", "unless", "for all",
		    "for every", "for each", "for some", "for any", "for", "prime", "odd", "even"};
		for (size_t i = 0; i < NELEM(kw); i++)
			if (streq(kw[i], k->text))
				a.text = true;
	}
	VPUSH(*out, a);
}

static void
flatten(M *m, St s, int parent, Atoms *out)
{
	int node = parent;

	if (m->kids.n > 1 || m->times)
		node = new_node(parent, "mrow", NULL);
	for (size_t i = 0; i < m->kids.n; i++)
		add_atom(m->kids.v[i], s, node, out);
}

/* lay out a sequence of atoms on one baseline, with TeX's spacing */
static Box *
hlist(Atoms *at, St s)
{
	Box *b = box_new();
	double x = 0;
	int prev = -1;	/* index of previous real atom */

	/* binary operators in unary position become ordinary */
	for (size_t i = 0; i < at->n; i++) {
		Atom *a = &at->v[i];
		if (a->space || a->lcls != BIN)
			continue;
		int pc = -1;
		for (int j = (int)i - 1; j >= 0; j--)
			if (!at->v[j].space) {
				pc = at->v[j].rcls;
				break;
			}
		if (pc < 0 || pc == BIN || pc == OPC || pc == REL || pc == OPEN || pc == PUNCT)
			a->lcls = a->rcls = ORD;
	}
	for (size_t i = 0; i < at->n; i++) {
		Atom *a = &at->v[i];
		if (a->space || a->rcls != BIN)
			continue;
		int nc = -1;
		for (size_t j = i + 1; j < at->n; j++)
			if (!at->v[j].space) {
				nc = at->v[j].lcls;
				break;
			}
		if (nc < 0 || nc == REL || nc == CLOSE || nc == PUNCT)
			a->lcls = a->rcls = ORD;
	}
	double explicit_align = NAN, rel_align = NAN;
	for (size_t i = 0; i < at->n; i++) {
		Atom *a = &at->v[i];
		if (a->align) {
			if (isnan(explicit_align))
				explicit_align = x;
			continue;
		}
		if (a->space) {
			x += a->b->w;
			continue;
		}
		double before = x;
		if (prev >= 0) {
			Atom *p = &at->v[prev];
			if (a->text || p->text)
				x += (s.style >= S ? 250 : 500) * scale(s);
			else
				x += space_between(p->rcls, a->lcls, s);
		}
		/* the alignment point of a line is just before its first relation */
		if (a->lcls == REL && isnan(rel_align))
			rel_align = before;
		/* a line may break after a relation or binary operator, as in TeX */
		if (prev >= 0 && (at->v[prev].rcls == REL || at->v[prev].rcls == BIN) && !at->v[prev].text) {
			VPUSH(b->brk, before);
			VPUSH(b->brk, x);
			VPUSH(b->brk, at->v[prev].rcls == REL ? 500 : 700);
		}
		put(b, a->b, x, 0);
		x += a->b->w;
		b->h = MAX(b->h, a->b->h);
		b->d = MAX(b->d, a->b->d);
		b->italic = a->b->italic;
		prev = (int)i;
	}
	b->align = !isnan(explicit_align) ? explicit_align : rel_align;
	b->w = x;
	return b;
}

/* ---- constructions ---- */

static Box *
lay_frac(M *m, St s, int parent)
{
	int node = new_node(parent, "mfrac", NULL);
	Box *num = lay(m->a, num_style(s), node), *den = lay(m->b, den_style(s), node);
	Box *b = box_new();
	bool disp = s.style == D;
	double axis = C(s, AxisHeight);
	double pad = 120 * scale(s);
	double w = MAX(num->w, den->w);
	double u, v;

	if (m->thin) {
		u = disp ? C(s, StackTopDisplayStyleShiftUp) : C(s, StackTopShiftUp);
		v = disp ? C(s, StackBottomDisplayStyleShiftDown) : C(s, StackBottomShiftDown);
		double gap = disp ? C(s, StackDisplayStyleGapMin) : C(s, StackGapMin);
		double have = (u - num->d) - (den->h - v);
		if (have < gap) {
			u += (gap - have) / 2;
			v += (gap - have) / 2;
		}
	} else {
		double t = C(s, FractionRuleThickness);
		u = disp ? C(s, FractionNumeratorDisplayStyleShiftUp) : C(s, FractionNumeratorShiftUp);
		v = disp ? C(s, FractionDenominatorDisplayStyleShiftDown) : C(s, FractionDenominatorShiftDown);
		double gn = disp ? C(s, FractionNumDisplayStyleGapMin) : C(s, FractionNumeratorGapMin);
		double gd = disp ? C(s, FractionDenomDisplayStyleGapMin) : C(s, FractionDenominatorGapMin);
		u = MAX(u, axis + t / 2 + gn + num->d);
		v = MAX(v, den->h + gd + t / 2 - axis);
		/* reading order for plain-text extraction: (num)/(den) */
		rule(b, pad, -(axis + t / 2), w, t);
	}
	double nx = pad + (w - num->w) / 2, dx = pad + (w - den->w) / 2;
	bool pn = !is_atom(m->a), pd = !is_atom(m->b);
	if (pn)
		helper(b, "(", nx, -u, num_style(s));
	put(b, num, nx, -u);
	if (pn)
		helper(b, ")", nx + num->w, -u, num_style(s));
	if (!m->thin)
		helper(b, "/", pad + w / 2, -axis, s);
	if (pd)
		helper(b, "(", dx, v, den_style(s));
	put(b, den, dx, v);
	if (pd)
		helper(b, ")", dx + den->w, v, den_style(s));
	if (m->thin) {
		/* rule-less: the order in the item list already reads num, den */
	}
	b->w = w + 2 * pad;
	b->h = u + num->h;
	b->d = v + den->d;
	return b;
}

static bool
single_glyph(M *m)
{
	while (m && m->kind == M_ROW && m->kids.n == 1)
		m = m->kids.v[0];
	return m && (m->kind == M_ID || m->kind == M_NUM || (m->kind == M_OP && !m->unary)) && utf8_len(id_char(m)) == 1;
}

static Box *
lay_scripts(Box *base, M *bm, M *sub, M *sup, St s, int node, bool limits, bool bigop, double italic)
{
	Box *b = box_new();
	Box *bs = sub ? lay(sub, sub_style(s), node) : NULL;
	Box *bp = sup ? lay(sup, sup_style(s), node) : NULL;

	if (limits) {
		double w = base->w;
		if (bs) w = MAX(w, bs->w);
		if (bp) w = MAX(w, bp->w);
		double bx = (w - base->w) / 2;
		put(b, base, bx, 0);
		b->h = base->h;
		b->d = base->d;
		if (bs) {
			double drop = MAX(C(s, LowerLimitBaselineDropMin), C(s, LowerLimitGapMin) + bs->h);
			double y = base->d + drop;
			helper(b, "_(", (w - bs->w) / 2, y, sub_style(s));
			put(b, bs, (w - bs->w) / 2 - italic / 2, y);
			helper(b, ")", (w + bs->w) / 2, y, sub_style(s));
			b->d = y + bs->d;
		}
		if (bp) {
			double rise = MAX(C(s, UpperLimitBaselineRiseMin), C(s, UpperLimitGapMin) + bp->d);
			double y = -(base->h + rise);
			helper(b, "^(", (w - bp->w) / 2, y, sup_style(s));
			put(b, bp, (w - bp->w) / 2 + italic / 2, y);
			helper(b, ")", (w + bp->w) / 2, y, sup_style(s));
			b->h = -y + bp->h;
		}
		b->w = w;
		return b;
	}
	bool glyph = single_glyph(bm) && !bigop;	/* big operators are boxes */
	double u = 0, v = 0;
	if (bp) {
		u = s.cramped ? C(s, SuperscriptShiftUpCramped) : C(s, SuperscriptShiftUp);
		if (!glyph)
			u = MAX(u, base->h - C(s, SuperscriptBaselineDropMax));
		u = MAX(u, bp->d + C(s, SuperscriptBottomMin));
	}
	if (bs) {
		v = C(s, SubscriptShiftDown);
		if (!glyph)
			v = MAX(v, base->d + C(s, SubscriptBaselineDropMin));
		v = MAX(v, bs->h - C(s, SubscriptTopMax));
	}
	if (bp && bs) {
		double gap = (u - bp->d) - (bs->h - v);
		double need = C(s, SubSuperscriptGapMin);
		if (gap < need) {
			v += need - gap;
			double lift = C(s, SuperscriptBottomMaxWithSubscript) - (u - bp->d);
			if (lift > 0) {
				u += lift;
				v -= lift;
			}
		}
	}
	put(b, base, 0, 0);
	double after = C(s, SpaceAfterScript);
	double w = base->w;
	b->h = base->h;
	b->d = base->d;
	double subx = base->w - (bigop ? italic : 0), supx = base->w + (bigop ? 0 : italic);
	if (bs) {
		char *lt = eqn_linear(sub);
		bool uni = true;
		for (char *c = lt; *c; c++)
			if (*c < '0' || *c > '9')
				uni = false;
		if (!uni)
			helper(b, is_atom(sub) ? "_" : "_(", subx, v, sub_style(s));
		put(b, bs, subx, v);
		if (!uni && !is_atom(sub))
			helper(b, ")", subx + bs->w, v, sub_style(s));
		w = MAX(w, subx + bs->w);
		b->d = MAX(b->d, v + bs->d);
		b->h = MAX(b->h, bs->h - v);
	}
	if (bp) {
		bool prime = sup->kind == M_OP && (streq(sup->text, "′") || streq(sup->text, "″"));
		if (!prime)
			helper(b, is_atom(sup) ? "^" : "^(", supx, -u, sup_style(s));
		put(b, bp, supx, -u);
		if (!prime && !is_atom(sup))
			helper(b, ")", supx + bp->w, -u, sup_style(s));
		w = MAX(w, supx + bp->w);
		b->h = MAX(b->h, u + bp->h);
		b->d = MAX(b->d, bp->d - u);
	}
	b->w = w + after;
	return b;
}

static const char *
script_tag(M *m)
{
	if (m->limits)
		return m->b && m->c ? "munderover" : m->b ? "munder" : "mover";
	return m->b && m->c ? "msubsup" : m->b ? "msub" : "msup";
}

static Box *
lay_radical(M *m, St s, int parent)
{
	int node = new_node(parent, m->kind == M_ROOT ? "mroot" : "msqrt", NULL);
	Box *body = lay(m->a, cramp(s), node);
	bool disp = s.style == D;
	double gap = disp ? C(s, RadicalDisplayStyleVerticalGap) : C(s, RadicalVerticalGap);
	double t = C(s, RadicalRuleThickness);
	double target = body->h + body->d + gap + t;
	int g = font_glyph(mf, 0x221a);
	/* the radical sign belongs to msqrt itself: MathML has no operator for it */
	bool assembled;
	Box *rad = vstretch(g, target, s, node, "√", &assembled);
	/* the radical's own extent */
	double rh = rad->h, rd = rad->d;
	double total = rh + rd;
	if (total > target)
		gap += (total - target) / 2;
	/* place the radical so that its top is at body->h + gap + t */
	double top = body->h + gap + t;
	double shift = rh - top;	/* move down by shift */
	Box *b = box_new();
	double x = 0;
	Box *idx = NULL;
	if (m->kind == M_ROOT) {
		St is = {SS, true};
		idx = lay(m->b, is, node);
		double before = C(s, RadicalKernBeforeDegree), after = C(s, RadicalKernAfterDegree);
		double raise = mf->mc[RadicalDegreeBottomRaisePercent] / 100.0 * total;
		double iy = -(raise - rd - shift) - idx->d;
		/* the degree is read first: root(n, x) */
		helper(b, "root(", before, iy, is);
		put(b, idx, before, iy);
		helper(b, ",", before + idx->w, iy, is);
		x = MAX(0, before + idx->w + after);
		b->h = MAX(b->h, -iy + idx->h);
	}
	put(b, rad, x, shift);
	double bx = x + rad->w;
	rule(b, bx, -top, body->w, t);
	bool par = !is_atom(m->a);
	if (par)
		helper(b, "(", bx, 0, s);
	put(b, body, bx, 0);
	if (par || m->kind == M_ROOT)
		helper(b, ")", bx + body->w, 0, s);
	b->w = bx + body->w;
	b->h = MAX(b->h, top + C(s, RadicalExtraAscender));
	b->d = MAX(body->d, rd + shift);
	return b;
}

static Box *
lay_fence(M *m, St s, int parent)
{
	int node = new_node(parent, "mrow", NULL);
	/* the opening delimiter's element must precede the body's, but its
	 * size depends on the body: reserve the element now, fill it later */
	int open_node = *m->open ? new_node(node, "mo", NULL) : -1;
	Box *body = lay(m->a, s, node);
	Box *b = box_new();
	double x = 0;
	Box *o = NULL, *c = NULL;

	if (*m->open) {
		/* reuse the placeholder node for the delimiter glyphs */
		size_t before = nodes->n;
		o = delimiter(m->open, body->h, body->d, s, node);
		/* delimiter() created a new node; fold it into the placeholder */
		if (nodes->n > before) {
			int made = (int)nodes->n - 1;
			for (size_t i = 0; i < o->items.n; i++)
				if (o->items.v[i].node == made)
					o->items.v[i].node = open_node;
			nodes->v[open_node].actual = nodes->v[made].actual;
			nodes->n--;
		}
		put(b, o, 0, 0);
		x += o->w;
	}
	put(b, body, x, 0);
	x += body->w;
	if (*m->close) {
		c = delimiter(m->close, body->h, body->d, s, node);
		put(b, c, x, 0);
		x += c->w;
	}
	b->w = x;
	b->h = body->h;
	b->d = body->d;
	if (o) {
		b->h = MAX(b->h, o->h);
		b->d = MAX(b->d, o->d);
	}
	if (c) {
		b->h = MAX(b->h, c->h);
		b->d = MAX(b->d, c->d);
	}
	return b;
}

static Box *
lay_bigop(M *m, St s, int parent)
{
	int node = new_node(parent, "mrow", NULL);
	int snode = node;
	if (m->b || m->c)
		snode = new_node(node, m->limits && s.style == D ? (m->b && m->c ? "munderover" : m->b ? "munder" : "mover") :
		    (m->b && m->c ? "msubsup" : m->b ? "msub" : "msup"), NULL);
	int onode = new_node(snode, "mo", NULL);
	Box *op;
	double italic = 0;
	if (m->largeop) {
		u32 cp = utf8_first(m->text);
		int g = font_glyph(mf, cp);
		if (s.style == D) {
			Construction c;
			if (math_construction(mf, g, true, &c) && c.nvariants > 0) {
				double minh = C(s, DisplayOperatorMinHeight);
				int pick = c.variant[c.nvariants - 1];
				for (int i = 0; i < c.nvariants; i++) {
					BBox bb = font_bbox(mf, c.variant[i]);
					if ((bb.y1 - bb.y0) * K(s) >= minh) {
						pick = c.variant[i];
						break;
					}
				}
				g = pick;
			}
		}
		op = glyph_box(mf, F_MATH, g, scale(s), onode, m->text);
		italic = op->italic;
		center_on_axis(op, s);
	} else {
		op = chars_box(m->text, s, onode, NULL, NULL);
	}
	Box *b = box_new();
	Box *withlim = op;
	if (m->b || m->c)
		withlim = lay_scripts(op, NULL, m->b, m->c, s, snode, m->limits && s.style == D, true, italic);
	put(b, withlim, 0, 0);
	b->w = withlim->w;
	b->h = withlim->h;
	b->d = withlim->d;
	if (m->a) {
		Atoms at = {0};
		add_atom(m->a, s, node, &at);
		Box *body = hlist(&at, s);
		double x = b->w + space_between(OPC, at.n ? at.v[0].lcls : ORD, s);
		helper(b, " ", b->w, 0, s);
		put(b, body, x, 0);
		b->w = x + body->w;
		b->h = MAX(b->h, body->h);
		b->d = MAX(b->d, body->d);
	}
	return b;
}

static Box *
lay_accent(M *m, St s, int parent)
{
	int node = new_node(parent, "mover", NULL);
	Box *base = lay(m->a, cramp(s), node);
	int anode = new_node(node, "mo", NULL);
	u32 cp = utf8_first(m->text);
	int g = font_glyph(mf, cp);
	Box *b = box_new();
	double k = K(s);

	put(b, base, 0, 0);
	b->w = base->w;
	b->h = base->h;
	b->d = base->d;
	if (g == 0)
		return b;
	/* wide accents over wide bases */
	if (!single_glyph(m->a)) {
		Construction c;
		if (math_construction(mf, g, false, &c))
			for (int i = 0; i < c.nvariants; i++) {
				g = c.variant[i];
				if (c.advance[i] * k >= base->w * 0.9)
					break;
			}
	}
	double battach = base->w / 2;
	if (single_glyph(m->a) && base->items.n == 1) {
		bool found;
		int a = math_top_accent(mf, base->items.v[0].gid, &found);
		if (found)
			battach = a * k * (base->items.v[0].scale / scale(s));
	}
	bool found;
	int aa = math_top_accent(mf, g, &found);
	BBox bb = font_bbox(mf, g);
	double aattach = found ? aa * k : (bb.x0 + bb.x1) / 2 * k;
	double shift = MAX(0, base->h - C(s, AccentBaseHeight));
	Box *ab = glyph_box(mf, F_MATH, g, scale(s), anode, m->text);
	put(b, ab, battach - aattach, -shift);
	b->h = MAX(b->h, bb.y1 * k + shift);
	return b;
}

static Box *
lay_overline(M *m, St s, int parent)
{
	int node = new_node(parent, "mover", NULL);
	Box *base = lay(m->a, cramp(s), node);
	Box *b = box_new();
	double gap = C(s, OverbarVerticalGap), t = C(s, OverbarRuleThickness);

	put(b, base, 0, 0);
	rule(b, 0, -(base->h + gap + t), base->w, t);
	b->w = base->w;
	b->h = base->h + gap + t + C(s, OverbarExtraAscender);
	b->d = base->d;
	return b;
}

static Box *
lay_table(M *m, St s, int parent)
{
	int node = new_node(parent, "mtable", NULL);
	St cs = s;
	if (cs.style == D)
		cs.style = T;
	int R = m->rows, Cn = m->cols;
	Box **cell = xcalloc((size_t)(R * Cn) + 1, sizeof *cell);
	double *cw = xcalloc((size_t)Cn + 1, sizeof *cw), *rh = xcalloc((size_t)R + 1, sizeof *rh),
	    *rd = xcalloc((size_t)R + 1, sizeof *rd);
	bool cases = m->sem && streq(m->sem, "cases");
	double sc = scale(cs);
	double strut_h = 840 * sc, strut_d = 360 * sc;
	double rowgap = cases ? 150 * sc : 0;
	double colgap = 1000 * sc;

	for (int r = 0; r < R; r++) {
		int tr = new_node(node, "mtr", NULL);
		rh[r] = strut_h;
		rd[r] = strut_d;
		for (int c = 0; c < Cn; c++) {
			int td = new_node(tr, "mtd", NULL);
			Box *x = lay(m->kids.v[(size_t)(r * Cn + c)], cs, td);
			cell[r * Cn + c] = x;
			cw[c] = MAX(cw[c], x->w);
			rh[r] = MAX(rh[r], x->h);
			rd[r] = MAX(rd[r], x->d);
		}
	}
	double total = 0;
	for (int r = 0; r < R; r++)
		total += rh[r] + rd[r] + (r ? rowgap : 0);
	double axis = C(s, AxisHeight);
	double top = -(axis + total / 2);
	Box *b = box_new();
	double y = top;
	for (int r = 0; r < R; r++) {
		y += rh[r] + (r ? rowgap : 0);
		double x = 0;
		if (r)
			helper(b, ";", 0, y, cs);
		for (int c = 0; c < Cn; c++) {
			Box *x0 = cell[r * Cn + c];
			char al = m->colalign && m->colalign[c] ? m->colalign[c] : 'c';
			double off = al == 'l' ? 0 : al == 'r' ? cw[c] - x0->w : (cw[c] - x0->w) / 2;
			if (c)
				helper(b, ",", x - colgap / 2, y, cs);
			put(b, x0, x + off, y);
			x += cw[c] + colgap;
		}
		y += rd[r];
	}
	double w = 0;
	for (int c = 0; c < Cn; c++)
		w += cw[c] + (c ? colgap : 0);
	b->w = w;
	b->h = axis + total / 2;
	b->d = total / 2 - axis;
	return b;
}

static Box *
lay(M *m, St s, int parent)
{
	if (m == NULL)
		return box_new();
	switch (m->kind) {
	case M_ROW: {
		Atoms at = {0};
		if (m->apply && m->kids.n == 2) {
			int node = new_node(parent, "mrow", NULL);
			add_atom(m->kids.v[0], s, node, &at);
			add_atom(m->kids.v[1], s, node, &at);
			return hlist(&at, s);
		}
		flatten(m, s, parent, &at);
		return hlist(&at, s);
	}
	case M_ID: {
		int node = new_node(parent, "mi", NULL);
		const char *c = id_char(m);
		/* copy text: plain letters for default italics, the symbol otherwise */
		const char *copy = (m->variant == V_ITALIC && utf8_len(m->text) == 1) ? m->text : NULL;
		return chars_box(c, s, node, copy, NULL);
	}
	case M_NUM:
		return chars_box(m->text, s, new_node(parent, "mn", NULL), NULL, NULL);
	case M_OP: {
		int node = new_node(parent, "mo", NULL);
		return chars_box(m->text, s, node, NULL, NULL);
	}
	case M_TEXT:
		return text_box(m->text, s, new_node(parent, "mtext", NULL));
	case M_FRAC:
		return lay_frac(m, s, parent);
	case M_SCRIPT: {
		int node = new_node(parent, script_tag(m), NULL);
		Box *base = lay(m->a, s, node);
		if (m->limits && s.style == D)
			return lay_scripts(base, m->a, m->b, m->c, s, node, true, false, 0);
		return lay_scripts(base, m->a, m->b, m->c, s, node, false, false,
		    single_glyph(m->a) ? base->italic : 0);
	}
	case M_SQRT: case M_ROOT:
		return lay_radical(m, s, parent);
	case M_FENCE:
		return lay_fence(m, s, parent);
	case M_BIGOP:
		return lay_bigop(m, s, parent);
	case M_ACCENT:
		return lay_accent(m, s, parent);
	case M_OVERLINE:
		return lay_overline(m, s, parent);
	case M_TABLE:
		return lay_table(m, s, parent);
	case M_SPACE: {
		Box *b = box_new();
		b->w = m->num * scale(s);
		return b;
	}
	case M_ALIGN:
		return box_new();
	}
	return box_new();
}

Box *
eqn_layout(M *m, bool display, MNodes *ns)
{
	St s = {display ? D : T, false};

	if (mf == NULL) {
		mf = logical_font("math");
		tf = logical_font("text");
		if (!mf->hasmath)
			die("the math font has no OpenType MATH table");
	}
	nodes = ns;
	int root = new_node(-1, "math", NULL);
	Atoms at = {0};
	add_atom(m, s, root, &at);
	Box *b = hlist(&at, s);
	/* a line that starts with a binary operator continues the line above */
	if (isnan(b->align)) {
		for (size_t i = 0; i < at.n; i++) {
			if (at.v[i].space)
				continue;
			if (at.v[i].m && at.v[i].m->kind == M_ROW && at.v[i].m->kids.n == 0 && i + 1 < at.n &&
			    at.v[i + 1].m && at.v[i + 1].m->kind == M_OP && at.v[i + 1].m->cls == BIN)
				b->align = -1000;
			break;
		}
	}
	return b;
}
