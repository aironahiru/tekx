/*
 * texout.c -- a third back end: the document as LaTeX, for submission.
 *
 * Journals and arXiv take LaTeX source.  This back end reads the same tree
 * as html.c (after eqn) and writes an amsart document that a journal's
 * production system will compile unchanged:
 *
 *	tekx -o paper.tex paper.tekx
 *
 * Formulas are written from their meaning (eqn_latex), not copied from the
 * tekx source, so a formula typed as `sum k^2 for k from 1 to n` arrives
 * as \sum_{k = 1}^{n} k^{2}.  References become \Cref and \eqref,
 * citations \cite, and a BibTeX bibliography stays a BibTeX bibliography.
 */
#include "eqn.h"
#include "number.h"

static FILE *out;
static Buf abstract;		/* amsart sets the abstract before \maketitle */
static bool by_section;

typedef VEC(char *) Names;

static bool
has(const Names *v, const char *s)
{
	for (size_t i = 0; i < v->n; i++)
		if (streq(v->v[i], s))
			return true;
	return false;
}

/* ---- text ---- */

static void
esc(Buf *b, const char *s)
{
	static const struct { const char *u, *tex; } punct[] = {
		{"“", "``"}, {"”", "''"}, {"‘", "`"}, {"’", "'"}, {"–", "--"}, {"—", "---"},
		{"…", "\\ldots{}"}, {"\u00a0", "~"}, {"§", "\\S{}"}, {"¶", "\\P{}"}, {"·", "\\textperiodcentered{}"},
	};

	while (*s) {
		bool done = false;
		for (size_t i = 0; i < NELEM(punct) && !done; i++)
			if (startswith(s, punct[i].u)) {
				bputs(b, punct[i].tex);
				s += strlen(punct[i].u);
				done = true;
			}
		if (done)
			continue;
		switch (*s) {
		case '\\': bputs(b, "\\textbackslash{}"); break;
		case '~': bputs(b, "\\textasciitilde{}"); break;
		case '^': bputs(b, "\\textasciicircum{}"); break;
		case '<': bputs(b, "\\textless{}"); break;
		case '>': bputs(b, "\\textgreater{}"); break;
		case '|': bputs(b, "\\textbar{}"); break;
		case '&': case '%': case '$': case '#': case '_': case '{': case '}':
			bputc(b, '\\');
			bputc(b, *s);
			break;
		default: bputc(b, *s);
		}
		s++;
	}
}

/* a URL inside \url or \href: only % and # need protecting */
static void
url_esc(Buf *b, const char *s)
{
	for (; *s; s++) {
		if (*s == '%' || *s == '#')
			bputc(b, '\\');
		bputc(b, *s);
	}
}

static char *
formula(const char *src, int line, int col, bool display, char **rows, int nrows)
{
	M *m = eqn_parse(src, line, col, display, rows, nrows);

	return eqn_latex(m, display);
}

static void inlines(Buf *b, Node *n);

static void
cite(Buf *b, Node *k)
{
	/* [@a; @b; @c, Thm. 2]: in the author's order, one \cite for each run
	 * of keys without notes and one \cite[note] for each key with one */
	Buf run = {0};
	bool first = true;

	for (size_t i = 0; i <= k->kids.n; i++) {
		Node *it = i < k->kids.n ? k->kids.v[i] : NULL;
		if (it && !streq(it->kind, "item"))
			continue;
		if (it && it->kids.n == 0) {
			bprintf(&run, "%s%s", run.n ? "," : "", it->rest);
			continue;
		}
		if (run.n) {
			bprintf(b, "%s\\cite{%s}", first ? "" : ", ", bstr(&run));
			breset(&run);
			first = false;
		}
		if (it) {
			bprintf(b, "%s\\cite[", first ? "" : ", ");
			inlines(b, it);
			bprintf(b, "]{%s}", it->rest);
			first = false;
		}
	}
}

