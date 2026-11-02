/*
 * number.c -- the numbering pass shared by the back ends.
 *
 * References may point forward, so every number is assigned before any
 * layout: sections 1, 1.1, 1.1.1; one counter for all theorem-like
 * blocks; labelled equations; figures; reference entries.
 */
#include "number.h"

#include <strings.h>

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
	} else if (streq(n->kind, "ref") || streq(n->kind, "refnum")) {
		Label *l = find_label(n->rest);
		bputs(b, l ? (streq(n->kind, "refnum") ? l->number : l->text) : n->rest);
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

/*
 * The counters.  With `numbering by section`, theorems and equations
 * restart in each top-level section and carry its number: Theorem 2.3,
 * (2.3), and in an appendix Lemma A.1, (A.1).  Figures and references
 * are numbered through the whole document.
 */
static int sec[4], thm, eq, bib, fig;
static bool by_section, appendix;
static char *top = "";	/* the current top-level section number, "2" or "A" */

/* reference entries live in their own namespace, so that a citation key
 * and a label (an equation #landau and the paper landau) never collide */
char *
cite_label(const char *key)
{
	return fmt("cite:%s", key);
}

/* a theorem or equation number: "3", or "2.3" by section */
static char *
counted(int n)
{
	return by_section && *top ? fmt("%s.%d", top, n) : fmt("%d", n);
}

static char *
section_number(int level)
{
	Buf b = {0};

	for (int j = 1; j <= level && j < 4; j++) {
		if (j == 1 && appendix)
			bprintf(&b, "%c", 'A' + (sec[1] - 1) % 26);
		else
			bprintf(&b, "%s%d", j > 1 ? "." : "", sec[j]);
	}
	return bstr(&b);
}

const char *
heading_number(Node *s)
{
	if (s->num == NULL)
		return NULL;
	if (atoi(field(s, 0)) == 1 && s->num[0] >= 'A' && s->num[0] <= 'Z')
		return fmt("Appendix %s", s->num);
	return s->num;
}

static void
number(Node *n)
{
	for (size_t i = 0; i < n->kids.n; i++) {
		Node *k = n->kids.v[i];
		if (streq(k->kind, "env")) {
			char *name = field(k, 0), *kind = field(k, 1), *title = field_rest(k, 2);
			Env *base = find_env(kind);
			add_env(name, *title ? title : name, base ? base->style : 0);
		} else if (streq(k->kind, "appendix")) {
			appendix = true;
			sec[1] = 0;
		} else if (streq(k->kind, "section")) {
			int level = atoi(field(k, 0));
			bool numbered = atoi(field(k, 1)) != 0;
			if (numbered && level >= 1 && level <= 3) {
				sec[level]++;
				for (int j = level + 1; j < 4; j++)
					sec[j] = 0;
				k->num = section_number(level);
				if (level == 1) {
					top = k->num;
					thm = eq = 0;
				}
			}
			const char *text = !numbered ? plain_text(child(k, "heading")) :
			    level == 1 && appendix ? fmt("Appendix %s", k->num) : fmt("Section %s", k->num);
			add_label(section_key(k), text, numbered ? k->num : "", k->line);
			number(k);
		} else if (streq(k->kind, "theorem")) {
			char *kind = field(k, 0);
			size_t kl = strlen(kind);
			bool starred = kl > 1 && kind[kl - 1] == '*';
			Env *e = find_env(starred ? xstrndup(kind, kl - 1) : kind);
			const char *title = e ? e->title : kind;
			if (starred) {
				/* unnumbered: a reference reads its title, "Main Theorem", or "Theorem" */
				Node *h = child(k, "heading");
				add_label(field(k, 1), h ? plain_text(h) : title, "", k->line);
			} else {
				thm++;
				k->num = counted(thm);
				add_label(field(k, 1), fmt("%s %s", title, k->num), k->num, k->line);
			}
			number(k);
		} else if (streq(k->kind, "line")) {
			char *label = field(k, 0);
			if (!streq(label, "-")) {
				eq++;
				k->num = counted(eq);
				add_label(label, fmt("(%s)", k->num), k->num, k->line);
			}
		} else if (streq(k->kind, "figure")) {
			fig++;
			k->num = fmt("%d", fig);
			add_label(field(k, 2), fmt("Figure %d", fig), k->num, k->line);
			number(k);
		} else if (streq(k->kind, "entry")) {
			Node *lb = child(k, "label");
			bib++;
			k->num = lb ? lb->rest : fmt("%d", bib);
			add_label(cite_label(k->rest), fmt("[%s]", k->num), k->num, k->line);
		} else {
			number(k);
		}
	}
}

/*
 * Writers coming from LaTeX type "Theorem @main" (Theorem~\\ref{main}),
 * "(@gauss)" and "Lemmas @a and @b".  A reference already says
 * "Theorem 1.1" and "(2.1)", so the duplicated word or parentheses are
 * dropped, and after a plural word the references give numbers alone:
 * "Lemmas 2.2 and 2.3".  Links keep their full spoken names.
 */
static bool
word_before(const char *t, const char *word, size_t *at)
{
	size_t n = strlen(t), w = strlen(word);

	if (n < w + 1 || t[n - 1] != ' ')
		return false;
	size_t a = n - 1 - w;
	if (strncasecmp(t + a, word, w) != 0 || (a > 0 && t[a - 1] != ' ' && t[a - 1] != '('))
		return false;
	*at = a;
	return true;
}

static bool
separator(const char *t)
{
	return streq(t, ", ") || streq(t, " and ") || streq(t, ", and ") || streq(t, " or ") ||
	    streq(t, ", or ") || streq(t, "–") || streq(t, " to ");
}

static void
tidy_refs(Node *n)
{
	for (size_t i = 0; i < n->kids.n; i++) {
		Node *k = n->kids.v[i];
		Node *prev = i > 0 && streq(n->kids.v[i - 1]->kind, "text") ? n->kids.v[i - 1] : NULL;
		Node *next = i + 1 < n->kids.n && streq(n->kids.v[i + 1]->kind, "text") ? n->kids.v[i + 1] : NULL;
		Label *l = streq(k->kind, "ref") ? find_label(k->rest) : NULL;
		if (l == NULL || prev == NULL) {
			tidy_refs(k);
			continue;
		}
		const char *sp = strchr(l->text, ' ');
		size_t at;
		if (l->text[0] == '(' && prev->rest[0] && prev->rest[strlen(prev->rest) - 1] == '(' &&
		    next && next->rest[0] == ')') {
			prev->rest[strlen(prev->rest) - 1] = 0;
			next->rest++;
		} else if (sp && word_before(prev->rest, strrchr(l->text, ' ') + 1, &at) &&
		    !word_before(prev->rest, xstrndup(l->text, (size_t)(sp - l->text)), &at)) {
			/* "Theorem @main" for an unnumbered "Main Theorem" */
			word_before(prev->rest, strrchr(l->text, ' ') + 1, &at);
			prev->rest[at] = 0;
		} else if (sp) {
			char *word = xstrndup(l->text, (size_t)(sp - l->text));
			if (word_before(prev->rest, word, &at)) {
				prev->rest[at] = 0;
			} else if (word_before(prev->rest, fmt("%ss", word), &at)) {
				/* a plural: this reference and the ones joined to it give numbers */
				for (size_t j = i; j < n->kids.n; j += 2) {
					Node *r = n->kids.v[j];
					Label *rl = streq(r->kind, "ref") ? find_label(r->rest) : NULL;
					if (rl == NULL)
						break;
					/* only a target of that kind loses its word: "Lemmas 1.2
					 * and Theorem 2.1", never "Lemmas 1.2 and 2.1" */
					if (strncmp(rl->text, word, strlen(word)) == 0 && rl->text[strlen(word)] == ' ')
						r->kind = xstrdup("refnum");
					if (j + 1 >= n->kids.n || !streq(n->kids.v[j + 1]->kind, "text") ||
					    !separator(n->kids.v[j + 1]->rest))
						break;
				}
			}
		}
	}
}

void
number_document(Node *doc)
{
	init_envs();
	/* the setting may come after the sections it governs: read it first */
	for (size_t i = 0; i < doc->kids.n; i++) {
		Node *k = doc->kids.v[i];
		if (streq(k->kind, "meta") && streq(field(k, 0), "numbering"))
			by_section = streq(field_rest(k, 1), "by section");
	}
	number(doc);
	tidy_refs(doc);
}
