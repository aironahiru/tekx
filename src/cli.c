/*
 * cli.c -- the commands around the compiler: watch a document, start a
 * new one.
 */
#define _XOPEN_SOURCE 700	/* realpath, nanosleep */

#include "tekx.h"

#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ---- tekx -w: build, then build again whenever an input changes ---- */

typedef struct {
	char *path;
	time_t sec;
	long nsec;
	off_t size;
} Stamp;

typedef VEC(Stamp) Stamps;

/* the inputs a document can have: sources, bibliographies, images, and
 * the installed packages, under the document's directory */
static bool
input_file(const char *name)
{
	static const char *ext[] = {".tekx", ".bib", ".png", ".jpg", ".jpeg", ".otf", ".ttf", ".lock"};
	size_t n = strlen(name);

	for (size_t i = 0; i < NELEM(ext); i++) {
		size_t k = strlen(ext[i]);
		if (n > k && streq(name + n - k, ext[i]))
			return true;
	}
	return false;
}

static void
scan(const char *dir, int depth, bool in_store, Stamps *out)
{
	DIR *d = opendir(dir);
	struct dirent *e;

	if (d == NULL || depth > 6)
		return;
	while ((e = readdir(d)) != NULL) {
		const char *n = e->d_name;
		if (streq(n, ".") || streq(n, "..") || streq(n, ".git"))
			continue;
		/* hidden directories are skipped, except the package store */
		bool store = in_store || streq(n, ".tekx");
		if (n[0] == '.' && !store)
			continue;
		char *p = fmt("%s/%s", dir, n);
		struct stat st;
		if (stat(p, &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode)) {
			scan(p, depth + 1, store, out);
		} else if (S_ISREG(st.st_mode) && (store || input_file(n))) {
			Stamp s = {p, st.st_mtim.tv_sec, st.st_mtim.tv_nsec, st.st_size};
			VPUSH(*out, s);
		}
	}
	closedir(d);
}

static bool
same(const Stamps *a, const Stamps *b)
{
	if (a->n != b->n)
		return false;
	for (size_t i = 0; i < a->n; i++)
		if (!streq(a->v[i].path, b->v[i].path) || a->v[i].sec != b->v[i].sec ||
		    a->v[i].nsec != b->v[i].nsec || a->v[i].size != b->v[i].size)
			return false;
	return true;
}

static double
now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

int
watch_main(const char *self, int argc, char **argv, const char *file)
{
	/* the same command without -w, run afresh for every build */
	char **args = xcalloc((size_t)argc + 1, sizeof *args);
	int n = 0;
	for (int i = 0; i < argc; i++)
		if (!streq(argv[i], "-w"))
			args[n++] = argv[i];
	args[n] = NULL;
	const char *slash = strrchr(file, '/');
	char *dir = slash ? xstrndup(file, (size_t)(slash - file)) : xstrdup(".");
	Stamps last = {0};
	bool first = true;

	fprintf(stderr, "tekx: watching %s (Ctrl-C stops)\n", dir);
	for (;;) {
		Stamps cur = {0};
		scan(dir, 0, false, &cur);
		if (first || !same(&cur, &last)) {
			first = false;
			last = cur;
			double t0 = now_ms();
			pid_t pid = fork();
			if (pid < 0)
				die("fork: %s", strerror(errno));
			if (pid == 0) {
				execv(self, args);
				execvp(args[0], args);
				fprintf(stderr, "tekx: cannot run %s: %s\n", self, strerror(errno));
				_exit(127);
			}
			int status = 0;
			waitpid(pid, &status, 0);
			time_t wall = time(NULL);
			struct tm tm;
			localtime_r(&wall, &tm);
			bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
			fprintf(stderr, "tekx: %02d:%02d:%02d %s %s (%.0f ms)\n", tm.tm_hour, tm.tm_min, tm.tm_sec,
			    ok ? "built" : "errors in", file, now_ms() - t0);
			/* rescan, so that files the build itself touched do not loop */
			Stamps after = {0};
			scan(dir, 0, false, &after);
			last = after;
		}
		struct timespec pause = {0, 200 * 1000 * 1000};
		nanosleep(&pause, NULL);
	}
}