static void
inline_node(Buf *b, Node *k)
{
	const char *kd = k->kind;

	if (streq(kd, "text")) {
		esc(b, k->rest);
	} else if (streq(kd, "emph")) {
		bputs(b, "\\emph{");
		inlines(b, k);
		bputc(b, '}');
	} else if (streq(kd, "strong")) {
		bputs(b, "\\textbf{");
		inlines(b, k);
		bputc(b, '}');
	} else if (streq(kd, "code")) {
		bputs(b, "\\texttt{");
		esc(b, k->rest);
		bputc(b, '}');
	} else if (streq(kd, "math")) {
		bprintf(b, "$%s$", formula(k->rest, k->line, k->col, false, NULL, 0));
	} else if (streq(kd, "link")) {
		bputs(b, "\\href{");
		url_esc(b, k->rest);
		bputs(b, "}{");
		inlines(b, k);
		bputc(b, '}');
	} else if (streq(kd, "url")) {
		bputs(b, "\\url{");
		url_esc(b, k->rest);
		bputc(b, '}');
	} else if (streq(kd, "ref") || streq(kd, "refnum")) {
		Label *l = find_label(k->rest);
		if (l == NULL) {
			error_at(k->line, k->col, "unknown label @%s", k->rest);
			return;
		}
		/* "(3)" for equations, as tekx sets them; "Theorem~2" for the rest,
		 * the name written out as authors write it, so that nothing beyond
		 * amsmath and hyperref is needed */
		size_t tn = strlen(l->text), nn = strlen(l->number);
		if (streq(kd, "refnum")) {
			bprintf(b, "\\ref{%s}", l->name);
		} else if (l->text[0] == '(') {
			bprintf(b, "\\eqref{%s}", l->name);
		} else if (nn && tn > nn + 1 && streq(l->text + tn - nn, l->number) && l->text[tn - nn - 1] == ' ') {
			Buf name = {0};
			esc(&name, xstrndup(l->text, tn - nn - 1));
			bprintf(b, "%s~\\ref{%s}", bstr(&name), l->name);
		} else {
			bprintf(b, "\\hyperref[%s]{", l->name);
			esc(b, l->text);
			bputc(b, '}');
		}
	} else if (streq(kd, "cite")) {
		cite(b, k);
	} else if (streq(kd, "note")) {
		bputs(b, "\\footnote{");
		inlines(b, k);
		bputc(b, '}');
	}
}

static void
inlines(Buf *b, Node *n)
{
	for (size_t i = 0; i < n->kids.n; i++)
		inline_node(b, n->kids.v[i]);
}

static char *
inline_str(Node *n)
{
	Buf b = {0};

	if (n)
		inlines(&b, n);
	return bstr(&b);
}

/* ---- blocks ---- */

static void blocks(Buf *o, Node *n);

/* a display: one numbered or unnumbered equation, or an aligned group */
static void
display(Buf *o, Node *d)
{
	typedef struct { char *full, *left, *right, *label; bool split; } Line;
	VEC(Line) v = {0};
	bool any_label = false, any_split = false;

	for (size_t i = 0; i < d->kids.n; i++) {
		Node *ln = d->kids.v[i];
		if (!streq(ln->kind, "line"))
			continue;
		char *label = field(ln, 0), *src = field_rest(ln, 1);
		VEC(char *) rows = {0};
		for (size_t k = 0; k < ln->kids.n; k++)
			if (streq(ln->kids.v[k]->kind, "cont"))
				VPUSH(rows, ln->kids.v[k]->rest);
		M *m = eqn_parse(src, ln->line, ln->col, true, rows.v, (int)rows.n);
		Line l = {eqn_latex(m, true), NULL, NULL, streq(label, "-") ? NULL : label, false};
		l.split = eqn_latex_split(m, true, &l.left, &l.right);
		any_label |= l.label != NULL;
		any_split |= l.split;
		VPUSH(v, l);
	}
	if (v.n == 0)
		return;
	if (v.n == 1) {
		if (v.v[0].label)
			bprintf(o, "\\begin{equation}\\label{%s}\n  %s\n\\end{equation}\n", v.v[0].label, v.v[0].full);
		else
			bprintf(o, "\\[\n  %s\n\\]\n", v.v[0].full);
		return;
	}
	const char *env = any_split ? "align" : "gather";
	bprintf(o, "\\begin{%s%s}\n", env, any_label ? "" : "*");
	for (size_t i = 0; i < v.n; i++) {
		Line *l = &v.v[i];
		if (any_split)
			bprintf(o, "  %s &%s%s", l->split ? l->left : "", l->split ? " " : " \\quad ",
			    l->split ? l->right : l->full);
		else
			bprintf(o, "  %s", l->full);
		if (any_label)
			bprintf(o, l->label ? " \\label{%s}" : " \\notag", l->label ? l->label : "");
		bputs(o, i + 1 < v.n ? " \\\\\n" : "\n");
	}
	bprintf(o, "\\end{%s%s}\n", env, any_label ? "" : "*");
}

