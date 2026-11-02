/*
 * html.c -- a second back end: the document tree with laid-out formulas in,
 * HTML with native MathML out.
 *
 * The same source that makes the PDF makes an accessible web page:
 * semantic elements for the structure, MathML for every formula (which
 * browsers render and screen readers can walk), the same numbers and
 * cross-references as the PDF, and footnotes as linked endnotes.  It reads
 * the tree after eqn, so it is one more filter in the pipeline:
 *
 *	tekx pp paper.tekx | tekx parse | tekx eqn | tekx html >paper.html
 */
#include "number.h"

#include <time.h>

static FILE *out;
static Buf endnotes;
static int nnotes;
static char *title, *subtitle, *date, *language = "en";
static VEC(char *) authors;

static void
esc(FILE *f, const char *s)
{
	for (; *s; s++) {
		switch (*s) {
		case '&': fputs("&amp;", f); break;
		case '<': fputs("&lt;", f); break;
		case '>': fputs("&gt;", f); break;
		case '"': fputs("&quot;", f); break;
		default: fputc(*s, f);
		}
	}
}

static void
besc(Buf *b, const char *s)
{
	for (; *s; s++) {
		switch (*s) {
		case '&': bputs(b, "&amp;"); break;
		case '<': bputs(b, "&lt;"); break;
		case '>': bputs(b, "&gt;"); break;
		case '"': bputs(b, "&quot;"); break;
		default: bputc(b, *s);
		}
	}
}

/* ---- inline content, into a buffer (footnotes are written elsewhere) ---- */

static void inlines(Buf *b, Node *n);

static void
inline_node(Buf *b, Node *k)
{
	const char *kd = k->kind;

	if (streq(kd, "text")) {
		besc(b, k->rest);
	} else if (streq(kd, "emph") || streq(kd, "strong")) {
		bprintf(b, "<%s>", streq(kd, "emph") ? "em" : "strong");
		inlines(b, k);
		bprintf(b, "</%s>", streq(kd, "emph") ? "em" : "strong");
	} else if (streq(kd, "code")) {
		bputs(b, "<code>");
		besc(b, k->rest);
		bputs(b, "</code>");
	} else if (streq(kd, "math")) {
		Node *m = child(k, "mathml");
		if (m)
			bputs(b, m->rest);
		else
			besc(b, k->rest);
	} else if (streq(kd, "link") || streq(kd, "url")) {
		bputs(b, "<a href=\"");
		besc(b, k->rest);
		bputs(b, "\">");
		if (streq(kd, "url"))
			besc(b, k->rest);
		else
			inlines(b, k);
		bputs(b, "</a>");
	} else if (streq(kd, "ref")) {
		Label *l = find_label(k->rest);
		if (l == NULL) {
			error_at(k->line, k->col, "unknown label @%s", k->rest);
			return;
		}
		bputs(b, "<a href=\"#");
		besc(b, l->name);
		bputc(b, '"');
		if (!streq(l->spoken, l->text)) {
			bputs(b, " aria-label=\"");
			besc(b, l->spoken);
			bputc(b, '"');
		}
		bputc(b, '>');
		besc(b, l->text);
		bputs(b, "</a>");
	} else if (streq(kd, "note")) {
		int n = ++nnotes;
		bprintf(b, "<sup class=\"fn\"><a href=\"#fn-%d\" id=\"fnref-%d\" role=\"doc-noteref\" "
		    "aria-label=\"Footnote %d\">%d</a></sup>", n, n, n, n);
		Buf note = {0};
		inlines(&note, k);
		bprintf(&endnotes, "<li id=\"fn-%d\" role=\"doc-endnote\">%s <a href=\"#fnref-%d\" "
		    "role=\"doc-backlink\" aria-label=\"Back to the text\">↩</a></li>\n", n, bstr(&note), n);
	}
}

static void
inlines(Buf *b, Node *n)
{
	for (size_t i = 0; i < n->kids.n; i++)
		inline_node(b, n->kids.v[i]);
}

static void
write_inlines(Node *n)
{
	Buf b = {0};

	inlines(&b, n);
	fputs(bstr(&b), out);
}