/* ---- tekx new: a paper to start from ---- */

static const char paper_template[] =
    "// %s.tekx -- build with: tekx %s.tekx   (or tekx -w %s.tekx while writing)\n"
    "tekx " TEKX_VERSION_SHORT "\n"
    "title \"The Title of the Paper\"\n"
    "author \"Your Name\", affiliation: \"Department, University\", email: \"you@example.org\"\n"
    "date today\n"
    "msc \"11A41 (primary)\"\n"
    "keywords \"first key word, second key word\"\n"
    "numbering by section\n"
    "bibliography \"%s.bib\"\n"
    "\n"
    "abstract\n"
    "  One paragraph on what the paper proves, and why it matters.\n"
    "\n"
    "section Introduction #intro\n"
    "\n"
    "Euclid proved that there are infinitely many primes [@euclid, Prop. IX.20].\n"
    "Writing $pi(x)$ for the number of primes up to $x$, the statement\n"
    "\n"
    "    limit of pi(x) log x / x as x approaches infinity = 1   #pnt\n"
    "\n"
    "is the prime number theorem [@hadamard; @poussin].\n"
    "\n"
    "theorem \"Main\" #main\n"
    "  State the main result here; refer to it as Theorem @main, and to the\n"
    "  display above as (@pnt).\n"
    "\n"
    "proof\n"
    "  The argument. A formula can be written the way it is read, as\n"
    "  $sum 1/p for p <= x$, or in LaTeX notation, as $\\sum_{p \\le x} 1/p$.\n"
    "\n"
    "section Preliminaries\n"
    "\n"
    "lemma #lem\n"
    "  A lemma, with a display:\n"
    "\n"
    "      integral of e^(-t^2) dt from -infinity to infinity = sqrt pi\n"
    "\n"
    "appendix\n"
    "\n"
    "section Computations\n"
    "\n"
    "Tables, figures and long calculations can go here.\n";

static const char bib_template[] =
    "@book{euclid,\n"
    "  author    = {Euclid},\n"
    "  title     = {Elements},\n"
    "  note      = {Book IX},\n"
    "  year      = {300 BC},\n"
    "}\n"
    "@article{hadamard,\n"
    "  author  = {Hadamard, Jacques},\n"
    "  title   = {Sur la distribution des z{\\'e}ros de la fonction $\\zeta(s)$ et ses cons{\\'e}quences arithm{\\'e}tiques},\n"
    "  journal = {Bull. Soc. Math. France},\n"
    "  volume  = {24},\n"
    "  year    = {1896},\n"
    "  pages   = {199--220},\n"
    "}\n"
    "@article{poussin,\n"
    "  author  = {de la Vall{\\'e}e Poussin, Charles},\n"
    "  title   = {Recherches analytiques sur la th{\\'e}orie des nombres premiers},\n"
    "  journal = {Ann. Soc. Sci. Bruxelles},\n"
    "  volume  = {20},\n"
    "  year    = {1896},\n"
    "  pages   = {183--256},\n"
    "}\n";

int
new_main(int argc, char **argv)
{
	if (argc != 1 || argv[0][0] == '-') {
		fputs("usage: tekx new NAME      writes NAME.tekx and NAME.bib, a paper to start from\n", stderr);
		return 2;
	}
	const char *name = argv[0];
	char *tekx = fmt("%s.tekx", name), *bib = fmt("%s.bib", name);
	struct stat st;
	if (stat(tekx, &st) == 0 || stat(bib, &st) == 0)
		die("%s or %s already exists", tekx, bib);
	const char *base = strrchr(name, '/') ? strrchr(name, '/') + 1 : name;
	FILE *f = fopen(tekx, "w");
	if (f == NULL)
		die("cannot write %s: %s", tekx, strerror(errno));
	fprintf(f, paper_template, base, base, base, base);
	fclose(f);
	f = fopen(bib, "w");
	if (f == NULL)
		die("cannot write %s: %s", bib, strerror(errno));
	fputs(bib_template, f);
	fclose(f);
	printf("created %s and %s; build with: tekx %s\n", tekx, bib, tekx);
	return 0;
}