static void
theorem(Buf *o, Node *t)
{
	char *kind = field(t, 0), *label = field(t, 1);
	Node *h = child(t, "heading");

	bprintf(o, "\\begin{%s}", kind);
	if (h)
		bprintf(o, "[%s]", inline_str(h));
	if (!streq(label, "-"))
		bprintf(o, "\\label{%s}", label);
	bputc(o, '\n');
	blocks(o, t);
	bprintf(o, "\\end{%s}\n\n", kind);
}

static void
list(Buf *o, Node *k)
{
	bool ordered = atoi(field(k, 0)) != 0;
	int start = atoi(field(k, 1));

	bputs(o, ordered ? "\\begin{enumerate}\n" : "\\begin{itemize}\n");
	if (ordered && start > 1)
		bprintf(o, "\\setcounter{enumi}{%d}\n", start - 1);
	for (size_t j = 0; j < k->kids.n; j++) {
		Node *it = k->kids.v[j];
		if (!streq(it->kind, "item"))
			continue;
		bputs(o, "\\item ");
		if (it->kids.n == 1 && streq(it->kids.v[0]->kind, "para")) {
			bprintf(o, "%s\n", inline_str(it->kids.v[0]));
		} else {
			bputc(o, '\n');
			blocks(o, it);
		}
	}
	bputs(o, ordered ? "\\end{enumerate}\n\n" : "\\end{itemize}\n\n");
}

static void
table(Buf *o, Node *t)
{
	Node *al = child(t, "align");
	size_t cols = 0;

	for (size_t r = 0; r < t->kids.n; r++)
		if (streq(t->kids.v[r]->kind, "row"))
			cols = MAX(cols, t->kids.v[r]->kids.n);
	Buf spec = {0};
	for (size_t c = 0; c < cols; c++) {
		char *a = al ? field(al, (int)c) : "";
		bputc(&spec, *a ? *a : 'l');
	}
	bprintf(o, "\\begin{center}\n\\begin{tabular}{%s}\n\\hline\n", bstr(&spec));
	bool header_done = false;
	for (size_t r = 0; r < t->kids.n; r++) {
		Node *row = t->kids.v[r];
		if (!streq(row->kind, "row"))
			continue;
		bool head = atoi(row->rest) != 0;
		if (!head && !header_done && r > 0) {
			bputs(o, "\\hline\n");
		}
		if (!head)
			header_done = true;
		for (size_t c = 0; c < row->kids.n; c++) {
			if (c)
				bputs(o, " & ");
			char *cell = inline_str(row->kids.v[c]);
			bprintf(o, head ? "\\textbf{%s}" : "%s", cell);
		}
		bputs(o, " \\\\\n");
	}
	bputs(o, "\\hline\n\\end{tabular}\n\\end{center}\n\n");
}

static bool any_figure;

static void
figure(Buf *o, Node *f)
{
	any_figure = true;
	char *path = pct_unescape(field(f, 0)), *width = field(f, 1), *label = field(f, 2);
	Node *alt = child(f, "alt");
	Buf opt = {0};

	if (!streq(width, "-")) {
		size_t n = strlen(width);
		if (n && width[n - 1] == '%')
			bprintf(&opt, "width=%.3g\\linewidth", atof(width) / 100);
		else
			bprintf(&opt, "width=%s", width);
	}
	if (alt) {
		bprintf(&opt, "%salt={", opt.n ? "," : "");
		esc(&opt, alt->rest);
		bputc(&opt, '}');
	}
	bprintf(o, "\\begin{figure}[htbp]\n\\centering\n\\includegraphics[%s]{%s}\n\\caption{", bstr(&opt), path);
	for (size_t i = 0; i < f->kids.n; i++)
		if (streq(f->kids.v[i]->kind, "para"))
			bputs(o, inline_str(f->kids.v[i]));
	bputc(o, '}');
	if (!streq(label, "-"))
		bprintf(o, "\\label{%s}", label);
	bputs(o, "\n\\end{figure}\n\n");
}

