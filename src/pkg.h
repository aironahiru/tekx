/*
 * pkg.h -- packages: notation, environments, macros and programs a
 * document can `use`, pinned by content hash in tekx.lock.
 */
#ifndef PKG_H
#define PKG_H

#include "tekx.h"

typedef VEC(char *) PkgStrs;

typedef struct {
	char *name, *version, *summary;
	char *requires;		/* the lowest tekx version it works with, or NULL */
	char *dir;		/* where it is installed */
	PkgStrs provides;	/* files spliced into the document, relative to dir */
	PkgStrs depends;	/* "name version" */
	PkgStrs filters;	/* programs run on the document tree, relative to dir */
	PkgStrs blocks;		/* block names its filters implement */
} Package;

/*
 * The package a document uses, from the project's tekx.lock and
 * .tekx/packages, its content verified against the lock.  Without a lock,
 * from the directories on TEKX_PATH.  NULL and *err set on failure.
 */
Package *pkg_use(const char *docfile, const char *name, const char *want, char **err);

/*
 * Run a package's filter: `in` (a document tree) on its standard input,
 * the new tree from its standard output.  NULL and *err set on failure.
 */
char *pkg_filter(const char *program, const char *pkgdir, const char *in, size_t inlen,
    size_t *outlen, char **err);

int pkg_main(int argc, char **argv);	/* tekx pkg ... */

#endif
