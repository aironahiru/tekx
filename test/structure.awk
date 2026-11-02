# test/structure.awk -- the structure tree of a page description, as an
# indented outline, for golden tests.
#
#	tekx -S doc.tekx | awk -f test/structure.awk
#
# A validator checks that a tag tree is well formed; it cannot tell whether
# an equation number is labelled as part of its equation or a caption
# belongs to its figure.  This outline is what a person reviews once and
# the test suite then holds still.  MathML below <math> is left out (the
# mathematics goldens pin it), and so is a formula's alt (it is the speech).
$1 == "e" {
	n++
	id[n] = $2
	depth[$2] = ($3 == "-") ? 0 : depth[$3] + 1
	ns[$2] = $4
	tag[$2] = $5
	hide[$2] = ($3 != "-" && (hide[$3] || ns[$3] == "mathml"))
	next
}
$1 == "a" && ($3 == "alt" || $3 == "actual" || $3 == "lang" || $3 == "ident") {
	if (tag[$2] == "Formula" && $3 == "alt")
		next
	v = $0
	sub(/^a [0-9]+ [a-z]+ /, "", v)
	note[$2] = note[$2] " " $3 "=\"" v "\""
	next
}
$1 == "A" {
	note[$2] = note[$2] " " $4 "=" $6
	next
}
$1 == "l" {
	first = ($7 == "u") ? 9 : 11
	v = $first
	for (k = first + 1; k <= NF; k++)
		v = v " " $k
	links[$6] = links[$6] " link=\"" v "\""
	if ($7 == "u")
		links[$6] = links[$6] " url=" $8
	else
		target[$6] = $8
	next
}
END {
	for (i = 1; i <= n; i++) {
		e = id[i]
		if (hide[e])
			continue
		s = ""
		for (d = 0; d < depth[e]; d++)
			s = s "  "
		to = (e in target) ? " to=" tag[target[e]] : ""
		printf "%s%s:%s%s%s%s\n", s, ns[e], tag[e], note[e], links[e], to
	}
}
