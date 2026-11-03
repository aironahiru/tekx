#!/bin/sh
# test/luatex/run.sh -- formula layout, two ways.
#
# Always: every formula in formulas.txt is laid out and the glyph positions
# are compared with test/luatex/layout.out, so that layout never changes by
# accident (-u updates it).
#
# With LUALATEX set to a lualatex binary (unicode-math installed), the same
# formulas are also set by LuaTeX with the same font, and each glyph's
# position is compared: score.py prints how many agree within 2 em/1000.
# The formulas that differ, and why:
#   xra     amsmath draws the arrow from a minus and a head; tekx uses the
#           font's assembly.  Same width, same labels.
#   coleq   tekx sets := as one symbol, U+2254.
#   intlim  tekx centres limits on an integral as the OpenType MATH
#           specification says; LuaTeX widens the box by the italic correction.
#   inner   \left( is spaced as a plain parenthesis; TeX adds thin spaces.
#   spc3 op2 limT limD log2 sinx mrme
#           the reference sets text and operator names in Latin Modern,
#           tekx in STIX Two Text.
#   m5 sq5  assembly overlaps: under 30 em/1000 on a 6 em delimiter.
set -u
T=${TEKX:-./tekx-debug}
case $T in /*) ;; *) T=$PWD/$T ;; esac
export ASAN_OPTIONS=detect_leaks=0 SOURCE_DATE_EPOCH=1793616300
D=$PWD/test/luatex
W=$PWD/test/out/luatex
rm -rf "$W" && mkdir -p "$W"
status=0

# one document, one formula per paragraph; tekx -M prints the boxes
{
	printf 'notation tex\n\n'
	while IFS='	' read -r label style src; do
		if [ "$style" = D ]; then
			printf '%s:\n\n    %s\n\n' "$label" "$src"
		else
			printf '%s: $%s$\n\n' "$label" "$src"
		fi
	done <"$D/formulas.txt"
} >"$W/formulas.tekx"
"$T" -M "$W/formulas.tekx" 2>&1 | grep -E '^ *(math@|line@|box |g |r )|error' >"$W/layout.out"
if [ "${1:-}" = -u ]; then
	cp "$W/layout.out" "$D/layout.out"
elif ! cmp -s "$W/layout.out" "$D/layout.out"; then
	echo "FAIL: luatex: formula layout changed"
	diff "$D/layout.out" "$W/layout.out" | head -20
	status=1
fi

if [ -n "${LUALATEX:-}" ]; then
	cp "$D/walk.lua" "$W/"
	{
		printf '%s\n' '\documentclass[10pt]{article}' '\usepackage{amsmath}' '\usepackage{unicode-math}' \
		    '\setmathfont{STIXTwoMath-Regular.otf}' '\directlua{dofile("walk.lua")}' \
		    '\newcommand\T[2]{\setbox0\hbox{$#2$}\directlua{dumpbox("\luaescapestring{#1}")}}' \
		    '\newcommand\TD[2]{\setbox0\hbox{$\displaystyle #2$}\directlua{dumpbox("\luaescapestring{#1}")}}' \
		    '\begin{document}'
		while IFS='	' read -r label style src; do
			[ "$style" = D ] && printf '\\TD{%s}{%s}\n' "$label" "$src" || printf '\\T{%s}{%s}\n' "$label" "$src"
		done <"$D/formulas.txt"
		printf '%s\n' '\end{document}'
	} >"$W/c.tex"
	(cd "$W" && "$LUALATEX" -interaction=nonstopmode c.tex >/dev/null 2>&1)
	python3 "$D/score.py" "$D/formulas.txt" "$W/c.log" "$W/layout.out"
fi
[ $status -eq 0 ] && echo "luatex: ok"
exit $status