/* ---- blocks ---- */

static void blocks(Node *n, int depth);

/* a display whose lines all have an alignment point: one MathML table,
 * right-aligned left sides against left-aligned right sides */
static bool
aligned_display(Node *d)
{
	int lines = 0;

	for (size_t i = 0; i < d->kids.n; i++) {
		Node *ln = d->kids.v[i];
		if (!streq(ln->kind, "line"))
			continue;
		lines++;
		if (!child(ln, "mleft"))
			return false;
	}
	if (lines < 2)
		return false;
	bool numbered = false;
	for (size_t i = 0; i < d->kids.n; i++)
		if (streq(d->kids.v[i]->kind, "line") && !streq(field(d->kids.v[i], 0), "-"))
			numbered = true;
	fputs("<div class=\"display aligned\">\n<math display=\"block\"><mtable>", out);
	for (size_t i = 0; i < d->kids.n; i++) {
		Node *ln = d->kids.v[i];
		if (!streq(ln->kind, "line"))
			continue;
		char *label = field(ln, 0);
		Label *l = streq(label, "-") ? NULL : find_label(label);
		fputs("<mtr", out);
		if (l) {
			fputs(" id=\"", out);
			esc(out, l->name);
			fputc('"', out);
		}
		/* inline alignment: browsers honour it on table cells where they ignore classes */
		fprintf(out, "><mtd style=\"text-align:right\">%s</mtd><mtd style=\"text-align:left\">%s</mtd>",
		    child(ln, "mleft")->rest, child(ln, "mright")->rest);
		if (numbered)
			fprintf(out, "<mtd style=\"padding-left:3em\">%s</mtd>", l ? fmt("<mtext>%s</mtext>", l->text) : "");
		fputs("</mtr>", out);
	}
	fputs("</mtable></math>\n</div>\n", out);
	return true;
}

static void
display(Node *d)
{
	if (aligned_display(d))
		return;
	fputs("<div class=\"display\">\n", out);
	for (size_t i = 0; i < d->kids.n; i++) {
		Node *ln = d->kids.v[i];
		if (!streq(ln->kind, "line"))
			continue;
		char *label = field(ln, 0);
		Node *m = child(ln, "mathml");
		Label *l = streq(label, "-") ? NULL : find_label(label);
		if (l) {
			fputs("<div class=\"equation\" id=\"", out);
			esc(out, l->name);
			fputs("\">", out);
		} else {
			fputs("<div class=\"equation\">", out);
		}
		if (m)
			fputs(m->rest, out);
		if (l)
			fprintf(out, "<span class=\"eqno\">%s</span>", l->text);
		fputs("</div>\n", out);
	}
	fputs("</div>\n", out);
}

static void
theorem(Node *t)
{
	char *kind = field(t, 0), *label = field(t, 1);
	Env *env = find_env(kind);
	Label *l = streq(label, "-") ? NULL : find_label(label);
	Node *h = child(t, "heading");
	static int count;
	int style = env ? env->style : 0;
	const char *styles[] = {"plain", "definition", "remark"};

	count++;
	fprintf(out, "<div class=\"theorem %s\" role=\"group\"", styles[style]);
	if (l) {
		fputs(" id=\"", out);
		esc(out, l->name);
		fputc('"', out);
	}
	fprintf(out, " aria-label=\"%s %d\">\n", env ? env->title : kind, count);
	Buf head = {0};
	bprintf(&head, "<span class=\"thead\">%s %d</span>", env ? env->title : kind, count);
	if (h) {
		bputs(&head, " <span class=\"tnote\">(");
		inlines(&head, h);
		bputs(&head, ")</span>");
	}
	bputs(&head, "<span class=\"thead\">.</span> ");
	bool first = true;
	for (size_t i = 0; i < t->kids.n; i++) {
		Node *k = t->kids.v[i];
		if (streq(k->kind, "heading"))
			continue;
		if (first && streq(k->kind, "para")) {
			fputs("<p>", out);
			fputs(bstr(&head), out);
			write_inlines(k);
			fputs("</p>\n", out);
		} else {
			if (first)
				fprintf(out, "<p>%s</p>\n", bstr(&head));
			Node one = *t;
			Node *kids[1] = {k};
			one.kids.v = kids;
			one.kids.n = 1;
			blocks(&one, 0);
		}
		first = false;
	}
	if (first)
		fprintf(out, "<p>%s</p>\n", bstr(&head));
	fputs("</div>\n", out);
}

