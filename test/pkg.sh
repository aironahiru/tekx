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

# --- hostile and broken input: what the adversarial review found ---
H=$W/hostile
mkdir -p "$H/victim/data" "$H/src/p" "$H/reg"
echo precious >"$H/victim/data/keep.txt"
printf 'package "p"\nversion "1.0.0"\nprovides "main.tekx"\n' >"$H/src/p/package.tekx"
echo 'define lab as "x"' >"$H/src/p/main.tekx"
mkpkg() {	# mkpkg DIR NAME EXTRA-MANIFEST-LINES
	mkdir -p "$1"
	printf 'package "%s"\nversion "1.0.0"\nprovides "main.tekx"\n%b' "$2" "$3" >"$1/package.tekx"
	echo 'define lab as "x"' >"$1/main.tekx"
}
# a lock whose package name climbs out of the store must not touch anything
mkdir -p "$H/proj1" && cd "$H/proj1" || exit 2
"$T" pkg add "$H/src/p" >/dev/null 2>&1 || fail "add p"
hash=$(awk '/^package/ {print $4}' tekx.lock)
rm -rf .tekx tekx.lock
printf 'package ../../../victim/data 1.0.0 %s dir %s\n' "$hash" "$H/src/p" >tekx.lock
"$T" pkg install >out.txt 2>&1 && fail "a lock naming ../../ must be refused"
expect "package name must be" out.txt
[ -f "$H/victim/data/keep.txt" ] || fail "install destroyed a directory outside the project"
# duplicate entries, bad versions, a short hash
printf 'package p 1.0.0 %s dir %s\npackage p 1.0.0 %s dir %s\n' "$hash" "$H/src/p" "$hash" "$H/src/p" >tekx.lock
"$T" pkg list >out.txt 2>&1 && fail "a duplicated lock entry must be refused"
expect "already listed" out.txt
printf 'package p v1 %s dir %s\n' "$hash" "$H/src/p" >tekx.lock
"$T" pkg list >out.txt 2>&1 && fail "a lock version v1 must be refused"
printf 'package p 1.0.0 sha256:abc dir %s\n' "$H/src/p" >tekx.lock
"$T" pkg list >out.txt 2>&1 && fail "a short hash must be refused"
rm -f tekx.lock
# provides and filters must stay inside the package, and so its hash
echo SECRET >"$H/secret"
mkpkg "$H/reg/evil" evil 'provides "../../secret"\n'
"$T" pkg add "$H/reg/evil" >out.txt 2>&1 && fail "provides ../ must be refused"
expect "outside the package" out.txt
mkdir -p "$H/tools" && printf '#!/bin/sh\ncat\n' >"$H/tools/t" && chmod +x "$H/tools/t"
mkpkg "$H/reg/ff" ff 'filter "../../tools/t"\n'
"$T" pkg add "$H/reg/ff" >out.txt 2>&1 && fail "a filter outside the package must be refused"
expect "outside the package" out.txt
# a package may not take over tekx's own blocks
mkpkg "$H/reg/sh" sh 'block "theorem"\n'
"$T" pkg add "$H/reg/sh" >out.txt 2>&1 && fail "block theorem must be refused"
expect "tekx's own keywords" out.txt
# versions
mkdir -p "$H/reg/bv" && printf 'package "bv"\nversion "1.2.3.4"\n' >"$H/reg/bv/package.tekx"
"$T" pkg add "$H/reg/bv" >out.txt 2>&1 && fail "version 1.2.3.4 must be refused"
# git: nothing that looks like an option
"$T" pkg add -- '--upload-pack=x .git' >out.txt 2>&1 && fail "an option-like git source must be refused"
# a failed add changes nothing: a diamond with conflicting versions
mkdir -p "$H/reg2" "$H/proj2"
mkdir -p "$H/reg2/b" && printf 'package "b"\nversion "2.0.0"\nprovides "main.tekx"\n' >"$H/reg2/b/package.tekx" &&
    echo x >"$H/reg2/b/main.tekx"
mkpkg "$H/reg2/c" c 'depends "b 2.0"\n'
mkpkg "$H/reg2/a" a 'depends "c 1.0"\ndepends "b 1.0"\n'
cd "$H/proj2" || exit 2
"$T" pkg registry add r "$H/reg2" >/dev/null 2>&1
"$T" pkg add a >out.txt 2>&1 && fail "a version conflict must fail the add"
[ "$(grep -c '^package' tekx.lock)" -eq 0 ] || fail "a failed add must leave the lock unchanged"
[ -z "$(ls -A .tekx/packages 2>/dev/null)" ] || fail "a failed add must leave no packages or staging behind"
# cycles are named as cycles
mkpkg "$H/reg2/x" x 'depends "y 1.0"\n'
mkpkg "$H/reg2/y" y 'depends "x 1.0"\n'
"$T" pkg add x >out.txt 2>&1 && fail "a dependency cycle must fail"
expect "dependency cycle: x → y → x" out.txt
# concurrent adds keep every entry
for q in d1 d2 d3 d4; do mkpkg "$H/reg2/$q" $q ''; done
"$T" pkg add d1 & "$T" pkg add d2 & "$T" pkg add d3 & "$T" pkg add d4 & wait
[ "$(grep -c '^package d' tekx.lock)" -eq 4 ] || fail "concurrent adds lost lock entries"
# an interrupted install is repaired by the next command
mv .tekx/packages/d1 .tekx/packages/d1.old && mkdir -p .tekx/packages/.staging-junk-1
"$T" pkg install >/dev/null 2>&1 || fail "install after an interruption"
[ -d .tekx/packages/d1 ] && [ ! -e .tekx/packages/d1.old ] && [ ! -e .tekx/packages/.staging-junk-1 ] ||
    fail "an interrupted install was not repaired"
# a project path with a blank in it, and a package with a filter
mkdir -p "$H/my project" && cp "$W/paper/paper.tekx" "$H/my project/" && cd "$H/my project" || exit 2
"$T" pkg registry add house "$R" >/dev/null 2>&1 && "$T" pkg add house-notation >/dev/null 2>&1 &&
    "$T" pkg add callouts >/dev/null 2>&1 || fail "adds in a directory with a blank"
"$T" -P paper.tekx >tree.txt 2>&1 || fail "a filter in a directory with a blank"
expect "quote@" tree.txt
cd "$W" || exit 2

for f in "$W"/san.*; do
	[ -e "$f" ] || continue
	fail "sanitizer report $f"
	head -5 "$f"
done
[ $fails -eq 0 ] && echo "pkg: ok"
[ $fails -eq 0 ]
