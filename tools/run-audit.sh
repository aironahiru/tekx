#!/usr/bin/env bash
# Reproduce every compile, validation and inspection result cited in docs/.
#
# Requirements
#   - TeX Live 2026 with LaTeX 2026-11-01 (lualatex, pdflatex on PATH or TEXBIN)
#     packages beyond collection-latex/-luatex/-latexrecommended:
#     latex-tagging-status, mwe, soul, ulem, multirow, caption, enumitem, hyperxmp
#   - veraPDF >= 1.30 (VERAPDF=/path/to/verapdf)
#   - python3 with pikepdf; poppler-utils (pdftotext, pdfinfo)
#   - optional: node + pdfjs-dist (PDFJS=/path/to/node_modules/pdfjs-dist)
#
# Usage: VERAPDF=... [TEXBIN=...] [PDFJS=...] tools/run-audit.sh
# Output: build/ (artefacts, git-ignored) and evidence/*.txt (committed).
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
[ -n "${TEXBIN:-}" ] && export PATH="$TEXBIN:$PATH"
: "${VERAPDF:?set VERAPDF to the veraPDF CLI launcher}"
B="$ROOT/build"; E="$ROOT/evidence"; T="$ROOT/tools"
rm -rf "$B"; mkdir -p "$B" "$E"
cp -r "$ROOT"/examples/* "$B"/
cd "$B"

insp() { python3 -I "$T/inspect_pdf.py" "$@"; }
vera() { python3 -I "$T/verapdf_report.py" "$VERAPDF" "$1" "$2"; }
run()  { # engine file passes
  local eng=$1 f=$2 n=$3 i
  for i in $(seq "$n"); do "$eng" -interaction=nonstopmode "$f.tex" >/dev/null 2>&1; done
}
hdr()  { printf '\n===== %s =====\n' "$*"; }

# ---------------------------------------------------------------- environment
{
  hdr "Toolchain"
  lualatex --version | head -1
  pdftex --version | head -1
  "$VERAPDF" --version 2>/dev/null | grep -m1 veraPDF
  pdftotext -v 2>&1 | head -1
  python3 -c "import pikepdf; print('pikepdf', pikepdf.__version__)"
  [ -n "${PDFJS:-}" ] && node -e "console.log('pdfjs-dist', require('$PDFJS/package.json').version)"
  hdr "Format and component versions (from 01-ua2-lualatex-mathml-se.log)"
} > "$E/environment.txt"

# ---------------------------------------------------------------- 01 / 02 / 03
{
  for p in 1 2; do
    lualatex -interaction=nonstopmode 01-ua2-lualatex-mathml-se.tex >/dev/null 2>&1
    hdr "lualatex pass $p"; vera ua2 01-ua2-lualatex-mathml-se.pdf
  done
} > "$E/01-lualatex-pass-convergence.txt"
grep -E "^(LaTeX2e|L3 programming layer) <" 01-ua2-lualatex-mathml-se.log | awk '!seen[$0]++' >> "$E/environment.txt"
grep -E "^(Package|File): (tagpdf|tagpdf-luatex.def|latex-lab-testphase-latest|latex-lab-testphase-math|latex-lab-testphase-table|pdfmanagement-init|l3backend-pdf-luatex.def|hgeneric-testphase.def|luamml|unicode-math|fontspec|hyperref) " \
  01-ua2-lualatex-mathml-se.log | cut -c1-100 | sort -u >> "$E/environment.txt"
grep -m1 "documentmetadatasupportversion" "$(kpsewhich documentmetadata-support.ltx)" >> "$E/environment.txt"

{
  hdr "veraPDF"; vera ua2 01-ua2-lualatex-mathml-se.pdf; vera 4f 01-ua2-lualatex-mathml-se.pdf
  vera wt1a 01-ua2-lualatex-mathml-se.pdf
  hdr "Catalog / StructTreeRoot"; insp 01-ua2-lualatex-mathml-se.pdf catalog
  hdr "XMP"; insp 01-ua2-lualatex-mathml-se.pdf xmp
  hdr "Structure tree"; insp 01-ua2-lualatex-mathml-se.pdf tree 10
  hdr "Role maps"; insp 01-ua2-lualatex-mathml-se.pdf rolemap
  hdr "Marked content, page 1 (first 14 operators)"; insp 01-ua2-lualatex-mathml-se.pdf stream 14
  hdr "Fonts"; insp 01-ua2-lualatex-mathml-se.pdf fonts
  hdr "Embedded files"; insp 01-ua2-lualatex-mathml-se.pdf af
} > "$E/01-ua2-lualatex-mathml-se.txt"

rm -f 02-ua2-lualatex-mathml-af-luamml-mathml.html
run lualatex 02-ua2-lualatex-mathml-af 1
{
  hdr "Pass 1 (no <jobname>-luamml-mathml.html yet)"; insp 02-ua2-lualatex-mathml-af.pdf af
} > "$E/02-ua2-lualatex-mathml-af.txt"
run lualatex 02-ua2-lualatex-mathml-af 1
{
  hdr "Pass 2"; insp 02-ua2-lualatex-mathml-af.pdf af
  grep -A6 "MathML statistic" 02-ua2-lualatex-mathml-af.log
  hdr "veraPDF"; vera ua2 02-ua2-lualatex-mathml-af.pdf; vera 4f 02-ua2-lualatex-mathml-af.pdf
  hdr "Structure tree (formulas)"; insp 02-ua2-lualatex-mathml-af.pdf tree 6 | grep -E "Formula|Document|section \["
  hdr "Head of 02-ua2-lualatex-mathml-af-luamml-mathml.html"; head -28 02-ua2-lualatex-mathml-af-luamml-mathml.html
} >> "$E/02-ua2-lualatex-mathml-af.txt"

{
  for p in 1 2 3; do
    pdflatex -interaction=nonstopmode 03-ua2-pdflatex-external-mathml.tex >/dev/null 2>&1
    hdr "pdflatex pass $p"; vera ua2 03-ua2-pdflatex-external-mathml.pdf
    grep -c "Rerun to get cross-references right" 03-ua2-pdflatex-external-mathml.log | sed 's/^/rerun warnings: /'
  done
  grep -A6 "MathML statistic" 03-ua2-pdflatex-external-mathml.log
  hdr "veraPDF 4f"; vera 4f 03-ua2-pdflatex-external-mathml.pdf
  hdr "Fonts (T1 / cm-super + PdfTeX-Space)"; insp 03-ua2-pdflatex-external-mathml.pdf fonts
  hdr "Structure tree"; insp 03-ua2-pdflatex-external-mathml.pdf tree 6
} > "$E/03-ua2-pdflatex-external-mathml.txt"

# ---------------------------------------------------------------- 04 / 05
run pdflatex 04-legacy-baseline 2
{
  hdr "Catalog"; insp 04-legacy-baseline.pdf catalog
  hdr "Fonts"; insp 04-legacy-baseline.pdf fonts
  hdr "Marked content"; insp 04-legacy-baseline.pdf stream 3
  hdr "veraPDF ua2"; vera ua2 04-legacy-baseline.pdf
} > "$E/04-legacy-baseline.txt"

run pdflatex 05-pdfmanagement-without-tagging 2
{ hdr "Catalog"; insp 05-pdfmanagement-without-tagging.pdf catalog; hdr "XMP"; insp 05-pdfmanagement-without-tagging.pdf xmp; } \
  > "$E/05-pdfmanagement-without-tagging.txt"

# ---------------------------------------------------------------- 06 / 07 / 08 / 09
run lualatex 06-table-header-rowspan 2
run lualatex 08-friction-multirow 2
{
  hdr "06 native table tagging"; vera ua2 06-table-header-rowspan.pdf; insp 06-table-header-rowspan.pdf table
  hdr "08 multirow package"; vera ua2 08-friction-multirow.pdf; insp 08-friction-multirow.pdf table
  if [ -n "${PDFJS:-}" ]; then
    for f in 06-table-header-rowspan 08-friction-multirow; do
      hdr "pdf.js getStructTree: $f"; node "$T/pdfjs_probe.mjs" "$f.pdf" | sed -n '/getStructTree/,/metadata/p' | grep -E "Table|TR|TH|TD"
    done
  fi
} > "$E/06-08-table-semantics.txt"

run lualatex 07-friction-soul 2
{
  hdr "Compile errors"; grep -A1 "^! " 07-friction-soul.log
  hdr "tagpdf warnings"; grep -oE "Package tagpdf Warning: .{0,80}" 07-friction-soul.log | sort | uniq -c
  hdr "veraPDF ua2"; vera ua2 07-friction-soul.pdf
} > "$E/07-friction-soul.txt"

run lualatex 09-check-tagging-status 1
{
  grep -E "pdfmanagement Warning|Emulating enumitem" 09-check-tagging-status.log
  lualatex -interaction=nonstopmode 09-check-tagging-status.tex 2>&1 | sed -n '/Status report of the tagging support/,/End of status report/p'
} > "$E/09-check-tagging-status.txt"

# ---------------------------------------------------------------- clipboard
cd "$B/clipboard"
for f in c1-legacy-pdftex:pdflatex c2-tagged-pdftex:pdflatex c3-tagged-lua:lualatex c4-tagged-lua-softhyphen:lualatex c5-actualtext-span:pdflatex; do
  run "${f##*:}" "${f%%:*}" 3
done
{
  for f in c1-legacy-pdftex c2-tagged-pdftex c3-tagged-lua c4-tagged-lua-softhyphen; do
    hdr "poppler pdftotext -raw: $f"
    pdftotext -raw -enc UTF-8 "$f.pdf" - | sed -n '1,7p' | python3 -I -c "import sys; [print(repr(l.rstrip(chr(10)))) for l in sys.stdin]"
  done
  hdr "poppler pdftotext default mode (dehyphenation): c1-legacy-pdftex"
  pdftotext -enc UTF-8 c1-legacy-pdftex.pdf - | sed -n '1,6p'
  hdr "content-stream /ActualText span (c5): poppler"; pdftotext -raw c5-actualtext-span.pdf - | head -1
  if [ -n "${PDFJS:-}" ]; then
    hdr "content-stream /ActualText span (c5): pdf.js getTextContent()"; node "$T/pdfjs_probe.mjs" c5-actualtext-span.pdf | sed -n 2p
    for f in c2-tagged-pdftex c3-tagged-lua; do hdr "pdf.js: $f"; node "$T/pdfjs_probe.mjs" "$f.pdf"; done
    hdr "pdf.js: 01-ua2-lualatex-mathml-se (MathML structure elements)"; node "$T/pdfjs_probe.mjs" "$B/01-ua2-lualatex-mathml-se.pdf" | sed -n '/getStructTree/,/metadata/p' | head -30
  fi
  hdr "Structure of c3-tagged-lua"; insp c3-tagged-lua.pdf tree 8
} > "$E/clipboard.txt"

# ---------------------------------------------------------------- activation modes
cd "$B/modes"
{
  for f in m0-none m1-empty m2-off m3-draft m4-on; do
    run pdflatex "$f" 2
    python3 -I - "$f.pdf" <<'PY'
import sys, pikepdf
p = pikepdf.open(sys.argv[1]); r = p.Root; pg = p.pages[0]
fonts = pg.obj.Resources.get("/Font") or {}
space = [k for k, f in fonts.items() if "PdfTeX-Space" in str(f.get("/BaseFont"))]
c = {}; sp = 0
for ops, op in pikepdf.parse_content_stream(pg):
    c[str(op)] = c.get(str(op), 0) + 1
    if str(op) == "Tf" and str(ops[0]) in space: sp += 1
names = sorted(str(f.get("/BaseFont")).split("+")[-1] for f in fonts.values())
print(f"{sys.argv[1]:12} PDF {p.pdf_version}  StructTreeRoot={'/StructTreeRoot' in r}  Marked={bool(r.get('/MarkInfo', {}).get('/Marked', False))}"
      f"  BDC={c.get('BDC',0)} BMC={c.get('BMC',0)}  XMP={'/Metadata' in r}  Lang={r.get('/Lang')}  space-font-selections={sp}  fonts={names}")
PY
  done
  hdr "Text operators, m2-off vs m4-on (interword space glyph only with tagging=on)"
  for f in m2-off m4-on; do
    echo "-- $f"
    python3 -I - "$f.pdf" <<'PY'
import sys, pikepdf
p = pikepdf.open(sys.argv[1])
for ops, op in pikepdf.parse_content_stream(p.pages[0]):
    if str(op) in ("TJ", "Tj"):
        a = ops[0]; items = a if isinstance(a, pikepdf.Array) else [a]
        print(" ", str(op), repr("".join(bytes(x).decode("latin-1") for x in items if isinstance(x, pikepdf.String))))
PY
  done
} > "$E/activation-modes.txt"

# ---------------------------------------------------------------- version/standard conflict
cd "$B/conflict"
{
  for f in v1-standard-then-version v2-version-then-standard; do
    lualatex -interaction=nonstopmode "$f.tex" >/dev/null 2>&1
    hdr "$f: $(head -1 "$f.tex")"
    pdfinfo "$f.pdf" | grep "PDF version"
    grep -A2 "Package pdf Warning" "$f.log" || echo "(no pdf warning)"
  done
} > "$E/version-standard-conflict.txt"

# ---------------------------------------------------------------- benchmark
mkdir -p "$B/bench"; cd "$B/bench"
python3 -I "$T/gen_bench.py" .
{
  echo "40 sections, ~49 pages, article class; median of 3 warm runs (after 2 priming runs)"
  nproc | sed 's/^/CPU threads: /'
  for f in b1-pdftex-legacy:pdflatex b2-pdftex-tagged:pdflatex b3-lua-legacy:lualatex b4-lua-tagged-af:lualatex b5-lua-tagged-se:lualatex; do
    n=${f%%:*}; e=${f##*:}; run "$e" "$n" 2; ts=""
    for r in 1 2 3; do s=$(date +%s.%N); "$e" -interaction=nonstopmode "$n.tex" >/dev/null 2>&1; ts="$ts $(echo "$(date +%s.%N) - $s" | bc)"; done
    med=$(echo $ts | tr ' ' '\n' | sort -n | sed -n 2p)
    se=$(python3 -I -c "import pikepdf; p=pikepdf.open('$n.pdf'); print(sum(1 for o in p.objects if isinstance(o, pikepdf.Dictionary) and '/S' in o and '/P' in o))")
    v=$("$VERAPDF" -f ua2 --format text "$n.pdf" 2>/dev/null | cut -c1-4)
    printf "%-18s %-9s pages=%s median=%.2fs size=%sKiB struct-elems=%s ua2=%s\n" "$n" "$e" \
      "$(pdfinfo "$n.pdf" | awk '/^Pages/{print $2}')" "$med" "$(( $(stat -c %s "$n.pdf") / 1024 ))" "$se" "$v"
  done
} > "$E/benchmark.txt"

echo "evidence written to $E"