static void
proof(Node *t)
{
	Node *h = child(t, "heading");
	Node *last = NULL;

	for (size_t i = 0; i < t->kids.n; i++)
		if (!streq(t->kids.v[i]->kind, "heading"))
			last = t->kids.v[i];
	fputs("<div class=\"proof\">\n", out);
	Buf head = {0};
	bputs(&head, "<em class=\"phead\">");
	if (h) {
		inlines(&head, h);
		bputc(&head, '.');
	} else {
		bputs(&head, "Proof.");
	}
	bputs(&head, "</em> ");
	const char *qed = "<span class=\"qed\" role=\"img\" aria-label=\"End of proof\">∎</span>";
	bool first = true;
	for (size_t i = 0; i < t->kids.n; i++) {
		Node *k = t->kids.v[i];
		if (streq(k->kind, "heading"))
			continue;
		if (streq(k->kind, "para")) {
			fputs("<p>", out);
			if (first)
				fputs(bstr(&head), out);
			write_inlines(k);
			if (k == last)
				fputs(qed, out);
			fputs("</p>\n", out);
		} else {
			if (first)
				fprintf(out, "<p>%s</p>\n", bstr(&head));
			Node one = *t;
			Node *kids[1] = {k};
			one.kids.v = kids;
			one.kids.n = 1;
			blocks(&one, 0);
			if (k == last)
				fprintf(out, "<p class=\"qedline\">%s</p>\n", qed);
		}
		first = false;
	}
	if (first)
		fprintf(out, "<p>%s%s</p>\n", bstr(&head), qed);
	fputs("</div>\n", out);
}

static void
table(Node *t)
{
	Node *al = child(t, "align");
	char aligns[64];
	memset(aligns, 'l', sizeof aligns);
	if (al) {
		char *a = xstrdup(al->rest), *f;
		for (int c = 0; c < 64 && *(f = next_field(&a)); c++)
			aligns[c] = f[0];
	}
	fputs("<table>\n", out);
	bool in_head = false, in_body = false;
	for (size_t i = 0; i < t->kids.n; i++) {
		Node *r = t->kids.v[i];
		if (!streq(r->kind, "row"))
			continue;
		bool head = atoi(r->rest) != 0;
		if (head && !in_head) {
			fputs("<thead>\n", out);
			in_head = true;
		}
		if (!head && in_head) {
			fputs("</thead>\n", out);
			in_head = false;
		}
		if (!head && !in_body) {
			fputs("<tbody>\n", out);
			in_body = true;
		}
		fputs("<tr>", out);
		for (size_t c = 0; c < r->kids.n; c++) {
			char a = c < 64 ? aligns[c] : 'l';
			const char *cls = a == 'c' ? " class=\"c\"" : a == 'r' ? " class=\"r\"" : "";
			fprintf(out, head ? "<th scope=\"col\"%s>" : "<td%s>", cls);
			write_inlines(r->kids.v[c]);
			fputs(head ? "</th>" : "</td>", out);
		}
		fputs("</tr>\n", out);
	}
	if (in_head)
		fputs("</thead>\n", out);
	if (in_body)
		fputs("</tbody>\n", out);
	fputs("</table>\n", out);
}