static Names bibfiles;

static const char *cite_style = "numeric";	/* citations numeric | ordered | alphabetic */

static void
references(Buf *o, Node *r)
{
	bool manual = false;

	for (size_t i = 0; i < r->kids.n; i++)
		manual |= r->kids.v[i]->line > 0;
	if (bibfiles.n && !manual) {
		/* the AMS styles, numbered by author or alphabetic; unsrt for citation order */
		bprintf(o, "\\bibliographystyle{%s}\n\\bibliography{", streq(cite_style, "alphabetic") ? "amsalpha" :
		    streq(cite_style, "ordered") ? "unsrt" : "amsplain");
		for (size_t i = 0; i < bibfiles.n; i++) {
			char *f = xstrdup(bibfiles.v[i]);
			size_t n = strlen(f);
			if (n > 4 && streq(f + n - 4, ".bib"))
				f[n - 4] = 0;
			bprintf(o, "%s%s", i ? "," : "", f);
		}
		bputs(o, "}\n\n");
		return;
	}
	bputs(o, "\\begin{thebibliography}{99}\n");
	for (size_t i = 0; i < r->kids.n; i++) {
		Node *e = r->kids.v[i];
		bprintf(o, "\\bibitem{%s} %s\n", e->rest, inline_str(e));
	}
	bputs(o, "\\end{thebibliography}\n\n");
}

static void
blocks(Buf *o, Node *n)
{
	for (size_t i = 0; i < n->kids.n; i++) {
		Node *k = n->kids.v[i];
		const char *kd = k->kind;
		if (streq(kd, "para")) {
			bprintf(o, "%s\n\n", inline_str(k));
		} else if (streq(kd, "section")) {
			static const char *cmd[] = {"section", "section", "subsection", "subsubsection"};
			int level = atoi(field(k, 0));
			bool numbered = atoi(field(k, 1)) != 0;
			char *label = field(k, 2);
			bprintf(o, "\\%s%s{%s}", cmd[level < 1 || level > 3 ? 1 : level], numbered ? "" : "*",
			    inline_str(child(k, "heading")));
			if (!streq(label, "-"))
				bprintf(o, "\\label{%s}", label);
			bputs(o, "\n\n");
			blocks(o, k);
		} else if (streq(kd, "appendix")) {
			bputs(o, "\\appendix\n\n");
		} else if (streq(kd, "display")) {
			display(o, k);
			bputc(o, '\n');
		} else if (streq(kd, "theorem")) {
			theorem(o, k);
		} else if (streq(kd, "proof")) {
			Node *h = child(k, "heading");
			bputs(o, "\\begin{proof}");
			if (h)
				bprintf(o, "[%s]", inline_str(h));
			bputc(o, '\n');
			blocks(o, k);
			bputs(o, "\\end{proof}\n\n");
		} else if (streq(kd, "quote")) {
			bputs(o, "\\begin{quote}\n");
			blocks(o, k);
			bputs(o, "\\end{quote}\n\n");
		} else if (streq(kd, "abstract") && n->parent == NULL) {
			blocks(&abstract, k);
		} else if (streq(kd, "abstract")) {
			bputs(o, "\\begin{abstract}\n");
			blocks(o, k);
			bputs(o, "\\end{abstract}\n\n");
		} else if (streq(kd, "list")) {
			list(o, k);
		} else if (streq(kd, "table")) {
			table(o, k);
		} else if (streq(kd, "code")) {
			bputs(o, "\\begin{verbatim}\n");
			for (size_t j = 0; j < k->kids.n; j++)
				bprintf(o, "%s\n", k->kids.v[j]->rest);
			bputs(o, "\\end{verbatim}\n\n");
		} else if (streq(kd, "let")) {
			Node *prose = child(k, "prose");
			char *pt = prose ? plain_text(prose) : "";
			size_t len = strlen(pt);
			bprintf(o, "Let $%s$", formula(k->rest, k->line, k->col, false, NULL, 0));
			if (prose)
				bprintf(o, " %s", inline_str(prose));
			bputs(o, len == 0 || !strchr(".!?", pt[len - 1]) ? ".\n\n" : "\n\n");
		} else if (streq(kd, "figure")) {
			figure(o, k);
		} else if (streq(kd, "refs")) {
			references(o, k);
		} else if (streq(kd, "contents")) {
			bputs(o, "\\tableofcontents\n\n");
		} else if (streq(kd, "newpage")) {
			bputs(o, "\\newpage\n\n");
		}
	}
}

