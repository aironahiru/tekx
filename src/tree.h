/*
 * tree.h -- the document tree exchanged by parse, eqn and set.
 *
 * Text format, one node per line, nested by two-space indentation:
 *
 *	kind[@line[:col]] [args...]
 *
 * Each kind takes a fixed number of space-separated arguments; anything
 * after them is free text running to the end of the line.
 */
#ifndef TREE_H
#define TREE_H

#include "tekx.h"

typedef struct Node Node;
struct Node {
	char *kind;
	int line, col;
	char *rest;		/* text after the kind token */
	Node *parent;
	VEC(Node *) kids;
};

Node *node_new(const char *kind, int line, int col, const char *rest);
Node *node_add(Node *parent, const char *kind, int line, int col, const char *rest);
void node_append(Node *parent, Node *child);
Node *tree_read(FILE *in);
void tree_write(FILE *out, Node *n, int depth);

/* split a node's free text into fields: returns field `i` (0-based) or "" */
char *field(Node *n, int i);
char *field_rest(Node *n, int i);	/* text after field i-1 (i fields skipped) */
Node *child(Node *n, const char *kind);

/* logical fonts shared by eqn and set */
struct Font;
struct Font *logical_font(const char *name);
const char *logical_font_source(const char *name);
void logical_font_override(const char *name, const char *path);

#endif
