/*
 * tree.c -- reading and writing the document tree; logical fonts.
 */
#include "tree.h"
#include "font.h"

Node *
node_new(const char *kind, int line, int col, const char *rest)
{
	Node *n = NEW(Node);

	n->kind = xstrdup(kind);
	n->line = line;
	n->col = col;
	n->rest = xstrdup(rest ? rest : "");
	return n;
}

void
node_append(Node *p, Node *c)
{
	c->parent = p;
	VPUSH(p->kids, c);
}

Node *
node_add(Node *p, const char *kind, int line, int col, const char *rest)
{
	Node *c = node_new(kind, line, col, rest);

	node_append(p, c);
	return c;
}

Node *
tree_read(FILE *in)
{
	Node *root = NULL, *stack[256];
	char *line;
	int depth;

	while ((line = read_line(in)) != NULL) {
		char *s = line, *kind, *at;
		int sp = 0, ln = 0, col = 0;
		while (*s == ' ') {
			s++;
			sp++;
		}
		if (*s == 0) {
			free(line);
			continue;
		}
		depth = sp / 2;
		kind = next_field(&s);
		if ((at = strchr(kind, '@')) != NULL) {
			*at = 0;
			ln = atoi(at + 1);
			char *c = strchr(at + 1, ':');
			col = c ? atoi(c + 1) : 0;
		}
		Node *n = node_new(kind, ln, col, s);
		if (root == NULL) {
			root = n;
			stack[0] = n;
		} else {
			if (depth < 1)
				depth = 1;
			if (depth > 255)
				depth = 255;
			while (depth > 1 && stack[depth - 1] == NULL)
				depth--;
			node_append(stack[depth - 1], n);
			stack[depth] = n;
			for (int k = depth + 1; k < 256 && stack[k]; k++)
				stack[k] = NULL;
		}
		free(line);
	}
	if (root == NULL)
		root = node_new("document", 0, 0, "");
	return root;
}

void
tree_write(FILE *out, Node *n, int depth)
{
	for (int i = 0; i < depth; i++)
		fputs("  ", out);
	fputs(n->kind, out);
	if (n->line > 0) {
		fprintf(out, "@%d", n->line);
		if (n->col > 0)
			fprintf(out, ":%d", n->col);
	}
	if (*n->rest) {
		fputc(' ', out);
		fputs(n->rest, out);
	}
	fputc('\n', out);
	for (size_t i = 0; i < n->kids.n; i++)
		tree_write(out, n->kids.v[i], depth + 1);
}

char *
field(Node *n, int i)
{
	char *s = n->rest, *start;

	for (;;) {
		while (*s == ' ')
			s++;
		start = s;
		while (*s && *s != ' ')
			s++;
		if (i-- == 0)
			return xstrndup(start, (size_t)(s - start));
		if (*s == 0)
			return xstrdup("");
	}
}

char *
field_rest(Node *n, int i)
{
	char *s = n->rest;

	while (i-- > 0) {
		while (*s == ' ')
			s++;
		while (*s && *s != ' ')
			s++;
	}
	if (*s == ' ')
		s++;
	return s;
}

Node *
child(Node *n, const char *kind)
{
	for (size_t i = 0; i < n->kids.n; i++)
		if (streq(n->kids.v[i]->kind, kind))
			return n->kids.v[i];
	return NULL;
}

/* ---- logical fonts ---- */

static struct {
	const char *name, *builtin;
	char *path;
	Font *font;
} lf[] = {
	{"text", "STIXTwoText-Regular.otf", NULL, NULL},
	{"italic", "STIXTwoText-Italic.otf", NULL, NULL},
	{"bold", "STIXTwoText-Bold.otf", NULL, NULL},
	{"bolditalic", "STIXTwoText-BoldItalic.otf", NULL, NULL},
	{"math", "STIXTwoMath-Regular.otf", NULL, NULL},
	{"mono", "LibertinusMono-Regular.otf", NULL, NULL},
};

static int
lf_index(const char *name)
{
	for (size_t i = 0; i < NELEM(lf); i++)
		if (streq(lf[i].name, name))
			return (int)i;
	return -1;
}

void
logical_font_override(const char *name, const char *path)
{
	int i = lf_index(name);

	if (i < 0)
		die("unknown font role '%s' (expected text, italic, bold, bolditalic, math or mono)", name);
	lf[i].path = xstrdup(path);
	lf[i].font = NULL;
}

const char *
logical_font_source(const char *name)
{
	int i = lf_index(name);

	if (i < 0)
		return NULL;
	return lf[i].path ? lf[i].path : fmt("builtin:%s", lf[i].builtin);
}

Font *
logical_font(const char *name)
{
	int i = lf_index(name);

	if (i < 0)
		die("unknown logical font %s", name);
	if (lf[i].font == NULL) {
		if (lf[i].path) {
			size_t n;
			char *err = NULL;
			u8 *d = (u8 *)read_file(lf[i].path, &n);
			if ((lf[i].font = font_load(d, n, &err)) == NULL)
				die("%s: %s", lf[i].path, err);
		} else {
			lf[i].font = font_builtin(lf[i].builtin);
		}
	}
	return lf[i].font;
}
