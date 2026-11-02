#!/bin/sh
# test/pkg.sh -- the package manager, end to end, in a scratch directory:
# a registry of in-house packages (one with a dependency, one with a
# filter that implements a custom block), a project that locks them, and
# what must happen when a package changes underneath a document.
set -u
T=${TEKX:-$PWD/tekx-debug}
case $T in /*) ;; *) T=$PWD/$T ;; esac
W=$PWD/test/out/pkg
rm -rf "$W"
mkdir -p "$W/registry" "$W/paper"
# sanitizer reports go to files, so that none hides behind a redirection
export SOURCE_DATE_EPOCH=1793616300 ASAN_OPTIONS="detect_leaks=0:log_path=$W/san" \
    UBSAN_OPTIONS="print_stacktrace=1:log_path=$W/san"
fails=0
fail() { echo "FAIL: pkg: $*"; fails=$((fails + 1)); }
expect() {	# expect PATTERN FILE: the file must contain the pattern
	grep -q -- "$1" "$2" || { fail "expected '$1' in $2"; sed 's/^/    /' "$2" | head -5; }
}

# --- the registry: a directory of packages ---
R=$W/registry
mkdir -p "$R/house-base" "$R/house-notation" "$R/callouts/bin"
printf 'package "house-base"\nversion "1.0.2"\nsummary "Shared operators"\nprovides "base.tekx"\n' \
    >"$R/house-base/package.tekx"
printf 'define operator Var, Cov\n' >"$R/house-base/base.tekx"
printf 'package "house-notation"\nversion "1.3.0"\nsummary "House notation"\nrequires "tekx 0.1"\ndepends "house-base 1.0"\n' \
    >"$R/house-notation/package.tekx"
cat >"$R/house-notation/main.tekx" <<'EOF'
// what every paper of the group takes in
define E(x) as EE[x] read as "the expected value of {x}"
define postulate like axiom titled "Postulate"
define lab as "the Analytical Engine Laboratory"
EOF
printf 'package "callouts"\nversion "0.2.0"\nsummary "Callouts, made by a filter"\nfilter "bin/callout"\nblock "callout"\n' \
    >"$R/callouts/package.tekx"
cat >"$R/callouts/bin/callout" <<'EOF'
#!/bin/sh
# custom callout nodes become a quotation with a strong first sentence
exec awk '
function flush() {
	if (!open) return
	printf "%squote@%s\n%s  para@%s 0\n%s    strong\n%s      text %s.\n%s    text  %s\n", ind, ln, ind, ln, ind, ind, title, ind, body
	open = 0
}
/^ *custom@[0-9]+ callout / {
	flush(); match($0, /^ */); ind = substr($0, 1, RLENGTH)
	split($1, a, "@"); ln = a[2]; title = "Note"; body = ""; open = 1; next
}
open && /^ *args / { t = $0; sub(/^ *args /, "", t); gsub(/"/, "", t); title = t; next }
open && /^ *verbatim/ { t = $0; sub(/^ *verbatim(@[0-9:]+)? ?/, "", t); body = body (body == "" ? "" : " ") t; next }
{ flush(); print }
END { flush() }'
EOF
chmod +x "$R/callouts/bin/callout"

cat >"$W/paper/paper.tekx" <<'EOF'
title "Using In-House Packages"
use house-notation 1.2
use callouts

We work at {lab}. For a random variable $X$, $Var X = E(X^2) - E(X)^2$.

postulate
  Every integral converges.

callout "Remember"
  Packages are pinned by hash in tekx.lock.
EOF

cd "$W/paper" || exit 2

# --- lock and build ---
"$T" pkg registry add house "$R" >/dev/null || fail "registry add"
"$T" pkg add house-notation >out.txt 2>&1 || fail "add house-notation"
expect "added house-base 1.0.2" out.txt
"$T" pkg add callouts >/dev/null 2>&1 || fail "add callouts"
expect "^package house-base 1.0.2 sha256:" tekx.lock
"$T" -P paper.tekx >tree.txt 2>&1 || fail "build with packages"
expect "text We work at the Analytical Engine Laboratory" tree.txt
expect "theorem@[0-9]* postulate" tree.txt
expect "quote@" tree.txt
"$T" -M paper.tekx >math.txt 2>&1 || fail "mathematics with packages"
expect "the expected value of" math.txt
"$T" -o paper.pdf paper.tekx 2>out.txt || fail "PDF with packages"

# --- a package changed underneath the document is refused ---
echo 'define lab as "somewhere else"' >>.tekx/packages/house-notation/main.tekx
"$T" -o paper.pdf paper.tekx 2>out.txt && fail "a changed package must be refused"
expect "house-notation has changed since it was locked" out.txt
[ "$(wc -l <out.txt)" -eq 1 ] || fail "a failed use should report itself only"
"$T" pkg verify >out.txt 2>&1 && fail "verify should notice the change"
"$T" pkg install >/dev/null 2>&1 || fail "install restores"
"$T" pkg verify >/dev/null 2>&1 || fail "verify after install"

# --- versions, unknown and uninstalled packages ---
sed 's/use house-notation 1.2/use house-notation 2.0/' paper.tekx >v2.tekx
"$T" -o v2.pdf v2.tekx 2>out.txt && fail "version 2.0 must be refused"
expect "wants house-notation 2.0, but 1.3.0 is locked" out.txt
printf 'use nosuch\n\nHi.\n' >other.tekx
"$T" -o other.pdf other.tekx 2>out.txt && fail "an unknown package must be refused"
expect "nosuch is not in" out.txt
mv .tekx .tekx-away
"$T" -o paper.pdf paper.tekx 2>out.txt && fail "uninstalled packages must be refused"
expect "locked but not installed" out.txt
mv .tekx-away .tekx

# --- a custom block without its filter's work is an error ---
printf '#!/bin/sh\nexit 3\n' >.tekx/packages/callouts/bin/callout
"$T" pkg lock >/dev/null 2>&1
"$T" -o paper.pdf paper.tekx 2>out.txt && fail "a failing filter must fail the build"
expect "failed (exit status 3)" out.txt
"$T" pkg remove house-base >out.txt 2>&1 && fail "removing a dependency must be refused"
expect "depends on house-base" out.txt

# --- a git registry: commits pinned, installs reproduce them ---
if command -v git >/dev/null 2>&1; then
	G=$W/gitreg
	cp -r "$R" "$G"
	(cd "$G" && git init -q && git add -A &&
	    git -c user.email=t@example.org -c user.name=t commit -qm one) || fail "git init"
	mkdir -p "$W/paper2"
	cp paper.tekx "$W/paper2/"
	cd "$W/paper2" || exit 2
	"$T" pkg registry add house "file://$G" >/dev/null || fail "git registry add"
	"$T" pkg add house-notation >/dev/null 2>&1 || fail "add from git"
	"$T" pkg add callouts >/dev/null 2>&1 || fail "add callouts from git"
	expect "^package house-notation 1.3.0 sha256:[0-9a-f]* git file://" tekx.lock
	before=$(cat tekx.lock)
	(cd "$G" && echo 'define lab as "moved"' >>house-notation/main.tekx &&
	    git -c user.email=t@example.org -c user.name=t commit -qam two) || fail "git commit"
	rm -rf .tekx
	"$T" pkg install >/dev/null 2>&1 || fail "install from git at the locked commit"
	[ "$before" = "$(cat tekx.lock)" ] || fail "install must not change the lock"
	"$T" -P paper.tekx >tree.txt 2>&1 || fail "build from git"
	expect "text We work at the Analytical Engine Laboratory" tree.txt
	cd "$W/paper" || exit 2
fi

# --- packages under development: TEKX_PATH, no lock ---
mkdir -p "$W/dev"
cp "$W/paper2/paper.tekx" "$W/dev/" 2>/dev/null || cp paper.tekx "$W/dev/"
(cd "$W/dev" && TEKX_PATH="$R" "$T" -P paper.tekx >tree.txt 2>&1) || fail "TEKX_PATH build"
expect "Analytical Engine Laboratory" "$W/dev/tree.txt"

# --- a new package skeleton is a valid package ---
cd "$W" || exit 2
"$T" pkg new fresh >/dev/null || fail "pkg new"
(cd "$W/paper" && "$T" pkg add "$W/fresh" >/dev/null 2>&1) || fail "add the new package"

for f in "$W"/san.*; do
	[ -e "$f" ] || continue
	fail "sanitizer report $f"
	head -5 "$f"
done
[ $fails -eq 0 ] && echo "pkg: ok"
[ $fails -eq 0 ]
