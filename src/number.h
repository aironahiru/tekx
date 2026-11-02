/*
 * number.h -- what every back end agrees on before it lays anything out:
 * the numbers of sections, theorems, equations, figures and references,
 * and how a reference to each reads.
 */
#ifndef NUMBER_H
#define NUMBER_H

#include "tree.h"

typedef struct {
	char *name;
	char *text;	/* how a reference reads: "Section 2", "(3)", "[1]" */
	char *spoken;	/* what a link to it says to a screen reader: "Equation 3" */
	char *number;	/* the number alone: "2.1", "3", "1" */
	int el;		/* destination element, set by the back end; -1 until then */
	int line;
} Label;

typedef struct {
	char *name, *title;
	int style;	/* 0 plain (italic body), 1 definition, 2 remark */
} Env;

void number_document(Node *doc);	/* the numbering pass, before layout */
Label *find_label(const char *name);
void add_label(const char *name, const char *text, const char *number, int line);
void label_el(const char *name, int el);
Env *find_env(const char *name);
const char *section_key(Node *section);	/* its label, or a generated one */
char *plain_text(Node *inline_content);

typedef VEC(Label) Labels;
extern Labels labels;

#endif