static void
figure(Node *f)
{
	char *path = pct_unescape(field(f, 0)), *width = field(f, 1), *label = field(f, 2);
	Node *alt = child(f, "alt");
	Label *l = streq(label, "-") ? NULL : find_label(label);
	static int count;

	count++;
	fputs("<figure", out);
	if (l) {
		fputs(" id=\"", out);
		esc(out, l->name);
		fputc('"', out);
	}
	fputs("><img src=\"", out);
	esc(out, path);
	fputs("\" alt=\"", out);
	esc(out, alt ? alt->rest : "");
	fputc('"', out);
	if (!streq(width, "-") && strchr(width, '%'))
		fprintf(out, " style=\"width: %s\"", width);
	fprintf(out, "><figcaption><strong>Figure %d.</strong> ", count);
	for (size_t i = 0; i < f->kids.n; i++)
		if (streq(f->kids.v[i]->kind, "para"))
			write_inlines(f->kids.v[i]);
	fputs("</figcaption></figure>\n", out);
}

static void
contents(Node *doc)
{
	fputs("<nav class=\"contents\" aria-labelledby=\"tekx-contents\">\n"
	    "<h2 id=\"tekx-contents\">Contents</h2>\n<ol>\n", out);
	/* sections, nested as in the document */
	VEC(Node *) stack = {0};
	VPUSH(stack, doc);
	VEC(size_t) at = {0};
	VPUSH(at, 0);
	while (stack.n) {
		Node *n = stack.v[stack.n - 1];
		size_t *i = &at.v[at.n - 1];
		if (*i >= n->kids.n) {
			stack.n--;
			at.n--;
			if (stack.n)
				fputs("</ol></li>\n", out);
			continue;
		}
		Node *k = n->kids.v[(*i)++];
		if (!streq(k->kind, "section"))
			continue;
		Label *l = find_label(section_key(k));
		fputs("<li><a href=\"#", out);
		esc(out, section_key(k));
		fputs("\">", out);
		if (l && *l->number)
			fprintf(out, "<span class=\"num\">%s</span> ", l->number);
		write_inlines(child(k, "heading"));
		fputs("</a><ol>\n", out);
		VPUSH(stack, k);
		VPUSH(at, 0);
	}
	fputs("</ol>\n</nav>\n", out);
}

static Node *doc_root;

static void
blocks(Node *n, int depth)
{
	for (size_t i = 0; i < n->kids.n; i++) {
		Node *k = n->kids.v[i];
		const char *kd = k->kind;
		if (streq(kd, "para")) {
			fputs("<p>", out);
			write_inlines(k);
			fputs("</p>\n", out);
		} else if (streq(kd, "section")) {
			int level = atoi(field(k, 0));
			const char *key = section_key(k);
			Label *l = find_label(key);
			int h = MIN(level + 1, 6);
			fputs("<section id=\"", out);
			esc(out, key);
			fprintf(out, "\">\n<h%d>", h);
			if (l && *l->number)
				fprintf(out, "<span class=\"num\">%s</span> ", l->number);
			write_inlines(child(k, "heading"));
			fprintf(out, "</h%d>\n", h);
			blocks(k, depth + 1);
			fputs("</section>\n", out);
		} else if (streq(kd, "display")) {
			display(k);
		} else if (streq(kd, "theorem")) {
			theorem(k);
		} else if (streq(kd, "proof")) {
			proof(k);
		} else if (streq(kd, "list")) {
			bool ordered = atoi(field(k, 0)) != 0;
			int start = atoi(field(k, 1));
			if (ordered && start != 1)
				fprintf(out, "<ol start=\"%d\">\n", start);
			else
				fputs(ordered ? "<ol>\n" : "<ul>\n", out);
			for (size_t j = 0; j < k->kids.n; j++) {
				Node *it = k->kids.v[j];
				if (!streq(it->kind, "item"))
					continue;
				fputs("<li>", out);
				/* a single paragraph needs no <p> */
				if (it->kids.n == 1 && streq(it->kids.v[0]->kind, "para"))
					write_inlines(it->kids.v[0]);
				else
					blocks(it, depth);
				fputs("</li>\n", out);
			}
			fputs(ordered ? "</ol>\n" : "</ul>\n", out);
		} else if (streq(kd, "table")) {
			table(k);
		} else if (streq(kd, "code")) {
			fputs("<pre><code>", out);
			for (size_t j = 0; j < k->kids.n; j++) {
				esc(out, k->kids.v[j]->rest);
				fputc('\n', out);
			}
			fputs("</code></pre>\n", out);
		} else if (streq(kd, "quote")) {
			fputs("<blockquote>\n", out);
			blocks(k, depth);
			fputs("</blockquote>\n", out);
		} else if (streq(kd, "abstract")) {
			fputs("<section class=\"abstract\" aria-labelledby=\"tekx-abstract\">\n"
			    "<h2 id=\"tekx-abstract\">Abstract</h2>\n", out);
			blocks(k, depth);
			fputs("</section>\n", out);
		} else if (streq(kd, "let")) {
			Node *m = child(k, "mathml"), *prose = child(k, "prose");
			fputs("<p>Let ", out);
			if (m)
				fputs(m->rest, out);
			if (prose) {
				fputc(' ', out);
				write_inlines(prose);
			}
			char *pt = prose ? plain_text(prose) : "";
			size_t len = strlen(pt);
			if (len == 0 || !strchr(".!?", pt[len - 1]))
				fputc('.', out);
			fputs("</p>\n", out);
		} else if (streq(kd, "refs")) {
			fputs("<section class=\"references\" aria-labelledby=\"tekx-references\">\n"
			    "<h2 id=\"tekx-references\">References</h2>\n<ol>\n", out);
			for (size_t j = 0; j < k->kids.n; j++) {
				Node *e = k->kids.v[j];
				Label *l = find_label(e->rest);
				fputs("<li id=\"", out);
				esc(out, e->rest);
				fprintf(out, "\"><span class=\"lbl\">%s</span> ", l ? l->text : "[?]");
				write_inlines(e);
				fputs("</li>\n", out);
			}
			fputs("</ol>\n</section>\n", out);
		} else if (streq(kd, "contents")) {
			contents(doc_root);
		} else if (streq(kd, "figure")) {
			figure(k);
		}
	}
}

