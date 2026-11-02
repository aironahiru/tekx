/*
 * number.c -- the numbering pass shared by the back ends.
 *
 * References may point forward, so every number is assigned before any
 * layout: sections 1, 1.1, 1.1.1; one counter for all theorem-like
 * blocks; labelled equations; figures; reference entries.
 */
#include "number.h"

Labels labels;
static VEC(Env) envs;
static bool envs_ready;

Label *
find_label(const char *name)
{
	for (size_t i = 0; i < labels.n; i++)
		if (streq(labels.v[i].name, name))
			return &labels.v[i];
	return NULL;
}

void
add_label(const char *name, const char *text, const char *number, int line)
{
	if (streq(name, "-"))
		return;
	Label *old = find_label(name);
	if (old) {
		error_at(line, 0, "label #%s is already used (line %d)", name, old->line);
		return;
	}
	Label l = {xstrdup(name), xstrdup(text), xstrdup(text), xstrdup(number), -1, line};
	if (text[0] == '(')
		l.spoken = fmt("Equation %s", number);
	else if (text[0] == '[')
		l.spoken = fmt("Reference %s", number);
	VPUSH(labels, l);
}

void
label_el(const char *name, int e)
{
	Label *l = name && !streq(name, "-") ? find_label(name) : NULL;

	if (l)
		l->el = e;
}

static void
add_env(const char *name, const char *title, int style)
{
	Env e = {xstrdup(name), xstrdup(title), style};

	VPUSH(envs, e);
}

Env *
find_env(const char *name)
{
	for (size_t i = envs.n; i-- > 0;)
		if (streq(envs.v[i].name, name))
			return &envs.v[i];
	return NULL;
}

static void
init_envs(void)
{
	static const struct { const char *n, *t; int s; } std[] = {
		{"theorem", "Theorem", 0}, {"lemma", "Lemma", 0}, {"proposition", "Proposition", 0},
		{"corollary", "Corollary", 0}, {"conjecture", "Conjecture", 0}, {"claim", "Claim", 0},
		{"axiom", "Axiom", 0}, {"hypothesis", "Hypothesis", 0}, {"fact", "Fact", 0},
		{"definition", "Definition", 1}, {"example", "Example", 1}, {"exercise", "Exercise", 1},
		{"problem", "Problem", 1}, {"solution", "Solution", 1}, {"notation", "Notation", 1},
		{"assumption", "Assumption", 1}, {"remark", "Remark", 2}, {"note", "Note", 2},
		{"observation", "Observation", 2},
	};
	if (envs_ready)
		return;
	envs_ready = true;
	for (size_t i = 0; i < NELEM(std); i++)
		add_env(std[i].n, std[i].t, std[i].s);
}

/* plain text of inline content, for bookmarks, link contents and alt text */
static void
plain(Buf *b, Node *n)
{
	if (streq(n->kind, "text") || streq(n->kind, "code") || streq(n->kind, "url"))
		bputs(b, n->rest);
	else if (streq(n->kind, "math")) {
		Node *l = child(n, "linear");
		bputs(b, l ? l->rest : n->rest);
	} else if (streq(n->kind, "ref")) {
		Label *l = find_label(n->rest);
		bputs(b, l ? l->text : n->rest);
	}
	for (size_t i = 0; i < n->kids.n; i++)
		if (!streq(n->kind, "math") && !streq(n->kind, "note"))
			plain(b, n->kids.v[i]);
}

char *
plain_text(Node *n)
{
	Buf b = {0};

	if (n)
		plain(&b, n);
	return bstr(&b);
}

const char *
section_key(Node *s)
{
	char *label = field(s, 2);

	return streq(label, "-") ? fmt("tekx-section-%zu", (size_t)s->line) : label;
}

static int sec[4], thm, eq, bib, fig;

static void
number(Node *n)
{
	for (size_t i = 0; i < n->kids.n; i++) {
		Node *k = n->kids.v[i];
		if (streq(k->kind, "env")) {
			char *name = field(k, 0), *kind = field(k, 1), *title = field_rest(k, 2);
			Env *base = find_env(kind);
			add_env(name, *title ? title : name, base ? base->style : 0);
		} else if (streq(k->kind, "section")) {
			int level = atoi(field(k, 0));
			bool numbered = atoi(field(k, 1)) != 0;
			if (numbered && level >= 1 && level <= 3) {
				sec[level]++;
				for (int j = level + 1; j < 4; j++)
					sec[j] = 0;
			}
			Buf b = {0};
			for (int j = 1; j <= level && j < 4; j++)
				bprintf(&b, "%s%d", j > 1 ? "." : "", sec[j]);
			add_label(section_key(k), numbered ? fmt("Section %s", bstr(&b)) : plain_text(child(k, "heading")),
			    numbered ? bstr(&b) : "", k->line);
			number(k);
		} else if (streq(k->kind, "theorem")) {
			thm++;
			Env *e = find_env(field(k, 0));
			add_label(field(k, 1), fmt("%s %d", e ? e->title : field(k, 0), thm), fmt("%d", thm), k->line);
			number(k);
		} else if (streq(k->kind, "line")) {
			char *label = field(k, 0);
			if (!streq(label, "-")) {
				eq++;
				add_label(label, fmt("(%d)", eq), fmt("%d", eq), k->line);
			}
		} else if (streq(k->kind, "figure")) {
			fig++;
			add_label(field(k, 2), fmt("Figure %d", fig), fmt("%d", fig), k->line);
		} else if (streq(k->kind, "entry")) {
			bib++;
			add_label(k->rest, fmt("[%d]", bib), fmt("%d", bib), k->line);
		} else {
			number(k);
		}
	}
}

void
number_document(Node *doc)
{
	init_envs();
	number(doc);
}
