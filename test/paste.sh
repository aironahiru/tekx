#!/bin/sh
# test/paste.sh -- what copy and paste gives in the PDF viewers people use.
#
#	sh test/paste.sh [-u]		-u updates the kept engine outputs
#
# Each engine that is at hand extracts the text of test PDFs:
#
#	poppler	pdftotext on the PATH		Evince, Okular, most tools
#	pdf.js	PDFJS=/path/to/pdfjs-dist	Firefox
#	PDFium	PDFIUM=/path/to/dir with pypdfium2	Chrome, Edge
#
# and three things are checked for each:
#
#  formulas  every formula in test/formulas.txt, set inline and displayed,
#            pastes back as itself: the pasted text reads back with the
#            same MathML as the original;
#  prose     test/paste/prose.tekx pastes as written: words whole across
#            hyphenated line breaks, ligatures as letters, real spaces;
#  kept      the text of examples/tour.tekx and test/golden/journal.tekx is
#            the same as test/paste/*.ENGINE.txt (-u updates them).
#
# A check that an engine cannot pass for reasons outside tekx is listed in
# test/paste/limits with the reason; it is reported, not failed.
set -u
update=false
[ "${1:-}" = "-u" ] && update=true
T=${TEKX:-./tekx-debug}
export SOURCE_DATE_EPOCH=1793616300 ASAN_OPTIONS=detect_leaks=0 LC_ALL=C.UTF-8
O=test/out/paste
mkdir -p "$O"
status=0

engines=""
command -v pdftotext >/dev/null 2>&1 && engines="$engines poppler"
[ -n "${PDFJS:-}" ] && command -v node >/dev/null 2>&1 && engines="$engines pdfjs"
[ -n "${PDFIUM:-}" ] && command -v python3 >/dev/null 2>&1 && engines="$engines pdfium"
if [ -z "$engines" ]; then
	echo "paste: no engine (install poppler-utils, or set PDFJS or PDFIUM)"
	exit 0
fi

# extract ENGINE PDF > TEXT: pages separated by form feeds
extract() {
	case $1 in
	poppler) pdftotext -enc UTF-8 "$2" - ;;
	pdfjs) node test/paste/pdfjs.mjs "$PDFJS" "$2" ;;
	pdfium) python3 -I test/paste/pdfium.py "$PDFIUM" "$2" ;;
	esac
}

# the text as one line: page numbers (the last line of each page, when it is
# only digits) dropped, line breaks as spaces, runs of blanks as one
flatten() {
	awk 'BEGIN { RS = "\f" } {
		n = split($0, line, "\n")
		while (n > 0 && line[n] ~ /^[ \t]*$/) n--
		if (n > 0 && line[n] ~ /^[ \t]*[0-9]+[ \t]*$/) n--
		for (i = 1; i <= n; i++) printf "%s ", line[i]
	}' | sed 's/[[:space:]][[:space:]]*/ /g'
}

limited() {	# limited ENGINE CHECK: the reason, if this is a known engine limit
	awk -v e="$1" -v c="$2" '$1 == e && $2 == c { $1 = ""; $2 = ""; sub(/^  /, ""); print; exit }' test/paste/limits
}

limited_formula() {	# limited_formula ENGINE N: true if formula N is a known limit
	awk -v e="$1" -v n="$2" '$1 == e && $2 == "formula" && $3 == n { found = 1 } END { exit !found }' test/paste/limits
}

result() {	# result ENGINE CHECK OK DETAIL
	why=$(limited "$1" "$2")
	if [ "$3" = ok ]; then
		printf '  %-8s %-9s ok    %s\n' "$1" "$2" "$4"
		[ -n "$why" ] && echo "           (listed in test/paste/limits but now passes: remove it)"
	elif [ -n "$why" ]; then
		printf '  %-8s %-9s limit %s: %s\n' "$1" "$2" "$4" "$why"
	else
		printf '  %-8s %-9s FAIL  %s\n' "$1" "$2" "$4"
		status=1
	fi
}

# --- the documents ---

n=$(wc -l <test/formulas.txt)
{
	echo 'title "Paste"'
	i=1
	while [ "$i" -le "$n" ]; do
		f=$(sed -n "${i}p" test/formulas.txt)
		printf '\nQQ%di $%s$ WW%di.\n\nQQ%dd\n\n    %s\n\nWW%dd\n' "$i" "$f" "$i" "$i" "$f" "$i"
		i=$((i + 1))
	done
} >"$O/formulas.tekx"
$T -o "$O/formulas.pdf" "$O/formulas.tekx" || { echo "paste: cannot build the formulas"; exit 1; }
$T -o "$O/prose.pdf" test/paste/prose.tekx || { echo "paste: cannot build the prose"; exit 1; }
$T -o "$O/tour.pdf" examples/tour.tekx || exit 1
$T -o "$O/journal.pdf" test/golden/journal.tekx || exit 1

