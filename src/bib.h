/*
 * bib.h -- BibTeX databases.
 */
#ifndef BIB_H
#define BIB_H

#include "tekx.h"

typedef struct {
	char *name, *value;	/* name lowercased; value as written, braces kept */
} BibField;

typedef struct {
	char *type, *key;	/* type lowercased: "article" */
	VEC(BibField) f;
	int line;
} BibEntry;

typedef VEC(BibEntry) BibEntries;

bool bib_read(const char *path, BibEntries *out);	/* false if unreadable; errors reported */
const char *bib_get(const BibEntry *e, const char *field);
void bib_resolve(BibEntries *db);	/* crossref inheritance, biblatex field names */
char *bib_format(const BibEntry *e);	/* the entry as tekx inline text */
char *bib_sort_key(const BibEntry *e);
char *bib_alpha_label(const BibEntry *e);	/* amsalpha: Hoo71, BC47, ABC+99 */

#endif
