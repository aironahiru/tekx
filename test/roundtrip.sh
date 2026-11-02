#!/bin/sh
# The copy text of a formula must read back as the same formula:
# linear(parse(linear(parse(s)))) = linear(parse(s)), with equal MathML.
TEKX=${TEKX:-./tekx}
tree() {	# formulas on stdin -> document tree
	awk 'BEGIN { print "document" } { printf "  math@%d:1 %s\n", NR, $0 }'
}
pass=0 fail=0
first=$(tree <test/formulas.txt | $TEKX eqn) || { echo "roundtrip: eqn failed"; exit 1; }
printf '%s\n' "$first" | sed -n 's/^    linear //p' >test/out/linear1.txt
second=$(tree <test/out/linear1.txt | $TEKX eqn) || { echo "roundtrip: eqn failed on copy text"; exit 1; }
printf '%s\n' "$second" | sed -n 's/^    linear //p' >test/out/linear2.txt
strip() { sed -n 's/^    mathml //p' | sed 's/ alttext="[^"]*"//; s/<annotation.*//'; }
printf '%s\n' "$first" | strip >test/out/mathml1.txt
printf '%s\n' "$second" | strip >test/out/mathml2.txt
n=$(wc -l <test/formulas.txt)
i=1
while [ "$i" -le "$n" ]; do
	l1=$(sed -n "${i}p" test/out/linear1.txt); l2=$(sed -n "${i}p" test/out/linear2.txt)
	m1=$(sed -n "${i}p" test/out/mathml1.txt); m2=$(sed -n "${i}p" test/out/mathml2.txt)
	if [ "$l1" = "$l2" ] && [ "$m1" = "$m2" ]; then
		pass=$((pass + 1))
	else
		fail=$((fail + 1))
		echo "roundtrip FAIL: $(sed -n "${i}p" test/formulas.txt)"
		echo "   copy text:  $l1"
		[ "$l1" = "$l2" ] || echo "   reads back: $l2"
		[ "$m1" = "$m2" ] || { echo "   mathml 1: $m1"; echo "   mathml 2: $m2"; }
	fi
	i=$((i + 1))
done
echo "roundtrip: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
