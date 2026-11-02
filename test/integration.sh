#!/bin/sh
# test/integration.sh -- one document that uses everything, through every
# back end: the PDF, the web page and the LaTeX export must agree on every
# number (theorems, equations, figures, sections, appendices) and on the
# order of the references.  Needs pdftotext; with TEXBIN set, the LaTeX
# export is compiled and compared too; with VERAPDF set, the PDF validated.
set -u
T=${TEKX:-./tekx-debug}
case $T in /*) ;; *) T=$PWD/$T ;; esac
export ASAN_OPTIONS=detect_leaks=0 SOURCE_DATE_EPOCH=1793616300
command -v pdftotext >/dev/null 2>&1 || { echo "integration: skipped (no pdftotext)"; exit 0; }
W=$PWD/test/out/integration
rm -rf "$W" && mkdir -p "$W"
cp test/integration/everything.tekx test/integration/refs.bib test/integration/parabola.png "$W/"
cd "$W" || exit 2
fails=0
fail() { echo "FAIL: integration: $*"; fails=$((fails + 1)); }

"$T" -o tekx.pdf everything.tekx || fail "PDF"
"$T" -o page.html everything.tekx || fail "HTML"
"$T" -o paper.tex everything.tekx || fail "LaTeX export"

# every reference to a numbered thing, with its count
numbers() {
	grep -oE '(Theorem|Lemma|Lemmas|Postulate|Figure|Section|Appendix) [0-9A-Z]+(\.[0-9]+)?|\([0-9A-Z]\.[0-9]+\)' |
	    sort | uniq -c
}
pdftotext tekx.pdf - | tr '\n' ' ' | sed 's/  */ /g' | numbers >pdf.num
sed -e 's/<math[^>]*>.*<\/math>//g' -e 's/<[^>]*>//g' page.html | tr '\n' ' ' | sed 's/  */ /g' | numbers >html.num
[ -s pdf.num ] || fail "no numbers found in the PDF"
cmp -s pdf.num html.num || { fail "the PDF and the web page number things differently"; diff pdf.num html.num; }

# the reference list: the same entries in the same order
pdftotext tekx.pdf - | grep -E '^\[[0-9]+\]' | sed 's/^\[[0-9]*\] //' | cut -c1-24 >pdf.refs
if [ -n "${TEXBIN:-}" ]; then
	("$TEXBIN/pdflatex" -interaction=batchmode paper.tex && "$TEXBIN/bibtex" paper &&
	    "$TEXBIN/pdflatex" -interaction=batchmode paper.tex &&
	    "$TEXBIN/pdflatex" -interaction=batchmode paper.tex) >latex.log 2>&1 || fail "LaTeX does not compile the export"
	# the list after the last "References" (the first is the contents entry);
	# amsplain lowercases titles, so compare without case
	pdftotext paper.pdf - | awk '/^References$/ {n = NR} {line[NR] = $0} END {for (i = n + 1; i <= NR; i++) print line[i]}' |
	    grep -E '^[0-9]+\. ' | sed 's/^[0-9]*\. //' | cut -c1-24 | tr 'A-Z' 'a-z' >tex.refs
	tr 'A-Z' 'a-z' <pdf.refs >pdf.refs.lc && mv pdf.refs.lc pdf.refs
	cmp -s pdf.refs tex.refs || { fail "tekx and its LaTeX export order the references differently"; diff pdf.refs tex.refs; }
	pdftotext paper.pdf - | tr '\n' ' ' | sed 's/  */ /g' | numbers >tex.num
	cmp -s pdf.num tex.num || { fail "the PDF and the LaTeX export number things differently"; diff pdf.num tex.num; }
fi
if [ -n "${VERAPDF:-}" ]; then
	for profile in ua2 wt1a wt1r 4f; do
		"$VERAPDF" --flavour $profile --format text tekx.pdf 2>/dev/null | grep -q '^PASS' || fail "not $profile"
	done
fi
[ $fails -eq 0 ] && echo "integration: ok"
[ $fails -eq 0 ]
