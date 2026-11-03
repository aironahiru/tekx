#!/bin/sh
# test/run.sh -- the test suite; `make test` runs it from the top directory.
#
#	sh test/run.sh		run every test
#	sh test/run.sh -u	update the expected outputs of golden and error tests
#
# Every program run goes through tekx-debug (AddressSanitizer, UBSan).
set -u
export SOURCE_DATE_EPOCH=1793616300 ASAN_OPTIONS=detect_leaks=0 LC_ALL=C
update=false
[ "${1:-}" = "-u" ] && update=true
T=./tekx-debug
# never test stale binaries: build first, and stop if the build fails
make -s tekx tekx-debug >/dev/null 2>test/build.err || { cat test/build.err; echo "FAIL: build"; exit 2; }
rm -f test/build.err
mkdir -p test/out
status=0
fail() { echo "FAIL: $*"; status=1; }

# 1. unit tests of the building blocks
objs=$(ls obj-debug/*.o | grep -v '/main\.o$')
if cc -std=c17 -g -fsanitize=address,undefined -D_POSIX_C_SOURCE=200809L -o test/out/unit test/unit.c $objs -lm &&
    test/out/unit; then
	echo "unit: ok"
else
	fail "unit tests"
fi

# 2. copy text reads back as the same formula
TEKX=$T sh test/roundtrip.sh || status=1

# 3. how formulas are read: tekx explain shows every grouping it chose
$T explain <test/explain.txt >test/out/explain.out 2>&1
if $update; then
	cp test/out/explain.out test/explain.out
elif ! cmp -s test/out/explain.out test/explain.out; then
	fail "tekx explain"
	diff test/explain.out test/out/explain.out | head -20
fi

# 3b. golden outputs: document trees and the meaning of every formula
for f in test/golden/*.tekx; do
	name=${f%.tekx}
	{
		echo "== tree"
		$T -P "$f"
		echo "== mathematics"
		$T -M "$f" | sed -n 's/^ *\(speech\|linear\|mathml\) /\1 /p'
		echo "== structure"
		$T -S "$f" | awk -f test/structure.awk
	} >test/out/golden.txt 2>&1
	if $update; then
		cp test/out/golden.txt "$name.out"
	elif ! cmp -s test/out/golden.txt "$name.out"; then
		fail "golden $f"
		diff "$name.out" test/out/golden.txt | head -20
	fi
done
# 3c. the tag tree a screen reader is given, for the document that uses
#     every feature: what veraPDF cannot judge, a person reviewed once
$T -S examples/tour.tekx | awk -f test/structure.awk >test/out/tour.structure 2>&1
if $update; then
	cp test/out/tour.structure test/tour.structure
elif ! cmp -s test/out/tour.structure test/tour.structure; then
	fail "structure of examples/tour.tekx"
	diff test/tour.structure test/out/tour.structure | head -20
fi
echo "golden: done"

# 4. diagnostics: errors are reported where they are, with a suggestion
for f in test/errors/*.tekx; do
	name=${f%.tekx}
	$T -o test/out/err.pdf "$f" 2>test/out/err.txt >/dev/null && fail "$f should not compile"
	[ -e test/out/err.pdf ] && fail "$f left an output file behind"
	if $update; then
		cp test/out/err.txt "$name.err"
	elif ! cmp -s test/out/err.txt "$name.err"; then
		fail "errors $f"
		diff "$name.err" test/out/err.txt | head -20
	fi
done
echo "errors: done"

# 5. examples: build under the sanitizers, reproducibly, identically to
#    the optimized build, and (when veraPDF is at hand) PDF/UA-2 conformant
for f in examples/*.tekx; do
	base=test/out/$(basename "${f%.tekx}")
	$T -o "$base.pdf" "$f" || { fail "build $f"; continue; }
	./tekx -o "$base.opt.pdf" "$f" || fail "optimized build $f"
	cmp -s "$base.pdf" "$base.opt.pdf" || fail "$f: debug and optimized builds differ"
	$T -o "$base.again.pdf" "$f" && cmp -s "$base.pdf" "$base.again.pdf" || fail "$f: not reproducible"
	if [ -n "${VERAPDF:-}" ]; then
		# PDF/A-4f when the document carries files (formulas' MathML), else PDF/A-4
		pdfa=4
		grep -q 'pdfaid:conformance>F' "$base.pdf" && pdfa=4f
		for profile in ua2 wt1a wt1r $pdfa; do
			"$VERAPDF" --flavour $profile --format text "$base.pdf" 2>/dev/null | grep -q '^PASS' ||
			    fail "$f: not $profile conformant"
		done
	fi
done
echo "examples: done"

# 6. every kind of image a figure accepts: PNG of each colour type and depth,
#    with and without alpha and row filters, and JPEG
$T -o test/out/figures.pdf test/figures.tekx || fail "figures"
if [ -n "${VERAPDF:-}" ]; then
	for profile in ua2 4f; do
		"$VERAPDF" --flavour $profile --format text test/out/figures.pdf 2>/dev/null | grep -q '^PASS' ||
		    fail "figures: not $profile conformant"
	done
	# a document without formulas carries no files: plain PDF/A-4
	printf 'title "Words"\n\nOnly prose, no formulas.\n' >test/out/words.tekx
	$T -o test/out/words.pdf test/out/words.tekx || fail "words"
	for profile in ua2 4; do
		"$VERAPDF" --flavour $profile --format text test/out/words.pdf 2>/dev/null | grep -q '^PASS' ||
		    fail "words: not $profile conformant"
	done
fi
# 600 formulas carry 1,200 files: the EmbeddedFiles name tree needs two leaves
{
	echo 'title "Many formulas"'
	i=1
	while [ $i -le 600 ]; do
		printf '\nFormula %d is $x_%d^2 + %d$.\n' $i $i $i
		i=$((i + 1))
	done
} >test/out/many.tekx
$T -o test/out/many.pdf test/out/many.tekx || fail "many formulas"
if [ -n "${VERAPDF:-}" ]; then
	"$VERAPDF" --flavour 4f --format text test/out/many.pdf 2>/dev/null | grep -q '^PASS' ||
	    fail "many formulas: not 4f conformant"
fi
echo "figures: done"

# 7. the web back end: every document makes HTML with MathML, and the
#    filters by hand give the same page as the driver
for f in examples/*.tekx test/golden/*.tekx test/figures.tekx; do
	$T -o test/out/page.html "$f" 2>/dev/null || { fail "html $f"; continue; }
	grep -q '</html>' test/out/page.html || fail "html $f: truncated"
done
$T pp examples/tour.tekx | $T parse | $T eqn | $T html >test/out/byhand.html &&
    $T -o test/out/driver.html examples/tour.tekx && cmp -s test/out/byhand.html test/out/driver.html ||
    fail "the HTML pipeline by hand differs from the driver"
echo "html: done"

# 8. each stage is a filter: the pipeline by hand equals the driver
f=examples/tour.tekx
$T pp "$f" | $T parse | $T eqn | $T set | $T pdf >test/out/byhand.pdf &&
    $T -o test/out/driver.pdf "$f" && cmp -s test/out/byhand.pdf test/out/driver.pdf ||
    fail "the shell pipeline differs from the driver"
echo "pipeline: done"

# 8b. tekx new: the paper it starts you with builds, with its bibliography
rm -rf test/out/new && mkdir -p test/out/new
(cd test/out/new && ../../../$T new paper >/dev/null && ../../../$T -o paper.pdf paper.tekx) ||
    fail "tekx new: the starting paper does not build"
echo "new: done"

# 9. packages: lock, verify, refuse a changed package, filters, git registries
TEKX=$T sh test/pkg.sh || status=1

# 10. copy and paste in the viewers people use (test/paste.sh): poppler
#    when pdftotext is installed, pdf.js with PDFJS=.../pdfjs-dist, PDFium
#    with PDFIUM=.../directory holding pypdfium2
if command -v pdftotext >/dev/null 2>&1 || [ -n "${PDFJS:-}" ] || [ -n "${PDFIUM:-}" ]; then
	if $update; then
		TEKX=$T sh test/paste.sh -u >/dev/null || status=1
	else
		TEKX=$T sh test/paste.sh || status=1
	fi
fi

# 11. LaTeX both ways: tekx import and the tex back end (test/latex.sh)
if $update; then
	TEKX=$T sh test/latex.sh -u >/dev/null || status=1
else
	TEKX=$T sh test/latex.sh || status=1
fi

# 12. one document through every back end: they agree on every number
TEKX=$T sh test/integration.sh || status=1

# 13. formula layout: unchanged, and (with LUALATEX set) measured against LuaTeX
if $update; then
	TEKX=$T sh test/luatex/run.sh -u >/dev/null || status=1
else
	TEKX=$T sh test/luatex/run.sh || status=1
fi

[ $status -eq 0 ] && echo "all tests passed"
exit $status
