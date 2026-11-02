/*
 * pkg.c -- the package manager.
 *
 * A package is a directory holding a manifest, package.tekx, and the
 * files it provides: definitions (notation, environments, macros) that a
 * document takes in with `use NAME`, and optionally programs, filters,
 * that rewrite the document tree, and the custom blocks those filters
 * implement.  Packages need not be public: a registry is any directory or
 * git repository whose subdirectories are packages, such as a share on
 * the office network or a private repository.
 *
 * A project pins its packages in tekx.lock, next to the document:
 *
 *	registry house git https://git.example.com/house/tekx-packages.git
 *	package notation 1.2.0 sha256:3b5d... git https://... 4f2a9c... notation
 *	package style 0.3.1 sha256:9d1c... dir /srv/tekx/style
 *
 * and installs them in .tekx/packages/.  Every build checks each package
 * it uses against the hash in the lock, so a document cannot change
 * because a package changed underneath it, and `tekx pkg install`
 * rebuilds .tekx/packages from the lock on another machine, refusing any
 * source whose content no longer matches.
 *
 * The hash covers every file's path, executable bit and content, in path
 * order; .git directories are left out and symbolic links refused.
 */
#define _XOPEN_SOURCE 700	/* realpath */

#include "pkg.h"
#include "sha256.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define LOCK "tekx.lock"
#define STORE ".tekx/packages"

/* ---- small helpers ---- */

static char *
path_join(const char *a, const char *b)
{
	if (!*a)
		return xstrdup(b);
	return fmt("%s%s%s", a, a[strlen(a) - 1] == '/' ? "" : "/", b);
}

