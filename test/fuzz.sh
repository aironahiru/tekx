#!/bin/sh
# test/fuzz.sh -- mutation fuzzing under AddressSanitizer and UBSan.
#
#	sh test/fuzz.sh [cases] [jobs]	default: 500 cases, 8 at a time
#
# Each case mutates a document from examples/ and test/ (one case in five
# mutates a PNG image instead, and one in five a BibTeX database) and builds
# it with tekx-debug.  A case
# fails if the build is killed by a signal, runs out of time, or makes a
# sanitizer report.  Errors in the document are expected and fine: the
# point is that no input, however broken, harms the compiler.
set -u
cases=${1:-500}
jobs=${2:-8}
[ -x tools/mutate ] || cc -O2 -o tools/mutate tools/mutate.c || exit 2
[ -x tekx-debug ] || { echo "fuzz: build tekx-debug first (make debug)"; exit 2; }
mkdir -p test/out/fuzz
rm -f test/out/fuzz/fail-*
export ASAN_OPTIONS=detect_leaks=0:abort_on_error=0 UBSAN_OPTIONS=print_stacktrace=1 SOURCE_DATE_EPOCH=1793616300
corpus=$(ls examples/*.tekx test/golden/*.tekx test/figures.tekx test/errors/*.tekx)
ncorpus=$(echo "$corpus" | wc -l)

one() {
	i=$1
	dir=test/out/fuzz/$i
	mkdir -p "$dir"
	cp test/golden/journal.bib "$dir/journal.bib"
	if [ $((i % 5)) -eq 0 ]; then
		./tools/mutate "$i" binary <test/images/parabola.png >"$dir/x.png"
		printf 'figure "x.png" width 50%%\n  alt "A mutated image."\n  Caption.\n' >"$dir/case.tekx"
	elif [ $((i % 5)) -eq 1 ]; then
		# a mutated BibTeX database, cited in full
		cp test/images/parabola.png "$dir/x.png"
		./tools/mutate "$i" <test/golden/journal.bib >"$dir/journal.bib"
		cp test/golden/journal.tekx "$dir/case.tekx"
	elif [ $((i % 5)) -eq 2 ]; then
		# a mutated LaTeX paper through tekx import, then built
		cp test/images/parabola.png "$dir/x.png"
		cp test/latex/refs.bib test/latex/parabola.png "$dir/"
		tex=test/latex/paper.tex
		[ $((i % 2)) -eq 0 ] && tex=test/latex/odd.tex
		./tools/mutate "$i" <"$tex" >"$dir/case.tex"
		timeout 20 ./tekx-debug import -o "$dir/case.tekx" "$dir/case.tex" >/dev/null 2>"$dir/import.txt"
		[ $? -ge 124 ] && echo "timeout" >>"$dir/import.txt"
		[ -e "$dir/case.tekx" ] || cp "$dir/case.tex" "$dir/case.tekx"
	else
		src=$(echo "$corpus" | sed -n "$((i % ncorpus + 1))p")
		cp test/images/parabola.png "$dir/x.png"
		./tools/mutate "$i" <"$src" >"$dir/case.tekx"
	fi
	timeout 20 ./tekx-debug -o "$dir/out.pdf" "$dir/case.tekx" >/dev/null 2>"$dir/err.txt"
	rc=$?
	timeout 20 ./tekx-debug -o "$dir/out.html" "$dir/case.tekx" >/dev/null 2>>"$dir/err.txt"
	[ $? -ge 124 ] && rc=124
	timeout 20 ./tekx-debug -o "$dir/out.tex" "$dir/case.tekx" >/dev/null 2>>"$dir/err.txt"
	[ $? -ge 124 ] && rc=124
	[ -e "$dir/import.txt" ] && cat "$dir/import.txt" >>"$dir/err.txt"
	grep -q '^timeout$' "$dir/err.txt" && rc=124
	if [ $rc -ge 124 ] || grep -q 'Sanitizer\|runtime error' "$dir/err.txt"; then
		cp "$dir/case.tekx" "test/out/fuzz/fail-$i.tekx"
		cp "$dir/x.png" "test/out/fuzz/fail-$i.png"
		cp "$dir/err.txt" "test/out/fuzz/fail-$i.txt"
		echo "fuzz: case $i failed (status $rc): test/out/fuzz/fail-$i.*"
	fi
	rm -rf "$dir"
}

i=${FUZZ_SEED:-1}; last=$((i + cases - 1))
while [ $i -le "$last" ]; do
	n=0
	while [ $n -lt "$jobs" ] && [ $i -le "$last" ]; do
		one $i &
		i=$((i + 1))
		n=$((n + 1))
	done
	wait
done
fails=$(ls test/out/fuzz/fail-*.txt 2>/dev/null | wc -l)
echo "fuzz: $cases cases, $fails failures"
[ "$fails" -eq 0 ]
