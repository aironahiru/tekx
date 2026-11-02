# Cross-layer pipeline: end-to-end trace, comparison engine, operational bottlenecks

This document links the seven layers. It traces two constructs from source to screen reader, consolidates the legacy-vs-tagged comparison, and records measured operational costs. Every number comes from `evidence/` (TeX Live 2026, LaTeX 2026-11-01, 4 CPU threads).

---

## 1. End-to-end trace: `\section{Introduction}` (LuaLaTeX, `tagging=on`)

| Layer | Transformation | Artefact (observed) |
|---|---|---|
| 1 Syntax | `\section{Introduction}` (no author change) | – |
| 2 Semantic map | Symbolic names resolve to `section` (container), `section-heading`, `heading-number` in `https://www.latex-project.org/ns/dflt` | RoleMapNS: `section→pdf2:Sect`, `section-heading→pdf2:H1`, `heading-number→pdf2:Lbl` |
| 4 Kernel | `latex-lab-sec-template` instance runs; sockets `sec/begin{1}`, `sec/title/begin{1}`, `sec/title/number{2}` call `\tag_struct_begin:n`, `\tag_mc_begin:n`; the `order` key separator inserts `\pdffakespace` | – |
| 3 Engine | MC attributes set; at shipout `pre_shipout_filter` → `mark_shipout` inserts literals; `space_chars_shipout` inserts U+0020 | `/heading-number <</MCID 3>> BDC … EMC`, `/section-heading <</MCID 4>> BDC … EMC` |
| 5 Ecosystem | `titlesec` would be emulated (`latex-lab-titlesec`), not loaded | – |
| 6 Container | StructElems with `/NS` → latex namespace; ParentTree maps MCIDs 3,4,5 → elements; `/T (Introduction)` | `section-heading [latex] /T='Introduction' (+2 MC)` |
| 7 Viewer | pdf.js resolves via fallback `/RoleMap`: `section→Sect`, `section-heading→H1`, `heading-number→Lbl`; DOM `role="heading" aria-level="1"` | `Sect / H1 / Lbl` in `getStructTree()` |

## 2. End-to-end trace: inline `$a^2+b^2=c^2$`

| Layer | LuaLaTeX `mathml-SE` | LuaLaTeX `mathml-AF` (default with unicode-math) | pdfLaTeX + external MathML |
|---|---|---|---|
| 1 | `tagging-setup={math/setup=mathml-SE}` | `tagging-setup={math/setup={mathml-AF,tex-AF}}` or nothing | `math/mathml/sources={<file>}` |
| 4 | `$` captured whole; socket `math/inline/formula/begin{2}`; `Formula` opened | same | same; MD5 of `$a^2+b^2=c^2$` = `E1AFFC0A43F89AE2DA953B789D29E42C` |
| 3 | `pre_mlist_to_hlist_filter` → luamml writes `math`/`msup`/`mi`/`mn`/`mo` structures | luamml appends `<div><h2>\mml 1</h2>…` to `<jobname>-luamml-mathml.html` | no conversion; hash lookup at `begindocument` |
| 6 | `Formula [pdf2]` → `math [mathml]` → `msup` → `mi` (+1 MC) … | `Formula` with `/AF [mathml-1.xml (/Supplement, application/mathml+xml), tag-AFfile1.tex (/Source, application/x-tex)]` | same AF pair |
| 7 pdf.js | MathML DOM elements | `mathML` property → `setHTML` with Sanitizer | same |
| Passes to valid UA-2 | 1 | 1 (MathML AF only from pass 2) | 2 |
| veraPDF | ua2 PASS, 4f PASS, wt1a PASS | ua2 PASS, 4f PASS | ua2 PASS (pass ≥ 2), 4f PASS |

## 3. Master comparison: legacy vs tagged pipeline

| Dimension | Legacy (`\documentclass` only) | Tagged (`\DocumentMetadata{…tagging=on}`) | Evidence |
|---|---|---|---|
| Kernel entry | format code paths | `documentmetadata-support.ltx` + 20 latex-lab modules | log |
| PDF version | 1.7 | 2.0 | `activation-modes.txt` |
| 8-bit text encoding (pdfTeX) | OT1, CM fonts | T1, cm-super fonts | `activation-modes.txt` |
| Marked content | none | `BDC`/`BMC`/`EMC` with MCIDs; artifacts | `01-…txt` |
| Logical structure | none | StructTreeRoot, 6 namespaces, ParentTree, IDTree, ClassMap, fallback RoleMap | `01-…txt` |
| Interword spaces | positioning | real glyphs (`PdfTeX-Space` / font U+0020) | `activation-modes.txt` |
| Metadata | Info dictionary | XMP (`pdfuaid`, `pdfaid`, `dc:*`, WTPDF declarations); Info omitted for A-4 | `01-…txt` |
| Language | none | Catalog `/Lang` | `01-…txt` |
| Math | glyphs | `Formula` + MathML (SE or AF) + TeX-source AF | `02-…txt`, `03-…txt` |
| Images | XObject | `Figure` + `/Alt`, or artifact | `01-…txt` |
| Tables | layout | `Table/TR/TH/TD` + `/Scope`, `/RowSpan`, `/ColSpan` classes | `06-08-…txt` |
| hyperref driver | `hpdftex.def` | `hgeneric-testphase.def` | logs |
| veraPDF UA-2 | FAIL (9 clauses) | PASS | `04-…txt`, `01-…txt` |
| File size (49 pp.) | 166 KiB (pdfTeX) / 126 KiB (LuaTeX) | 331 KiB / 260 KiB (AF) / 316 KiB (SE) | `benchmark.txt` |
| Compile time (49 pp.) | 0.23 s / 1.85 s | 6.06 s / 7.93 s (AF) / 13.05 s (SE) | `benchmark.txt` |

