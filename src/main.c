/*
 * tekx -- typeset a .tekx document into an accessible PDF.
 *
 * The work is done by five filters, each of which can be run alone:
 *
 *	tekx pp     macro preprocessor	.tekx -> core .tekx
 *	tekx parse  structure		core .tekx -> document tree
 *	tekx eqn    mathematics		tree -> tree with laid-out formulas
 *	tekx set    typesetting		tree -> page description
 *	tekx pdf    device back end	page description -> PDF
 *	tekx html   web back end	tree with mathematics -> HTML and MathML
 *
 * `tekx file.tekx` runs them in sequence, connected by in-memory text
 * streams, exactly as the shell pipeline
 *
 *	tekx pp <file.tekx | tekx parse | tekx eqn | tekx set | tekx pdf >file.pdf
 */
#include "tekx.h"
#include "eqn.h"

#include <errno.h>

typedef int (*Stage)(FILE *, FILE *, const char *);

static const struct {
	const char *name;
	Stage fn;
} stages[] = {
	{"pp", pp_main},
	{"parse", parse_main},
	{"eqn", eqn_main},
	{"set", set_main},
	{"pdf", pdf_main},
	{"html", html_main},
};

enum { PP, PARSE, EQN, SET, PDF, HTML };

static const char usage_text[] =
    "usage: tekx [-E | -P | -M | -S] [-o output] file.tekx\n"
    "       tekx pp | parse | eqn | set | pdf | html [file]\n"
    "       tekx explain [formula ...]\n"
    "       tekx -v | -h\n";

static void
usage(void)
{
	fputs(usage_text, stderr);
	exit(2);
}

/* How was a formula read?  The grouping the parser chose, the speech, the
 * copy text and the MathML, for formulas given as arguments or one per line
 * on the standard input. */
static int
explain(int argc, char **argv)
{
	VEC(char *) formulas = {0};
	char *line;

	if (argc > 0) {
		Buf b = {0};
		for (int i = 0; i < argc; i++)
			bprintf(&b, "%s%s", i ? " " : "", argv[i]);
		VPUSH(formulas, bstr(&b));
	} else {
		while ((line = read_line(stdin)) != NULL)
			if (*trim(line))
				VPUSH(formulas, trim(line));
	}
	diag_file = "formula";
	for (size_t i = 0; i < formulas.n; i++) {
		char *src = formulas.v[i];
		if (src[0] == '$' && src[strlen(src) - 1] == '$' && strlen(src) > 1) {
			src[strlen(src) - 1] = 0;
			src++;
		}
		M *m = eqn_parse(src, (int)i + 1, 1, false, NULL, 0);
		if (i)
			putchar('\n');
		printf("formula    %s\n", src);
		printf("reads as   %s\n", eqn_grouping(m));
		printf("says       %s\n", eqn_speech(m));
		printf("copies as  %s\n", eqn_linear(m));
		printf("MathML     %s\n", eqn_mathml(m, false, src));
	}
	return nerrors ? 1 : 0;
}

static char *
default_output(const char *in)
{
	size_t n = strlen(in);

	if (n > 5 && streq(in + n - 5, ".tekx"))
		return fmt("%.*s.pdf", (int)(n - 5), in);
	return fmt("%s.pdf", in);
}

/* Run a sequence of stages on `in`; the result goes to `out`.  After an
 * error the later stages still run, so that one pass reports as much as
 * it can, but nothing is produced and the device stage is skipped. */
static int
pipeline(FILE *in, const int *seq, int n, FILE *out, const char *file)
{
	int failed = 0;

	for (int k = 0; k < n; k++) {
		int i = seq[k];
		bool last = k == n - 1;
		if (failed && (i == PDF || i == HTML))
			break;
		char *buf = NULL;
		size_t len = 0;
		FILE *o = last ? out : open_memstream(&buf, &len);
		if (o == NULL)
			die("open_memstream: %s", strerror(errno));
		int rc = stages[i].fn(in, o, file);
		if (!last)
			fclose(o);
		if (in != stdin)
			fclose(in);
		if (rc != 0 || nerrors)
			failed = rc ? rc : 1;
		if (!last && (in = fmemopen(buf, len ? len : 1, "r")) == NULL)
			die("fmemopen: %s", strerror(errno));
		if (!last && len == 0)
			fseek(in, 0, SEEK_END);
	}
	return failed;
}

int
main(int argc, char **argv)
{
	const char *output = NULL, *file = NULL;
	int last = 4;

	if (argc >= 2 && streq(argv[1], "explain"))
		return explain(argc - 2, argv + 2);
	if (argc >= 2) {
		for (size_t s = 0; s < NELEM(stages); s++) {
			if (!streq(argv[1], stages[s].name))
				continue;
			FILE *in = stdin;
			const char *name = "<stdin>";
			if (argc > 3)
				usage();
			if (argc == 3 && !streq(argv[2], "-")) {
				name = argv[2];
				if ((in = fopen(name, "rb")) == NULL)
					die("cannot open %s: %s", name, strerror(errno));
			}
			int one[] = {(int)s};
			int rc = pipeline(in, one, 1, stdout, name);
			fflush(stdout);
			return rc;
		}
	}
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (streq(a, "-E"))
			last = 0;
		else if (streq(a, "-P"))
			last = 1;
		else if (streq(a, "-M"))
			last = 2;
		else if (streq(a, "-S"))
			last = 3;
		else if (streq(a, "-o") && i + 1 < argc)
			output = argv[++i];
		else if (streq(a, "-h") || streq(a, "--help")) {
			fputs(usage_text, stdout);
			fputs("\n  -E  stop after macros       -P  stop after parsing\n"
			    "  -M  stop after mathematics  -S  stop after typesetting\n"
			    "  -o  output file (- for standard output); default: file.pdf\n\n"
			    "See tekx(1), doc/language.md and doc/machinery.md.\n", stdout);
			return 0;
		} else if (streq(a, "-v") || streq(a, "--version")) {
			printf("tekx %s\n", TEKX_VERSION);
			return 0;
		} else if (a[0] == '-' && a[1])
			usage();
		else if (file == NULL)
			file = a;
		else
			usage();
	}
	if (file == NULL)
		usage();

	FILE *in = streq(file, "-") ? stdin : fopen(file, "rb");
	if (in == NULL)
		die("cannot open %s: %s", file, strerror(errno));
	if (last < 4 && output == NULL)
		output = "-";
	if (output == NULL)
		output = default_output(streq(file, "-") ? "stdin" : file);

	/* paper.html: the same tree, through the web back end instead */
	size_t olen = strlen(output);
	bool html = last == PDF && olen > 5 && streq(output + olen - 5, ".html");
	int seq[] = {PP, PARSE, EQN, html ? HTML : SET, PDF};
	int nseq = html ? 4 : last + 1;

	/* Build the result in memory; never leave a half-written file behind. */
	char *buf = NULL;
	size_t len = 0;
	FILE *mem = open_memstream(&buf, &len);
	int rc = pipeline(in, seq, nseq, mem, file);
	fclose(mem);
	if (rc != 0)
		return rc;
	FILE *out = streq(output, "-") ? stdout : fopen(output, "wb");
	if (out == NULL)
		die("cannot write %s: %s", output, strerror(errno));
	if (fwrite(buf, 1, len, out) != len || fflush(out) != 0)
		die("write error on %s", output);
	if (out != stdout)
		fclose(out);
	return 0;
}