# the original formulas' MathML, read as inline formulas
mathml() {	# formulas on stdin -> MathML per line, source and speech removed
	awk 'BEGIN { print "document" } { printf "  math@%d:1 %s\n", NR, $0 }' | $T eqn 2>/dev/null |
	    sed -n 's/^    mathml //p' | sed 's/ alttext="[^"]*"//; s/<annotation.*//'
}
mathml <test/formulas.txt >"$O/want.mathml"

echo "paste: engines:$engines"
for e in $engines; do
	extract "$e" "$O/formulas.pdf" >"$O/formulas.$e.txt" 2>"$O/err.txt" || { result "$e" formulas fail "extraction failed"; continue; }
	flat=$(flatten <"$O/formulas.$e.txt")
	for mode in i d; do
		: >"$O/got.$mode.$e.txt"
		i=1
		while [ "$i" -le "$n" ]; do
			printf '%s\n' "$flat" | sed -n "s/.*QQ$i$mode \(.*\) WW$i$mode.*/\1/p" | sed 's/^ *//; s/ *$//' >"$O/one.txt"
			got=$(cat "$O/one.txt")
			printf '%s\n' "${got:-(nothing)}" >>"$O/got.$mode.$e.txt"
			i=$((i + 1))
		done
		mathml <"$O/got.$mode.$e.txt" >"$O/got.$mode.$e.mathml"
		bad=0 lim=0
		i=1
		: >"$O/bad.$mode.$e.txt"
		while [ "$i" -le "$n" ]; do
			if [ "$(sed -n "${i}p" "$O/want.mathml")" != "$(sed -n "${i}p" "$O/got.$mode.$e.mathml")" ] ||
			    [ "$(sed -n "${i}p" "$O/got.$mode.$e.txt")" = "(nothing)" ]; then
				if limited_formula "$e" "$i"; then
					lim=$((lim + 1))
				else
					bad=$((bad + 1))
					printf '%s  =>  %s\n' "$(sed -n "${i}p" test/formulas.txt)" "$(sed -n "${i}p" "$O/got.$mode.$e.txt")" >>"$O/bad.$mode.$e.txt"
				fi
			elif limited_formula "$e" "$i"; then
				echo "           formula $i now pastes in $e: remove it from test/paste/limits"
			fi
			i=$((i + 1))
		done
		label=$([ $mode = i ] && echo inline || echo displayed)
		result "$e" "formulas-$mode" "$([ $bad -eq 0 ] && echo ok || echo fail)" \
		    "$((n - bad - lim))/$n $label formulas paste back as themselves$([ $lim -gt 0 ] && echo ", $lim known limits")$([ $bad -gt 0 ] && echo "; see $O/bad.$mode.$e.txt")"
	done

	# prose: each expected line must appear in the pasted text.  Where an
	# engine is known to leave hyphenated words split, they are joined
	# first, so that everything else is still checked.
	extract "$e" "$O/prose.pdf" | flatten >"$O/prose.$e.txt"
	if [ -n "$(limited "$e" prose)" ]; then
		sed 's/\([[:alpha:]]\)- \([[:lower:]]\)/\1\2/g' "$O/prose.$e.txt" >"$O/prose.$e.joined.txt"
		mv "$O/prose.$e.joined.txt" "$O/prose.$e.txt"
	fi
	miss=0
	: >"$O/prose-missing.$e.txt"
	while IFS= read -r want; do
		[ -z "$want" ] && continue
		if ! grep -qF -- "$want" "$O/prose.$e.txt"; then
			miss=$((miss + 1))
			printf '%s\n' "$want" >>"$O/prose-missing.$e.txt"
		fi
	done <test/paste/prose.expected
	total=$(grep -c . test/paste/prose.expected)
	if [ -n "$(limited "$e" prose)" ] && [ $miss -eq 0 ]; then
		printf '  %-8s %-9s limit %s\n' "$e" prose "$total/$total sentences paste as written once hyphenated words are rejoined: $(limited "$e" prose)"
	else
		result "$e" prose "$([ $miss -eq 0 ] && echo ok || echo fail)" \
		    "$((total - miss))/$total sentences paste as written$([ $miss -gt 0 ] && echo "; see $O/prose-missing.$e.txt")"
	fi

	# kept outputs
	kept=ok
	for d in tour journal; do
		# blanks normalized, so that small differences between versions of
		# an engine in how it pads lines do not count
		extract "$e" "$O/$d.pdf" | sed 's/[[:space:]][[:space:]]*/ /g; s/^ //; s/ $//' | grep -v '^$' >"$O/$d.$e.txt"
		if $update; then
			cp "$O/$d.$e.txt" "test/paste/$d.$e.txt"
		elif [ ! -f "test/paste/$d.$e.txt" ] || ! cmp -s "$O/$d.$e.txt" "test/paste/$d.$e.txt"; then
			kept=fail
			diff "test/paste/$d.$e.txt" "$O/$d.$e.txt" 2>&1 | head -6
		fi
	done
	result "$e" kept $kept "tour and journal text as kept in test/paste/"
done
[ $status -eq 0 ] && echo "paste: done" || echo "paste: failures"
exit $status
