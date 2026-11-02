#!/bin/sh
# test/latex.sh -- LaTeX both ways: tekx import, and the tex back end.
#
#	TEKX=./tekx-debug sh test/latex.sh [-u]
#
# -u updates the expected outputs.  With TEXBIN set to a TeX Live bin
# directory, every exported document is also compiled with pdflatex and
# bibtex, and must compile without errors or undefined references.
set -u
T=${TEKX:-./tekx-debug}
update=false
[ "${1:-}" = "-u" ] && update=true
status=0
fail() { echo "FAIL: $*"; status=1; }
out=test/out/latex
rm -rf "$out"
mkdir -p "$out"
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1793616300} ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0}

check() {	# expected actual
	if $update; then
		cp "$2" "$1"
	elif ! cmp -s "$1" "$2"; then
		fail "$2 differs from $1"
		diff "$1" "$2" | head -20
	fi
}

# 1. import: each paper gives the expected tekx and the expected warnings
for f in test/latex/*.tex; do
	name=$(basename "${f%.tex}")
	$T import -o "$out/$name.tekx" "$f" 2>"$out/$name.warnings" || fail "import $f"
	check "test/latex/$name.imported" "$out/$name.tekx"
	check "test/latex/$name.warnings" "$out/$name.warnings"
done

# 2. the imported paper builds, beside its bibliography and figure
cp test/latex/refs.bib test/latex/parabola.png "$out/"
$T -o "$out/paper.pdf" "$out/paper.tekx" || fail "the imported paper does not build"
if [ -n "${VERAPDF:-}" ] && [ -e "$out/paper.pdf" ]; then
	for profile in ua2 4f; do
		"$VERAPDF" --flavour $profile --format text "$out/paper.pdf" 2>/dev/null | grep -q '^PASS' ||
		    fail "the imported paper is not $profile conformant"
	done
fi

# 3. export: amsart for journals, from a tekx paper and from the imported one
$T -o "$out/journal.tex" test/golden/journal.tekx || fail "export test/golden/journal.tekx"
check test/latex/journal.exported "$out/journal.tex"
$T -o "$out/paper-out.tex" "$out/paper.tekx" || fail "export of the imported paper"
check test/latex/paper.exported "$out/paper-out.tex"
# amsalpha citations, an unnumbered theorem, running heads and line numbers
$T -o "$out/alpha.tex" test/golden/alpha.tekx || fail "export test/golden/alpha.tekx"
check test/latex/alpha.exported "$out/alpha.tex"

# 4. the tex stage is a filter like the others
$T pp test/golden/journal.tekx | $T parse | $T eqn | $T tex >"$out/byhand.tex" &&
    cmp -s "$out/byhand.tex" "$out/journal.tex" || fail "the tex pipeline by hand differs from the driver"

# 5. round trip: what tekx exports, tekx imports, and it builds
cp test/golden/journal.bib "$out/"
$T import -o "$out/roundtrip.tekx" "$out/journal.tex" 2>"$out/roundtrip.warnings" || fail "import of the export"
$T -o "$out/roundtrip.pdf" "$out/roundtrip.tekx" || fail "the round trip does not build"
[ -s "$out/roundtrip.warnings" ] && { fail "importing tekx's own export warns:"; cat "$out/roundtrip.warnings"; }

# 6. with TeX Live at hand, the exports compile
if [ -n "${TEXBIN:-}" ]; then
	for doc in journal paper-out alpha; do
		(
			cd "$out" || exit 1
			"$TEXBIN/pdflatex" -interaction=nonstopmode -halt-on-error "$doc.tex" >"$doc.log1" 2>&1 &&
			    "$TEXBIN/bibtex" "$doc" >"$doc.blg1" 2>&1 &&
			    "$TEXBIN/pdflatex" -interaction=nonstopmode -halt-on-error "$doc.tex" >"$doc.log2" 2>&1 &&
			    "$TEXBIN/pdflatex" -interaction=nonstopmode -halt-on-error "$doc.tex" >"$doc.log3" 2>&1 &&
			    ! grep -q 'undefined' "$doc.log3"
		) || fail "LaTeX does not compile $out/$doc.tex cleanly"
	done
fi

echo "latex: done"
exit $status