## 4. Operational bottlenecks (measured)

### 4.1 Compile cost [R: `evidence/benchmark.txt`]

Corpus: `tools/gen_bench.py` (40 sections × 4 paragraphs with inline math, 40 numbered equations, 40 lists, 40 tables; 49 pages). Median of 3 warm runs after 2 priming runs.

| Variant | Engine | Median time | Δ vs legacy | Size | StructElems | UA-2 |
|---|---|---|---|---|---|---|
| b1 legacy | pdfLaTeX | 0.23 s | – | 166 KiB | 0 | FAIL |
| b2 tagged | pdfLaTeX | 6.06 s | ×26 | 331 KiB | 1,643 | PASS |
| b3 legacy | LuaLaTeX | 1.85 s | – | 126 KiB | 0 | FAIL |
| b4 tagged, MathML AF | LuaLaTeX | 7.93 s | ×4.3 | 260 KiB | 1,604 | PASS |
| b5 tagged, MathML SE | LuaLaTeX | 13.05 s | ×7.1 | 316 KiB | 4,684 | PASS |

Run-to-run spread across the three audit executions on 2026-11-02 (median of 3 each): b1 0.22–0.27 s, b2 5.94–6.86 s, b3 1.79–1.85 s, b4 7.57–7.93 s, b5 12.73–13.64 s. Sizes and structure-element counts were identical in all runs. The table above is the final run committed in `evidence/benchmark.txt`.

Readings: absolute tagged cost is similar on both engines for AF-only math. pdfTeX needs at least 2 passes to validate, LuaTeX 1. MathML structure elements nearly triple the element count (one element per token) and add about 65% time over AF. `tagging=draft` exists to skip tree writing during drafting (Layer 1 A.2); its cost was not benchmarked here.

### 4.2 Convergence [R]

| Engine | Pass 1 | Pass 2 | Pass 3 |
|---|---|---|---|
| pdfLaTeX (generic mode) | FAIL §8.2.2 (79 checks), rerun warning | PASS, rerun warning | PASS, no warning |
| LuaLaTeX (Lua mode) | PASS | PASS | – |
| LuaLaTeX MathML AF | PASS without MathML AF (TeX AF only) | PASS with MathML AF | – |

### 4.3 Validation and inspection workflow

```bash
VERAPDF=/opt/verapdf/verapdf TEXBIN=/usr/local/texlive/2026/bin/x86_64-linux \
PDFJS=/opt/pdfjs/node_modules/pdfjs-dist tools/run-audit.sh
```

```bash
verapdf -f ua2 --format text doc.pdf
```

```bash
verapdf -f wt1a --format text doc.pdf
```

```bash
python3 -I tools/verapdf_report.py "$(command -v verapdf)" ua2 doc.pdf
```

```bash
python3 -I tools/inspect_pdf.py doc.pdf tree
```

## 5. Diagnostic matrix: symptom → layer → cause → fix

| Symptom (verbatim where applicable) | Layer | Cause | Fix |
|---|---|---|---|
| `! LaTeX Error: Two \DocumentMetadata declarations.` | 1 | Second call (error since 2026-06-01) | Merge into one call |
| `DocumentMetadata should be used only before \documentclass` | 1 | Late call | Move to line 1 |
| veraPDF `8.11.1 … shall contain a dc:title entry` | 1/6 | No `\title` | Add `\title{…}` |
| veraPDF `8.2.5.28.2` Figure | 1 | `\includegraphics` without `alt` | `alt=…` or `artifact` |
| veraPDF `8.2.2` on pass 1 (pdfTeX) | 3 | Unresolved properties | Re-run (≥ 2 passes) |
| `! Package tagpdf Error: The number of automatic begin (n) and end (m) text-block para hooks differ!` | 4/5 | Paragraph hooks fired inside boxes (e.g. `soul`) | Remove package / suspend tagging around construct |
| `Package tagpdf Warning: Parent-Child 'pdf2:P' --> 'pdf2:P'` | 2 | Nested P | Same as above |
| veraPDF `ISO 32005:2023 … Table 5. P-P` | 2/6 | Nested P in output | Same as above |
| `Package tagpdf Warning: Neither unicode-math nor lua-unicode-math has been loaded` | 3/5 | LuaLaTeX without Unicode math fonts: no automatic MathML | Load `unicode-math`, or force luamml and accept mapping risk |
| `Package pdfmanagement Warning: The loading of package hyperxmp is disabled.` | 5 | Suppressed package | Use `\DocumentMetadata` keys |
| MathML statistic: `0 fitting MathML AF found` | 5 | Formula source edited; MD5 mismatch | Regenerate MathML file (LuaLaTeX run or `write-dummy`) |
| Tables pass veraPDF but AT reports wrong spans | 2/5 | `multirow` package | `\tagpdfsetup{table/multirow=n}` inside the cell |
| Title not announced as heading | 2/7 | `Title` has no AAPI mapping | `tagging-setup={title=H1}` |
| No status report despite `check-tagging-status` | 5 | `latex-tagging-status` (collection-latexextra) not installed | `tlmgr install latex-tagging-status` |
| Copied text from HarfBuzz-shaped scripts wrong in pdf.js only | 3/7 | pdf.js ignores content-stream ActualText | Consumer limitation; verify with poppler/Acrobat |
