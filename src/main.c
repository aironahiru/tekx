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
 *
 * `tekx file.tekx` runs them in sequence, connected by in-memory text
 * streams, exactly as the shell pipeline
 *
 *	tekx pp <file.tekx | tekx parse | tekx eqn | tekx set | tekx pdf >file.pdf
 */
#include "tekx.h"

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
};

static void
usage(void)
{
	fputs("usage: tekx [-E | -P | -M | -S] [-o output] file.tekx\n"
	    "       tekx pp | parse | eqn | set | pdf [file]\n"
	    "       tekx -v\n", stderr);
	exit(2);
}

static char *
default_output(const char *in)
{
	size_t n = strlen(in);

	if (n > 5 && streq(in + n - 5, ".tekx"))
		return fmt("%.*s.pdf", (int)(n - 5), in);
	return fmt("%s.pdf", in);
}

/* Run stages first..last on `in`; the result goes to `out`.  After an
 * error the later stages still run, so that one pass reports as much as
 * it can, but nothing is produced and the device stage is skipped. */
static int
pipeline(FILE *in, int first, int last, FILE *out, const char *file)
{
	int failed = 0;

	for (int i = first; i <= last; i++) {
		if (failed && i == 4)
			break;
		char *buf = NULL;
		size_t len = 0;
		FILE *o = i == last ? out : open_memstream(&buf, &len);
		if (o == NULL)
			die("open_memstream: %s", strerror(errno));
		int rc = stages[i].fn(in, o, file);
		if (i != last)
			fclose(o);
		if (in != stdin)
			fclose(in);
		if (rc != 0 || nerrors)
			failed = rc ? rc : 1;
		if (i != last && (in = fmemopen(buf, len ? len : 1, "r")) == NULL)
			die("fmemopen: %s", strerror(errno));
		if (i != last && len == 0)
			fseek(in, 0, SEEK_END);
	}
	return failed;
}

int
main(int argc, char **argv)
{
	const char *output = NULL, *file = NULL;
	int last = 4;

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
			int rc = pipeline(in, (int)s, (int)s, stdout, name);
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
		else if (streq(a, "-v") || streq(a, "--version")) {
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

	/* Build the result in memory; never leave a half-written file behind. */
	char *buf = NULL;
	size_t len = 0;
	FILE *mem = open_memstream(&buf, &len);
	int rc = pipeline(in, 0, last, mem, file);
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
