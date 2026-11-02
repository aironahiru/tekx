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
	} >test/out/golden.txt 2>&1
	if $update; then
		cp test/out/golden.txt "$name.out"
	elif ! cmp -s test/out/golden.txt "$name.out"; then
		fail "golden $f"
		diff "$name.out" test/out/golden.txt | head -20
	fi
done
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
		for profile in ua2 wt1a wt1r; do
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
	"$VERAPDF" --flavour ua2 --format text test/out/figures.pdf 2>/dev/null | grep -q '^PASS' ||
	    fail "figures: not ua2 conformant"
fi
echo "figures: done"

# 7. each stage is a filter: the pipeline by hand equals the driver
f=examples/tour.tekx
$T pp "$f" | $T parse | $T eqn | $T set | $T pdf >test/out/byhand.pdf &&
    $T -o test/out/driver.pdf "$f" && cmp -s test/out/byhand.pdf test/out/driver.pdf ||
    fail "the shell pipeline differs from the driver"
echo "pipeline: done"

[ $status -eq 0 ] && echo "all tests passed"
exit $status