static const char style[] =
    ":root{color-scheme:light dark;--ink:#1b1f23;--paper:#fdfdfb;--muted:#5d6670;--rule:#d8dbd6;"
    "--link:#1d4f91}\n"
    "@media (prefers-color-scheme:dark){:root{--ink:#e6e7e3;--paper:#141719;--muted:#9aa3ab;"
    "--rule:#2c3339;--link:#8fb6ee}}\n"
    "body{margin:0;background:var(--paper);color:var(--ink);font:1.1rem/1.6 \"STIX Two Text\",\"STIX Two Math\","
    "Georgia,serif;padding:2rem max(1rem,env(safe-area-inset-left))}\n"
    "main,header{max-width:38rem;margin:0 auto}\n"
    "math{font-family:\"STIX Two Math\",math}\n"
    "a{color:var(--link)}\n"
    "header{text-align:center;margin-bottom:2.5rem}\nheader h1{font-size:1.9rem;line-height:1.2;margin:0 0 .4rem}\n"
    "header p{margin:.2rem 0}.subtitle{font-style:italic}.author{font-size:1.1rem}.date{color:var(--muted)}\n"
    "h2,h3,h4{line-height:1.25;margin:2rem 0 .6rem}.num{margin-right:.6em}\n"
    "p{margin:.5rem 0;text-align:justify;hyphens:auto}\n"
    ".abstract{margin:0 2rem 2rem;font-size:.95rem}.abstract h2{text-align:center;font-size:1rem;margin:0}\n"
    ".display{margin:1rem 0;overflow-x:auto}\n.equation{display:flex;align-items:center;justify-content:center;"
    "gap:1rem;position:relative;min-height:2em}\n.eqno{position:absolute;right:0}\n"
    ".theorem,.proof{margin:1rem 0}.theorem .thead{font-weight:700}.theorem.remark .thead{font-weight:400;"
    "font-style:italic}\n.theorem.plain p{font-style:italic}.theorem.plain .tnote,.theorem.plain .thead{font-style:normal}\n"
    ".qed{float:right}\n.qedline{text-align:right}\n"
    "table{border-collapse:collapse;margin:1rem auto;border-top:2px solid;border-bottom:2px solid}\n"
    "th,td{padding:.3rem .7rem;text-align:left}thead th{border-bottom:1px solid}.c{text-align:center}"
    ".r{text-align:right}\n"
    "pre{font-size:.9rem;overflow-x:auto;padding-left:1.5rem}\ncode{font-size:.92em}\n"
    "blockquote{margin:1rem 2rem}\nfigure{margin:1.5rem 0;text-align:center}figure img{max-width:100%;height:auto}\n"
    "figcaption{font-size:.95rem;margin-top:.5rem}\n"
    ".references ol{list-style:none;padding:0}.references li{padding-left:2.5rem;text-indent:-2.5rem;margin:.3rem 0}"
    ".lbl{display:inline-block;width:2.5rem;text-indent:0}\n"
    ".contents ol{list-style:none;padding-left:1.5rem}.contents>ol{padding-left:0}\n"
    ".endnotes{margin-top:3rem;border-top:1px solid var(--rule);font-size:.92rem}\n"
    "sup.fn a{text-decoration:none}\n";