/* ---- the preamble ---- */

static void
theorem_kinds(Node *n, Names *used)
{
	for (size_t i = 0; i < n->kids.n; i++) {
		Node *k = n->kids.v[i];
		if (streq(k->kind, "theorem") && !has(used, field(k, 0)))
			VPUSH(*used, xstrdup(field(k, 0)));
		theorem_kinds(k, used);
	}
}

/* tekx numbers every theorem-like block with one counter: so does this;
 * theorem* is amsthm's unnumbered \newtheorem* */
static void
theorem_decls(Buf *o, Node *doc)
{
	Names used = {0};

	theorem_kinds(doc, &used);
	if (used.n == 0)
		return;
	static const char *style[] = {"plain", "definition", "remark"};
	const char *base = NULL;	/* the first numbered kind declared carries the counter */
	for (int s = 0; s < 3; s++) {
		bool any = false;
		for (size_t i = 0; i < used.n; i++) {
			size_t kl = strlen(used.v[i]);
			bool starred = kl > 1 && used.v[i][kl - 1] == '*';
			Env *e = find_env(starred ? xstrndup(used.v[i], kl - 1) : used.v[i]);
			int st = e ? e->style : 0;
			if (st != s)
				continue;
			if (!any)
				bprintf(o, "\\theoremstyle{%s}\n", style[s]);
			any = true;
			const char *title = e ? e->title : used.v[i];
			if (starred) {
				bprintf(o, "\\newtheorem*{%s}{%s}\n", used.v[i], title);
				continue;
			}
			if (base == NULL) {
				bprintf(o, "\\newtheorem{%s}{%s}%s\n", used.v[i], title, by_section ? "[section]" : "");
				base = used.v[i];
			} else {
				bprintf(o, "\\newtheorem{%s}[%s]{%s}\n", used.v[i], base, title);
			}
		}
	}
}