static bool
is_dir(const char *p)
{
	struct stat st;

	return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool
is_file(const char *p)
{
	struct stat st;

	return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static char *
absolute(const char *p)
{
	char buf[4096];

	if (realpath(p, buf))
		return xstrdup(buf);
	if (p[0] == '/')
		return xstrdup(p);
	if (getcwd(buf, sizeof buf))
		return path_join(buf, p);
	return xstrdup(p);
}

static char *
dirname_of(const char *p)
{
	const char *s = strrchr(p, '/');

	return s ? (s == p ? xstrdup("/") : xstrndup(p, (size_t)(s - p))) : xstrdup(".");
}

static void
mkdirs(const char *p)
{
	char *t = xstrdup(p);

	for (char *s = t + 1; *s; s++)
		if (*s == '/') {
			*s = 0;
			mkdir(t, 0777);
			*s = '/';
		}
	mkdir(t, 0777);
}

/* fields in tekx.lock are separated by blanks: escape blanks and % */
static char *
field_esc(const char *s)
{
	Buf b = {0};

	for (; *s; s++) {
		if (*s == '%' || *s == ' ' || *s == '\t' || *s == '\n')
			bprintf(&b, "%%%02X", (unsigned char)*s);
		else
			bputc(&b, *s);
	}
	return bstr(&b);
}

/* ---- versions ---- */

static void
version_parts(const char *v, int out[3])
{
	out[0] = out[1] = out[2] = 0;
	for (int i = 0; i < 3 && *v; i++) {
		out[i] = atoi(v);
		while (*v >= '0' && *v <= '9')
			v++;
		if (*v == '.')
			v++;
	}
}

static int
version_cmp(const char *a, const char *b)
{
	int x[3], y[3];

	version_parts(a, x);
	version_parts(b, y);
	for (int i = 0; i < 3; i++)
		if (x[i] != y[i])
			return x[i] < y[i] ? -1 : 1;
	return 0;
}

/* `have` satisfies `want` as a caret range: the same major version (or,
 * before 1.0, the same minor version) and no older */
static bool
version_ok(const char *have, const char *want)
{
	int h[3], w[3];

	if (want == NULL || !*want)
		return true;
	version_parts(have, h);
	version_parts(want, w);
	if (h[0] != w[0] || (w[0] == 0 && h[1] != w[1]))
		return false;
	return version_cmp(have, want) >= 0;
}

static bool
valid_name(const char *n)
{
	if (!*n || !((*n >= 'a' && *n <= 'z') || (*n >= '0' && *n <= '9')))
		return false;
	for (; *n; n++)
		if (!((*n >= 'a' && *n <= 'z') || (*n >= '0' && *n <= '9') || *n == '-'))
			return false;
	return true;
}

/* ---- processes ---- */

/* run argv; with `capture`, its standard output is returned there */
static int
run(char *const argv[], char **capture)
{
	int fd[2] = {-1, -1};

	if (capture && pipe(fd) != 0)
		return -1;
	pid_t pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		if (capture) {
			dup2(fd[1], 1);
			close(fd[0]);
			close(fd[1]);
		} else {
			int null = open("/dev/null", O_WRONLY);
			if (null >= 0)
				dup2(null, 1);
		}
		execvp(argv[0], argv);
		fprintf(stderr, "tekx: cannot run %s: %s\n", argv[0], strerror(errno));
		_exit(127);
	}
	if (capture) {
		close(fd[1]);
		Buf b = {0};
		char chunk[4096];
		ssize_t k;
		while ((k = read(fd[0], chunk, sizeof chunk)) > 0)
			bput(&b, chunk, (size_t)k);
		close(fd[0]);
		char *t = bstr(&b);
		size_t n = strlen(t);
		while (n && (t[n - 1] == '\n' || t[n - 1] == '\r' || t[n - 1] == ' ' || t[n - 1] == '\t'))
			t[--n] = 0;
		*capture = t;
	}
	int status;
	if (waitpid(pid, &status, 0) < 0)
		return -1;
	return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}

char *
pkg_filter(const char *program, const char *pkgdir, const char *in, size_t inlen, size_t *outlen, char **err)
{
	FILE *fi = tmpfile(), *fo = tmpfile();

	if (!fi || !fo || fwrite(in, 1, inlen, fi) != inlen || fflush(fi) != 0) {
		*err = fmt("cannot make temporary files for %s", program);
		return NULL;
	}
	rewind(fi);
	pid_t pid = fork();
	if (pid < 0) {
		*err = fmt("cannot run %s: %s", program, strerror(errno));
		return NULL;
	}
	if (pid == 0) {
		dup2(fileno(fi), 0);
		dup2(fileno(fo), 1);
		setenv("TEKX_PACKAGE_DIR", pkgdir, 1);
		setenv("TEKX_VERSION", TEKX_VERSION, 1);
		execl(program, program, (char *)NULL);
		fprintf(stderr, "tekx: cannot run %s: %s\n", program, strerror(errno));
		_exit(127);
	}
	int status;
	waitpid(pid, &status, 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		*err = WIFEXITED(status) ? fmt("%s failed (exit status %d)", program, WEXITSTATUS(status)) :
		    fmt("%s was killed by a signal", program);
		return NULL;
	}
	rewind(fo);
	char *out = read_all(fo, outlen);
	fclose(fi);
	fclose(fo);
	return out;
}

/* ---- trees: hash, copy, remove ---- */

typedef VEC(char *) Paths;

static int
by_str(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

/* every file under root, relative, in path order; false on a symbolic link */
static bool
list_files(const char *root, const char *rel, Paths *out, char **err)
{
	char *dir = *rel ? path_join(root, rel) : xstrdup(root);
	DIR *d = opendir(dir);

	if (d == NULL) {
		*err = fmt("cannot read %s: %s", dir, strerror(errno));
		return false;
	}
	Paths names = {0};
	struct dirent *e;
	while ((e = readdir(d)) != NULL)
		if (!streq(e->d_name, ".") && !streq(e->d_name, "..") && !streq(e->d_name, ".git"))
			VPUSH(names, xstrdup(e->d_name));
	closedir(d);
	if (names.n)
		sort(names.v, names.n, sizeof *names.v, by_str);
	for (size_t i = 0; i < names.n; i++) {
		char *r = *rel ? path_join(rel, names.v[i]) : names.v[i];
		char *full = path_join(root, r);
		struct stat st;
		if (lstat(full, &st) != 0) {
			*err = fmt("cannot read %s: %s", full, strerror(errno));
			return false;
		}
		if (S_ISLNK(st.st_mode)) {
			*err = fmt("packages may not contain symbolic links: %s", full);
			return false;
		}
		if (S_ISDIR(st.st_mode)) {
			if (!list_files(root, r, out, err))
				return false;
		} else if (S_ISREG(st.st_mode)) {
			VPUSH(*out, r);
		}
	}
	return true;
}

static char *
tree_hash(const char *root, char **err)
{
	Paths files = {0};
	Sha256 c;
	u8 d[32];

	if (!list_files(root, "", &files, err))
		return NULL;
	if (files.n)
		sort(files.v, files.n, sizeof *files.v, by_str);
	sha256_init(&c);
	for (size_t i = 0; i < files.n; i++) {
		char *full = path_join(root, files.v[i]);
		struct stat st;
		size_t n = 0;
		stat(full, &st);
		FILE *f = fopen(full, "rb");
		if (f == NULL) {
			*err = fmt("cannot read %s: %s", full, strerror(errno));
			return NULL;
		}
		char *data = read_all(f, &n);
		fclose(f);
		/* path NUL mode size NUL content */
		Buf head = {0};
		bput(&head, files.v[i], strlen(files.v[i]) + 1);
		bprintf(&head, "%c%zu", (st.st_mode & 0111) ? 'x' : '-', n);
		bputc(&head, 0);
		sha256_update(&c, head.s, head.n);
		sha256_update(&c, data, n);
	}
	sha256_final(&c, d);
	return fmt("sha256:%s", sha256_hex(d));
}

static bool
copy_tree(const char *src, const char *dst, char **err)
{
	Paths files = {0};

	if (!list_files(src, "", &files, err))
		return false;
	mkdirs(dst);
	for (size_t i = 0; i < files.n; i++) {
		char *from = path_join(src, files.v[i]), *to = path_join(dst, files.v[i]);
		mkdirs(dirname_of(to));
		struct stat st;
		size_t n = 0;
		stat(from, &st);
		FILE *f = fopen(from, "rb");
		if (f == NULL) {
			*err = fmt("cannot read %s: %s", from, strerror(errno));
			return false;
		}
		char *data = read_all(f, &n);
		fclose(f);
		FILE *g = fopen(to, "wb");
		if (g == NULL || fwrite(data, 1, n, g) != n || fclose(g) != 0) {
			*err = fmt("cannot write %s: %s", to, strerror(errno));
			return false;
		}
		chmod(to, (st.st_mode & 0111) ? 0755 : 0644);
	}
	return true;
}

static void
remove_tree(const char *p)
{
	struct stat st;

	if (lstat(p, &st) != 0)
		return;
	if (S_ISDIR(st.st_mode)) {
		DIR *d = opendir(p);
		struct dirent *e;
		if (d) {
			while ((e = readdir(d)) != NULL)
				if (!streq(e->d_name, ".") && !streq(e->d_name, ".."))
					remove_tree(path_join(p, e->d_name));
			closedir(d);
		}
		rmdir(p);
	} else {
		unlink(p);
	}
}

/* ---- manifests ---- */

static char *
unq(char *s)
{
	s = trim(s);
	size_t n = strlen(s);
	if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
		s[n - 1] = 0;
		s++;
	}
	return s;
}

static Package *
read_manifest(const char *dir, char **err)
{
	char *path = path_join(dir, "package.tekx");
	FILE *f = fopen(path, "r");
	char *line;
	int no = 0;

	if (f == NULL) {
		*err = fmt("%s: no package.tekx (is this a package?)", dir);
		return NULL;
	}
	Package *p = NEW(Package);
	p->dir = absolute(dir);
	while ((line = read_line(f)) != NULL) {
		no++;
		char *c = strstr(line, "//");
		if (c && (c == line || c[-1] == ' '))
			*c = 0;
		char *s = trim(line), *rest = s;
		if (!*s)
			continue;
		while (*rest && *rest != ' ')
			rest++;
		char *key = xstrndup(s, (size_t)(rest - s));
		char *val = unq(xstrdup(rest));
		if (!*val) {
			*err = fmt("%s:%d: %s needs a value", path, no, key);
			fclose(f);
			return NULL;
		}
		if (streq(key, "package"))
			p->name = val;
		else if (streq(key, "version"))
			p->version = val;
		else if (streq(key, "summary"))
			p->summary = val;
		else if (streq(key, "requires"))
			p->requires = startswith(val, "tekx ") ? trim(val + 5) : val;
		else if (streq(key, "depends"))
			VPUSH(p->depends, val);
		else if (streq(key, "provides"))
			VPUSH(p->provides, val);
		else if (streq(key, "filter"))
			VPUSH(p->filters, val);
		else if (streq(key, "block"))
			VPUSH(p->blocks, val);
		else {
			*err = fmt("%s:%d: unknown manifest entry '%s' (expected package, version, summary, "
			    "requires, depends, provides, filter or block)", path, no, key);
			fclose(f);
			return NULL;
		}
	}
	fclose(f);
	if (!p->name || !valid_name(p->name)) {
		*err = fmt("%s: needs package \"name\" (lowercase letters, digits and -)", path);
		return NULL;
	}
	if (!p->version) {
		*err = fmt("%s: needs version \"1.0.0\"", path);
		return NULL;
	}
	if (p->provides.n == 0 && is_file(path_join(dir, "main.tekx")))
		VPUSH(p->provides, "main.tekx");
	for (size_t i = 0; i < p->provides.n; i++)
		if (!is_file(path_join(dir, p->provides.v[i]))) {
			*err = fmt("%s: provides %s, which is missing", path, p->provides.v[i]);
			return NULL;
		}
	for (size_t i = 0; i < p->filters.n; i++)
		if (access(path_join(dir, p->filters.v[i]), X_OK) != 0) {
			*err = fmt("%s: filter %s is missing or not executable", path, p->filters.v[i]);
			return NULL;
		}
	if (p->requires && version_cmp(TEKX_VERSION, p->requires) < 0) {
		*err = fmt("package %s %s needs tekx %s or later (this is tekx %s)", p->name, p->version,
		    p->requires, TEKX_VERSION);
		return NULL;
	}
	return p;
}

/* ---- the lock ---- */

typedef struct {
	char *name, *version, *hash, *kind;	/* kind: dir, git or local */
	char *where, *commit, *subdir;		/* dir: where; git: where (URL), commit, subdir */
} Entry;

typedef struct {
	char *name, *kind, *where;
} Registry;

typedef VEC(Registry) Registries;

typedef struct {
	char *root;	/* the project directory: where tekx.lock is */
	VEC(Entry) e;
	Registries r;
} Lock;

static char *
find_root(const char *start)
{
	char *d = absolute(start);

	for (;;) {
		if (is_file(path_join(d, LOCK)))
			return d;
		if (streq(d, "/"))
			return NULL;
		d = dirname_of(d);
	}
}

static bool
read_lock(const char *root, Lock *L, char **err)
{
	char *path = path_join(root, LOCK);
	FILE *f = fopen(path, "r");
	char *line;
	int no = 0;

	memset(L, 0, sizeof *L);
	L->root = xstrdup(root);
	if (f == NULL)
		return true;
	while ((line = read_line(f)) != NULL) {
		no++;
		char *s = trim(line);
		if (!*s || *s == '#')
			continue;
		char *fld[9] = {0};
		int n = 0;
		for (char *t = strtok(s, " \t"); t && n < 9; t = strtok(NULL, " \t"))
			fld[n++] = pct_unescape(t);
		if (n == 4 && streq(fld[0], "registry")) {
			Registry r = {fld[1], fld[2], fld[3]};
			VPUSH(L->r, r);
		} else if (n >= 6 && streq(fld[0], "package")) {
			Entry e = {fld[1], fld[2], fld[3], fld[4], fld[5], n > 6 ? fld[6] : NULL, n > 7 ? fld[7] : NULL};
			VPUSH(L->e, e);
		} else if (n >= 5 && streq(fld[0], "package") && streq(fld[4], "local")) {
			Entry e = {fld[1], fld[2], fld[3], fld[4], NULL, NULL, NULL};
			VPUSH(L->e, e);
		} else {
			*err = fmt("%s:%d: cannot read this line", path, no);
			fclose(f);
			return false;
		}
	}
	fclose(f);
	return true;
}

static int
by_entry(const void *a, const void *b)
{
	return strcmp(((const Entry *)a)->name, ((const Entry *)b)->name);
}

static void
write_lock(Lock *L)
{
	char *path = path_join(L->root, LOCK), *tmp = fmt("%s.tmp", path);
	FILE *f = fopen(tmp, "w");

	if (f == NULL)
		die("cannot write %s: %s", tmp, strerror(errno));
	fputs("# tekx.lock -- the exact packages this document builds with.\n"
	    "# Written by `tekx pkg`; commit it with the document.\n", f);
	for (size_t i = 0; i < L->r.n; i++)
		fprintf(f, "registry %s %s %s\n", L->r.v[i].name, L->r.v[i].kind, field_esc(L->r.v[i].where));
	if (L->e.n)
		sort(L->e.v, L->e.n, sizeof *L->e.v, by_entry);
	for (size_t i = 0; i < L->e.n; i++) {
		Entry *e = &L->e.v[i];
		fprintf(f, "package %s %s %s %s", e->name, e->version, e->hash, e->kind);
		if (streq(e->kind, "dir"))
			fprintf(f, " %s", field_esc(e->where));
		else if (streq(e->kind, "git"))
			fprintf(f, " %s %s %s", field_esc(e->where), e->commit, field_esc(e->subdir ? e->subdir : "."));
		fputc('\n', f);
	}
	if (fclose(f) != 0 || rename(tmp, path) != 0)
		die("cannot write %s: %s", path, strerror(errno));
}

static Entry *
lock_find(Lock *L, const char *name)
{
	for (size_t i = 0; i < L->e.n; i++)
		if (streq(L->e.v[i].name, name))
			return &L->e.v[i];
	return NULL;
}

/* ---- using a package from a document ---- */

Package *
pkg_use(const char *docfile, const char *name, const char *want, char **err)
{
	char *root = find_root(dirname_of(absolute(docfile)));

	if (root) {
		Lock L;
		if (!read_lock(root, &L, err))
			return NULL;
		Entry *e = lock_find(&L, name);
		if (e == NULL) {
			*err = fmt("package %s is not in %s (add it with: tekx pkg add %s)", name,
			    path_join(root, LOCK), name);
			return NULL;
		}
		char *dir = path_join(root, path_join(STORE, name));
		if (!is_dir(dir)) {
			*err = fmt("package %s is locked but not installed (run: tekx pkg install)", name);
			return NULL;
		}
		char *h = tree_hash(dir, err);
		if (h == NULL)
			return NULL;
		if (!streq(h, e->hash)) {
			*err = fmt("package %s has changed since it was locked: `tekx pkg install` restores "
			    "the locked version, `tekx pkg lock` accepts the change", name);
			return NULL;
		}
		Package *p = read_manifest(dir, err);
		if (p == NULL)
			return NULL;
		if (!version_ok(p->version, want)) {
			*err = fmt("the document wants %s %s, but %s is locked", name, want, p->version);
			return NULL;
		}
		return p;
	}
	/* no lock: packages being developed, from TEKX_PATH */
	const char *tp = getenv("TEKX_PATH");
	if (tp) {
		char *paths = xstrdup(tp);
		for (char *d = strtok(paths, ":"); d; d = strtok(NULL, ":")) {
			char *dir = path_join(d, name);
			if (is_file(path_join(dir, "package.tekx"))) {
				Package *p = read_manifest(dir, err);
				if (p && !version_ok(p->version, want)) {
					*err = fmt("the document wants %s %s, but %s has %s", name, want, dir, p->version);
					return NULL;
				}
				return p;
			}
		}
	}
	*err = fmt("cannot find package %s: no tekx.lock beside the document or above it "
	    "(tekx pkg add %s creates one), and not on TEKX_PATH", name, name);
	return NULL;
}

/* ---- fetching ---- */

static bool
looks_like_git(const char *s)
{
	size_t n = strlen(s);

	return strstr(s, "://") || startswith(s, "git@") || (n > 4 && streq(s + n - 4, ".git"));
}

/* clone url at rev ("" for the default branch) into dir; the commit in *commit */
static bool
git_fetch(const char *url, const char *rev, const char *dir, char **commit, char **err)
{
	char *clone[] = {"git", "clone", "--quiet", "--no-checkout", (char *)url, (char *)dir, NULL};
	if (run(clone, NULL) != 0) {
		*err = fmt("git clone %s failed", url);
		return false;
	}
	const char *r = rev && *rev ? rev : "HEAD";
	char *checkout[] = {"git", "-C", (char *)dir, "checkout", "--quiet", "--detach", (char *)r, NULL};
	if (run(checkout, NULL) != 0) {
		*err = fmt("%s has no revision %s", url, r);
		return false;
	}
	char *parse[] = {"git", "-C", (char *)dir, "rev-parse", "HEAD", NULL};
	if (run(parse, commit) != 0 || strlen(*commit) < 40) {
		*err = fmt("cannot read the commit of %s", url);
		return false;
	}
	return true;
}

/* bring the content of an entry's source into `dst`; false and *err on failure */
static bool
fetch(Entry *e, const char *root, const char *dst, char **err)
{
	if (streq(e->kind, "local")) {
		*err = fmt("package %s was accepted locally (tekx pkg lock) and exists only in %s: "
		    "copy or commit that directory", e->name, path_join(root, path_join(STORE, e->name)));
		return false;
	}
	if (streq(e->kind, "dir")) {
		char *src = e->where[0] == '/' ? e->where : path_join(root, e->where);
		return copy_tree(src, dst, err);
	}
	char *tmp = fmt("%s.clone", dst), *commit = NULL;
	remove_tree(tmp);
	bool ok = git_fetch(e->where, e->commit, tmp, &commit, err);
	if (ok) {
		const char *sub = e->subdir && !streq(e->subdir, ".") ? e->subdir : "";
		char *src = *sub ? path_join(tmp, sub) : tmp;
		if (!is_dir(src)) {
			*err = fmt("%s at %s has no directory %s", e->where, commit, sub);
			ok = false;
		} else {
			ok = copy_tree(src, dst, err);
		}
	}
	remove_tree(tmp);
	return ok;
}

/* ---- commands ---- */

static Lock
open_project(bool create)
{
	char *root = find_root(".");
	Lock L;
	char *err = NULL;

	if (root == NULL) {
		if (!create)
			die("no tekx.lock here or above (tekx pkg add creates one)");
		root = absolute(".");
	}
	if (!read_lock(root, &L, &err))
		die("%s", err);
	return L;
}

static char *
staging(Lock *L, const char *what)
{
	char *store = path_join(L->root, STORE);
	mkdirs(store);
	char *d = path_join(store, fmt(".staging-%s-%d", what, (int)getpid()));
	remove_tree(d);
	return d;
}

/* move a verified package into place */
static void
install_dir(Lock *L, const char *name, const char *staged)
{
	char *dst = path_join(L->root, path_join(STORE, name)), *old = fmt("%s.old", dst);

	remove_tree(old);
	if (is_dir(dst) && rename(dst, old) != 0)
		die("cannot replace %s: %s", dst, strerror(errno));
	if (rename(staged, dst) != 0)
		die("cannot install %s: %s", dst, strerror(errno));
	remove_tree(old);
}

/* the registries to search: the project's (in tekx.lock), then
 * TEKX_REGISTRY (locations separated by blanks), then the user's
 * ~/.config/tekx/registries (lines of "NAME LOCATION") */
static void
user_registries(Lock *L, Registries *out)
{
	for (size_t i = 0; i < L->r.n; i++)
		VPUSH(*out, L->r.v[i]);
	const char *env = getenv("TEKX_REGISTRY");
	if (env) {
		char *s = xstrdup(env);
		int k = 0;
		for (char *t = strtok(s, " \t\n"); t; t = strtok(NULL, " \t\n")) {
			Registry r = {fmt("env%d", ++k), looks_like_git(t) ? "git" : "dir", t};
			VPUSH(*out, r);
		}
	}
	const char *home = getenv("HOME");
	FILE *f = home ? fopen(fmt("%s/.config/tekx/registries", home), "r") : NULL;
	char *line;
	while (f && (line = read_line(f)) != NULL) {
		char *s = trim(line), *sp = strchr(s, ' ');
		if (!*s || *s == '#' || !sp)
			continue;
		*sp = 0;
		char *where = trim(sp + 1);
		Registry r = {xstrdup(s), looks_like_git(where) ? "git" : "dir", xstrdup(where)};
		VPUSH(*out, r);
	}
	if (f)
		fclose(f);
}

static int cmd_add_one(Lock *L, const char *source, const char *rev, const char *sub, int depth);

/* find `name` in the registries; an entry describing where it is */
static bool
from_registry(Lock *L, const char *name, Entry *out, char **staged, char **err)
{
	Registries regs = {0};

	user_registries(L, &regs);
	for (size_t i = 0; i < regs.n; i++) {
		Registry *r = &regs.v[i];
		if (streq(r->kind, "dir")) {
			char *base = r->where[0] == '/' ? r->where : path_join(L->root, r->where);
			char *dir = path_join(base, name);
			if (!is_file(path_join(dir, "package.tekx")))
				continue;
			*out = (Entry){xstrdup(name), NULL, NULL, "dir", dir[0] == '/' ? dir : absolute(dir), NULL, NULL};
			*staged = staging(L, name);
			return copy_tree(dir, *staged, err);
		}
		char *tmp = staging(L, fmt("registry-%zu", i)), *commit = NULL;
		if (!git_fetch(r->where, "", tmp, &commit, err)) {
			remove_tree(tmp);
			fprintf(stderr, "tekx: registry %s: %s\n", r->name, *err);
			continue;
		}
		char *dir = path_join(tmp, name);
		if (!is_file(path_join(dir, "package.tekx"))) {
			remove_tree(tmp);
			continue;
		}
		*out = (Entry){xstrdup(name), NULL, NULL, "git", r->where, commit, xstrdup(name)};
		*staged = staging(L, name);
		bool ok = copy_tree(dir, *staged, err);
		remove_tree(tmp);
		return ok;
	}
	*err = regs.n ? fmt("no registry has a package called %s", name) :
	    fmt("%s is not a directory or a git URL, and no registry is configured "
		"(tekx pkg registry add NAME LOCATION)", name);
	return false;
}

static int
cmd_add_one(Lock *L, const char *source, const char *rev, const char *sub, int depth)
{
	Entry e = {0};
	char *staged = NULL, *err = NULL;

	if (depth > 32)
		die("package dependencies nested too deeply at %s", source);
	if (is_dir(source)) {
		char *src = sub ? path_join(source, sub) : xstrdup(source);
		e = (Entry){NULL, NULL, NULL, "dir", absolute(src), NULL, NULL};
		staged = staging(L, "dir");
		if (!copy_tree(src, staged, &err))
			die("%s", err);
	} else if (looks_like_git(source)) {
		char *tmp = staging(L, "clone"), *commit = NULL;
		if (!git_fetch(source, rev, tmp, &commit, &err))
			die("%s", err);
		char *src = sub ? path_join(tmp, sub) : tmp;
		if (!is_dir(src))
			die("%s has no directory %s", source, sub);
		e = (Entry){NULL, NULL, NULL, "git", xstrdup(source), commit, xstrdup(sub ? sub : ".")};
		staged = staging(L, "git");
		if (!copy_tree(src, staged, &err))
			die("%s", err);
		remove_tree(tmp);
	} else {
		if (!valid_name(source))
			die("%s is not a directory, a git URL or a package name", source);
		if (!from_registry(L, source, &e, &staged, &err))
			die("%s", err);
	}
	Package *p = read_manifest(staged, &err);
	if (p == NULL) {
		remove_tree(staged);
		die("%s", err);
	}
	if (e.name && !streq(e.name, p->name)) {
		remove_tree(staged);
		die("the registry's %s calls itself %s", e.name, p->name);
	}
	e.name = p->name;
	e.version = p->version;
	e.hash = tree_hash(staged, &err);
	if (e.hash == NULL)
		die("%s", err);
	/* dependencies first, from the registries unless already locked */
	for (size_t i = 0; i < p->depends.n; i++) {
		char *d = xstrdup(p->depends.v[i]), *sp = strchr(d, ' ');
		char *want = sp ? trim(sp + 1) : NULL;
		if (sp)
			*sp = 0;
		Entry *have = lock_find(L, d);
		if (have && version_ok(have->version, want))
			continue;
		if (have)
			die("%s needs %s %s, but %s %s is locked", p->name, d, want, d, have->version);
		cmd_add_one(L, d, NULL, NULL, depth + 1);
		have = lock_find(L, d);
		if (have == NULL || !version_ok(have->version, want))
			die("%s needs %s %s, which no registry provides", p->name, d, want ? want : "");
	}
	install_dir(L, p->name, staged);
	Entry *old = lock_find(L, p->name);
	if (old)
		*old = e;
	else
		VPUSH(L->e, e);
	write_lock(L);
	printf("added %s %s (%.19s…) from %s%s%s\n", e.name, e.version, e.hash, e.where,
	    e.commit ? " at " : "", e.commit ? fmt("%.12s", e.commit) : "");
	return 0;
}

static int
cmd_install(Lock *L)
{
	int bad = 0;

	for (size_t i = 0; i < L->e.n; i++) {
		Entry *e = &L->e.v[i];
		char *dst = path_join(L->root, path_join(STORE, e->name)), *err = NULL;
		if (is_dir(dst)) {
			char *h = tree_hash(dst, &err);
			if (h && streq(h, e->hash))
				continue;
		}
		char *staged = staging(L, e->name);
		if (!fetch(e, L->root, staged, &err)) {
			fprintf(stderr, "tekx: %s: %s\n", e->name, err);
			remove_tree(staged);
			bad++;
			continue;
		}
		char *h = tree_hash(staged, &err);
		if (h == NULL || !streq(h, e->hash)) {
			fprintf(stderr, "tekx: %s: the source no longer has the locked content "
			    "(locked %.19s…, found %.19s…); refusing it\n", e->name, e->hash, h ? h : "nothing");
			remove_tree(staged);
			bad++;
			continue;
		}
		install_dir(L, e->name, staged);
		printf("installed %s %s\n", e->name, e->version);
	}
	return bad ? 1 : 0;
}

static int
cmd_verify(Lock *L, bool quiet)
{
	int bad = 0;

	for (size_t i = 0; i < L->e.n; i++) {
		Entry *e = &L->e.v[i];
		char *dst = path_join(L->root, path_join(STORE, e->name)), *err = NULL;
		const char *state = "ok";
		if (!is_dir(dst)) {
			state = "missing (tekx pkg install)";
			bad++;
		} else {
			char *h = tree_hash(dst, &err);
			if (h == NULL || !streq(h, e->hash)) {
				state = "changed since locked (tekx pkg install restores it, tekx pkg lock accepts it)";
				bad++;
			}
		}
		if (!quiet || !streq(state, "ok"))
			printf("%-20s %-10s %s\n", e->name, e->version, state);
	}
	return bad ? 1 : 0;
}

/* accept the installed packages as they are now */
static int
cmd_lock(Lock *L)
{
	for (size_t i = 0; i < L->e.n; i++) {
		Entry *e = &L->e.v[i];
		char *dst = path_join(L->root, path_join(STORE, e->name)), *err = NULL;
		char *h = is_dir(dst) ? tree_hash(dst, &err) : NULL;
		if (h == NULL || streq(h, e->hash))
			continue;
		Package *p = read_manifest(dst, &err);
		if (p == NULL)
			die("%s", err);
		e->hash = h;
		e->version = p->version;
		e->kind = "local";
		printf("locked %s %s as it is in %s\n", e->name, e->version, dst);
	}
	write_lock(L);
	return 0;
}

static int
cmd_list(Lock *L)
{
	for (size_t i = 0; i < L->e.n; i++) {
		Entry *e = &L->e.v[i];
		Package *p = NULL;
		char *err = NULL, *dst = path_join(L->root, path_join(STORE, e->name));
		if (is_dir(dst))
			p = read_manifest(dst, &err);
		printf("%-20s %-10s %s%s%s\n", e->name, e->version, streq(e->kind, "local") ? "(local)" : e->where,
		    p && p->summary ? "  -- " : "", p && p->summary ? p->summary : "");
	}
	for (size_t i = 0; i < L->r.n; i++)
		printf("registry %s: %s\n", L->r.v[i].name, L->r.v[i].where);
	return 0;
}

static int
cmd_remove(Lock *L, const char *name)
{
	Entry *e = lock_find(L, name);

	if (e == NULL)
		die("%s is not in tekx.lock", name);
	for (size_t i = 0; i < L->e.n; i++) {
		char *err = NULL, *dst = path_join(L->root, path_join(STORE, L->e.v[i].name));
		Package *p = is_dir(dst) ? read_manifest(dst, &err) : NULL;
		for (size_t k = 0; p && k < p->depends.n; k++)
			if (startswith(p->depends.v[k], name) &&
			    (p->depends.v[k][strlen(name)] == 0 || p->depends.v[k][strlen(name)] == ' '))
				die("%s depends on %s", p->name, name);
	}
	remove_tree(path_join(L->root, path_join(STORE, name)));
	*e = L->e.v[--L->e.n];
	write_lock(L);
	printf("removed %s\n", name);
	return 0;
}

static int
cmd_new(const char *name)
{
	if (!valid_name(name))
		die("a package name is lowercase letters, digits and -: %s", name);
	if (is_dir(name) || is_file(name))
		die("%s already exists", name);
	mkdirs(name);
	FILE *f = fopen(path_join(name, "package.tekx"), "w");
	if (f == NULL)
		die("cannot write %s/package.tekx", name);
	fprintf(f, "package \"%s\"\nversion \"0.1.0\"\nsummary \"What this package is for\"\n"
	    "requires \"tekx %s\"\nprovides \"main.tekx\"\n"
	    "// depends \"other-package 1.0\"\n"
	    "// filter \"bin/my-filter\"     a program that rewrites the document tree\n"
	    "// block \"my-block\"           a block that filter implements\n", name, TEKX_VERSION);
	fclose(f);
	f = fopen(path_join(name, "main.tekx"), "w");
	if (f == NULL)
		die("cannot write %s/main.tekx", name);
	fputs("// Definitions every document that uses this package receives.\n\n"
	    "define operator Var, Cov\n"
	    "define E(x) as EE[x] read as \"the expected value of {x}\"\n"
	    "define conjecture2 like conjecture titled \"Open Problem\"\n", f);
	fclose(f);
	printf("created %s/ (package.tekx, main.tekx)\n", name);
	return 0;
}

static int
cmd_registry(Lock *L, int argc, char **argv)
{
	if (argc >= 3 && streq(argv[0], "add")) {
		for (size_t i = 0; i < L->r.n; i++)
			if (streq(L->r.v[i].name, argv[1]))
				die("registry %s already exists", argv[1]);
		const char *where = argv[2];
		bool git = looks_like_git(where);
		if (!git && !is_dir(where))
			die("%s is neither a directory nor a git URL", where);
		Registry r = {argv[1], git ? "git" : "dir", git ? xstrdup(where) : absolute(where)};
		VPUSH(L->r, r);
		write_lock(L);
		printf("registry %s: %s\n", r.name, r.where);
		return 0;
	}
	if (argc >= 2 && streq(argv[0], "remove")) {
		for (size_t i = 0; i < L->r.n; i++)
			if (streq(L->r.v[i].name, argv[1])) {
				L->r.v[i] = L->r.v[--L->r.n];
				write_lock(L);
				return 0;
			}
		die("no registry %s", argv[1]);
	}
	Registries regs = {0};
	user_registries(L, &regs);
	for (size_t i = 0; i < regs.n; i++)
		printf("%s %s %s\n", regs.v[i].name, regs.v[i].kind, regs.v[i].where);
	return 0;
}

/* what the registries offer: name, version, summary */
static int
cmd_search(Lock *L, const char *word)
{
	Registries regs = {0};

	user_registries(L, &regs);
	for (size_t i = 0; i < regs.n; i++) {
		Registry *r = &regs.v[i];
		char *base = NULL, *tmp = NULL, *err = NULL, *commit = NULL;
		if (streq(r->kind, "dir")) {
			base = r->where[0] == '/' ? r->where : path_join(L->root, r->where);
		} else {
			tmp = staging(L, fmt("search-%zu", i));
			if (!git_fetch(r->where, "", tmp, &commit, &err)) {
				fprintf(stderr, "tekx: registry %s: %s\n", r->name, err);
				continue;
			}
			base = tmp;
		}
		DIR *d = opendir(base);
		struct dirent *e;
		Paths names = {0};
		while (d && (e = readdir(d)) != NULL)
			if (e->d_name[0] != '.' && is_file(path_join(path_join(base, e->d_name), "package.tekx")))
				VPUSH(names, xstrdup(e->d_name));
		if (d)
			closedir(d);
		if (names.n)
			sort(names.v, names.n, sizeof *names.v, by_str);
		for (size_t k = 0; k < names.n; k++) {
			Package *p = read_manifest(path_join(base, names.v[k]), &err);
			if (p == NULL)
				continue;
			if (word && !strstr(p->name, word) && !(p->summary && strstr(p->summary, word)))
				continue;
			printf("%-20s %-10s %s  [%s]\n", p->name, p->version, p->summary ? p->summary : "", r->name);
		}
		if (tmp)
			remove_tree(tmp);
	}
	return 0;
}

/* fetch the newest content of each package's source (or of one) */
static int
cmd_update(Lock *L, const char *only)
{
	size_t n = L->e.n;
	Entry *e = xcalloc(n + 1, sizeof *e);
	int done = 0;

	memcpy(e, L->e.v, n * sizeof *e);
	for (size_t i = 0; i < n; i++) {
		if (only && !streq(e[i].name, only))
			continue;
		done++;
		if (streq(e[i].kind, "local")) {
			printf("%s is local (accepted with tekx pkg lock): nothing to fetch\n", e[i].name);
			continue;
		}
		const char *sub = e[i].subdir && !streq(e[i].subdir, ".") ? e[i].subdir : NULL;
		cmd_add_one(L, e[i].where, NULL, streq(e[i].kind, "git") ? sub : NULL, 0);
	}
	if (only && done == 0)
		die("%s is not in tekx.lock", only);
	return 0;
}

static const char pkg_usage[] =
    "usage: tekx pkg new NAME                 make a package skeleton\n"
    "       tekx pkg add SOURCE [--rev REV] [--path DIR]\n"
    "                                         SOURCE: a directory, a git URL, or a name in a registry\n"
    "       tekx pkg install                  install what tekx.lock pins, verifying every hash\n"
    "       tekx pkg update [NAME]            fetch the newest content of the sources and lock it\n"
    "       tekx pkg verify                   check the installed packages against tekx.lock\n"
    "       tekx pkg lock                     accept changes made to installed packages\n"
    "       tekx pkg list | remove NAME | search [WORD]\n"
    "       tekx pkg registry [add NAME DIR-OR-GIT-URL | remove NAME]\n";

int
pkg_main(int argc, char **argv)
{
	if (argc < 1 || streq(argv[0], "-h") || streq(argv[0], "--help")) {
		fputs(pkg_usage, argc < 1 ? stderr : stdout);
		return argc < 1 ? 2 : 0;
	}
	const char *cmd = argv[0];
	if (streq(cmd, "new") && argc == 2)
		return cmd_new(argv[1]);
	if (streq(cmd, "add") && argc >= 2) {
		const char *rev = NULL, *sub = NULL;
		for (int i = 2; i < argc; i++) {
			if (streq(argv[i], "--rev") && i + 1 < argc)
				rev = argv[++i];
			else if (streq(argv[i], "--path") && i + 1 < argc)
				sub = argv[++i];
			else {
				fputs(pkg_usage, stderr);
				return 2;
			}
		}
		Lock L = open_project(true);
		return cmd_add_one(&L, argv[1], rev, sub, 0);
	}
	if (streq(cmd, "registry")) {
		Lock L = open_project(argc >= 2 && streq(argv[1], "add"));
		return cmd_registry(&L, argc - 1, argv + 1);
	}
	if (streq(cmd, "search")) {
		char *root = find_root(".");
		Lock L;
		char *err = NULL;
		if (!read_lock(root ? root : ".", &L, &err))
			die("%s", err);
		return cmd_search(&L, argc > 1 ? argv[1] : NULL);
	}
	Lock L = open_project(false);
	if (streq(cmd, "install") && argc == 1)
		return cmd_install(&L);
	if (streq(cmd, "verify") && argc == 1)
		return cmd_verify(&L, false);
	if (streq(cmd, "lock") && argc == 1)
		return cmd_lock(&L);
	if (streq(cmd, "list") && argc == 1)
		return cmd_list(&L);
	if (streq(cmd, "remove") && argc == 2)
		return cmd_remove(&L, argv[1]);
	if (streq(cmd, "update") && argc <= 2)
		return cmd_update(&L, argc == 2 ? argv[1] : NULL);
	fputs(pkg_usage, stderr);
	return 2;
}