int
html_main(FILE *in, FILE *o, const char *file)
{
	out = o;
	diag_file = file;
	Node *doc = tree_read(in);
	if (*doc->rest)
		diag_file = doc->rest;
	doc_root = doc;
	for (size_t i = 0; i < doc->kids.n; i++) {
		Node *k = doc->kids.v[i];
		if (!streq(k->kind, "meta"))
			continue;
		char *key = field(k, 0), *val = field_rest(k, 1);
		if (streq(key, "title"))
			title = val;
		else if (streq(key, "subtitle"))
			subtitle = val;
		else if (streq(key, "author")) {
			char *a = xstrdup(val), *s;
			while ((s = strstr(a, " and ")) != NULL) {
				*s = 0;
				VPUSH(authors, trim(a));
				a = s + 5;
			}
			VPUSH(authors, trim(a));
		} else if (streq(key, "date")) {
			if (streq(val, "today")) {
				static const char *month[] = {"January", "February", "March", "April", "May", "June",
				    "July", "August", "September", "October", "November", "December"};
				time_t t = (time_t)doc_time();
				struct tm tm;
				gmtime_r(&t, &tm);
				date = fmt("%d %s %d", tm.tm_mday, month[tm.tm_mon], tm.tm_year + 1900);
			} else {
				date = val;
			}
		} else if (streq(key, "language"))
			language = val;
	}
	number_document(doc);

	fputs("<!doctype html>\n<html lang=\"", out);
	esc(out, language);
	fputs("\">\n<head>\n<meta charset=\"utf-8\">\n"
	    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<title>", out);
	esc(out, title ? title : "Untitled");
	fputs("</title>\n", out);
	for (size_t i = 0; i < authors.n; i++) {
		fputs("<meta name=\"author\" content=\"", out);
		esc(out, authors.v[i]);
		fputs("\">\n", out);
	}
	fprintf(out, "<meta name=\"generator\" content=\"tekx %s\">\n<style>\n%s</style>\n</head>\n<body>\n",
	    TEKX_VERSION, style);
	if (title) {
		fputs("<header>\n<h1>", out);
		esc(out, title);
		fputs("</h1>\n", out);
		if (subtitle) {
			fputs("<p class=\"subtitle\">", out);
			esc(out, subtitle);
			fputs("</p>\n", out);
		}
		for (size_t i = 0; i < authors.n; i++) {
			fputs("<p class=\"author\">", out);
			esc(out, authors.v[i]);
			fputs("</p>\n", out);
		}
		if (date) {
			fputs("<p class=\"date\">", out);
			esc(out, date);
			fputs("</p>\n", out);
		}
		fputs("</header>\n", out);
	}
	fputs("<main>\n", out);
	blocks(doc, 0);
	if (nnotes) {
		fputs("<section class=\"endnotes\" role=\"doc-endnotes\" aria-label=\"Footnotes\">\n<ol>\n", out);
		fputs(bstr(&endnotes), out);
		fputs("</ol>\n</section>\n", out);
	}
	fputs("</main>\n</body>\n</html>\n", out);
	return nerrors ? 1 : 0;
}