int
tex_main(FILE *in, FILE *o, const char *file)
{
	out = o;
	diag_file = file;
	Node *doc = tree_read(in);
	if (*doc->rest)
		diag_file = doc->rest;
	eqn_reset_declarations();
	eqn_notation_from(doc);

	char *title = NULL, *subtitle = NULL, *date = NULL, *msc = NULL, *keywords = NULL, *running = NULL;
	bool lineno = false;
	const char *paper = "a4paper", *size = NULL;
	Names ops = {0};
	Buf authors = {0};
	for (size_t i = 0; i < doc->kids.n; i++) {
		Node *k = doc->kids.v[i];
		if (streq(k->kind, "define")) {
			eqn_define(k->rest, k->line, k->col);
			if (startswith(k->rest, "operator ") || startswith(k->rest, "operators ")) {
				char *t = xstrdup(strchr(k->rest, ' ') + 1);
				for (char *w = strtok(t, ", "); w; w = strtok(NULL, ", "))
					if (!has(&ops, w))
						VPUSH(ops, w);
			}
			continue;
		}
		if (streq(k->kind, "bibfile")) {
			VPUSH(bibfiles, k->rest);
			continue;
		}
		if (!streq(k->kind, "meta"))
			continue;
		char *key = field(k, 0), *val = field_rest(k, 1);
		if (streq(key, "title"))
			title = val;
		else if (streq(key, "subtitle"))
			subtitle = val;
		else if (streq(key, "date"))
			date = val;
		else if (streq(key, "msc"))
			msc = val;
		else if (streq(key, "keywords"))
			keywords = val;
		else if (streq(key, "running"))
			running = val;
		else if (streq(key, "linenumbers"))
			lineno = streq(val, "on");
		else if (streq(key, "citations"))
			cite_style = val;
		else if (streq(key, "numbering"))
			by_section = streq(val, "by section");
		else if (streq(key, "paper"))
			paper = streq(val, "letter") ? "letterpaper" : streq(val, "a5") ? "a5paper" : "a4paper";
		else if (streq(key, "size"))
			size = streq(val, "10") || streq(val, "11") || streq(val, "12") ? val : NULL;
		else if (streq(key, "author")) {
			/* "A and B", or one author with details */
			char *a = xstrdup(val), *s;
			while (k->kids.n == 0 && (s = strstr(a, " and ")) != NULL) {
				*s = 0;
				bputs(&authors, "\\author{");
				esc(&authors, trim(a));
				bputs(&authors, "}\n");
				a = s + 5;
			}
			bputs(&authors, "\\author{");
			esc(&authors, trim(a));
			bputs(&authors, "}\n");
			Buf addr = {0};
			for (size_t j = 0; j < k->kids.n; j++) {
				Node *d = k->kids.v[j];
				if (streq(d->kind, "affiliation")) {
					if (addr.n)
						bputs(&addr, "; ");
					esc(&addr, d->rest);
				} else if (streq(d->kind, "email")) {
					bputs(&authors, "\\email{");
					url_esc(&authors, d->rest);
					bputs(&authors, "}\n");
				} else if (streq(d->kind, "url") || streq(d->kind, "orcid")) {
					bputs(&authors, "\\urladdr{");
					if (streq(d->kind, "orcid"))
						bputs(&authors, "https://orcid.org/");
					url_esc(&authors, d->rest);
					bputs(&authors, "}\n");
				}
			}
			if (addr.n)
				bprintf(&authors, "\\address{%s}\n", bstr(&addr));
		}
	}
	number_document(doc);
	Buf body = {0};
	blocks(&body, doc);
	if (nerrors)
		return 1;

	fprintf(out, "%% Written by tekx %s from %s.\n", TEKX_VERSION, *doc->rest ? doc->rest : file);
	fprintf(out, "\\documentclass[%s%s%s]{amsart}\n", paper, size ? "," : "", size ? fmt("%spt", size) : "");
	fputs("\\usepackage[T1]{fontenc}\n\\usepackage{amssymb}\n\\usepackage{mathrsfs}\n"
	    "\\usepackage{graphicx}\n\\usepackage[hidelinks]{hyperref}\n", out);
	if (lineno)	/* referee line numbers */
		fputs("\\usepackage{lineno}\n\\linenumbers\n", out);
	fputc('\n', out);
	if (any_figure)	/* alt text for figures; older graphicx ignores it rather than failing */
		fputs("\\makeatletter\n\\@ifundefined{KV@Gin@alt}{\\define@key{Gin}{alt}{}}{}\n\\makeatother\n\n", out);
	Buf decl = {0};
	theorem_decls(&decl, doc);
	if (decl.n)
		fprintf(out, "%s\n", bstr(&decl));
	if (by_section)
		fputs("\\numberwithin{equation}{section}\n", out);
	for (size_t i = 0; i < ops.n; i++)
		fprintf(out, "\\DeclareMathOperator{\\%s}{%s}\n", ops.v[i], ops.v[i]);
	if (by_section || ops.n)
		fputc('\n', out);
	if (title) {
		Buf t = {0};
		esc(&t, title);
		if (subtitle) {
			bputs(&t, "\\\\ {\\normalsize ");
			esc(&t, subtitle);
			bputc(&t, '}');
		}
		if (running) {	/* amsart sets the short title in the running heads */
			Buf r = {0};
			esc(&r, running);
			fprintf(out, "\\title[%s]{%s}\n", bstr(&r), bstr(&t));
		} else {
			fprintf(out, "\\title{%s}\n", bstr(&t));
		}
	}
	fputs(bstr(&authors), out);
	if (date) {
		if (streq(date, "today")) {
			fputs("\\date{\\today}\n", out);
		} else {
			Buf d = {0};
			esc(&d, date);
			fprintf(out, "\\date{%s}\n", bstr(&d));
		}
	}
	if (msc) {
		Buf m = {0};
		esc(&m, msc);
		fprintf(out, "\\subjclass[2020]{%s}\n", bstr(&m));
	}
	if (keywords) {
		Buf kw = {0};
		esc(&kw, keywords);
		fprintf(out, "\\keywords{%s}\n", bstr(&kw));
	}
	fputs("\n\\begin{document}\n\n", out);
	if (abstract.n)
		fprintf(out, "\\begin{abstract}\n%s\\end{abstract}\n\n", bstr(&abstract));
	if (title)
		fputs("\\maketitle\n\n", out);
	fputs(bstr(&body), out);
	fputs("\\end{document}\n", out);
	return 0;
}
