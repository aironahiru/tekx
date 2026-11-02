# LaTeX Tagged-PDF stack audit (LaTeX 2026-11-01 / TeX Live 2026)

A developer reference for the modern "Tagged PDF" LaTeX pipeline. It covers seven layers, from `\DocumentMetadata` down to PDF-viewer clipboard engines. Every claim is tagged **[S]** (read in source or official docs), **[R]** (reproduced by compiling and validating; raw output in [`evidence/`](evidence/)) or **[U]** (under-documented runtime detail).

Audit date: 2026-11-02. Toolchain: LuaHBTeX 1.24.0, pdfTeX 1.40.29, LaTeX 2026-11-01, tagpdf 1.0i, pdfmanagement 0.97d, luamml 0.9.3, veraPDF 1.30.3, pdf.js 6.5.438, poppler 24.02. Details in [`docs/00-method-and-environment.md`](docs/00-method-and-environment.md).

## Layer documents

Each layer document has the same four sections: **A** technical mechanics and data flow · **B** core entities, commands and specifications · **C** interdependencies and package friction (with legacy-vs-tagged tables) · **D** verified primary sources.

| # | Layer | Document |
|---|---|---|
| 1 | Syntax: LaTeX markup, `\DocumentMetadata` keys | [`docs/01-syntax-layer.md`](docs/01-syntax-layer.md) |
| 2 | Semantic map: namespaces, role maps, `\tagpdfsetup`, sockets | [`docs/02-semantic-map-layer.md`](docs/02-semantic-map-layer.md) |
| 3 | Engine/compiler: pdfTeX generic mode, LuaTeX Lua mode, Unicode/fonts | [`docs/03-engine-compiler-layer.md`](docs/03-engine-compiler-layer.md) |
| 4 | Format kernel: hooks, sockets, templates, math capture, API stack | [`docs/04-format-kernel-layer.md`](docs/04-format-kernel-layer.md) |
| 5 | Package ecosystem: CTAN/TeX Live packaging, compatibility DB, MathML conversion | [`docs/05-package-ecosystem-layer.md`](docs/05-package-ecosystem-layer.md) |
| 6 | Container: ISO 32000-2, ISO 14289-2 (PDF/UA-2), ISO/TS 32005, WTPDF, PDF/A-4 | [`docs/06-container-specification-layer.md`](docs/06-container-specification-layer.md) |
| 7 | Rendering/viewer: pdf.js, poppler, PDFium, Foxit, AT | [`docs/07-rendering-viewer-layer.md`](docs/07-rendering-viewer-layer.md) |
| × | Cross-layer trace, master comparison, benchmarks, diagnostics | [`docs/08-end-to-end-pipeline.md`](docs/08-end-to-end-pipeline.md) |
| – | Consolidated source registry | [`docs/sources.md`](docs/sources.md) |

## Production configuration (validated)

```latex
\DocumentMetadata{
  lang          = en-US,
  pdfstandard   = {UA-2, A-4F},
  tagging       = on,
  tagging-setup = {math/setup = mathml-SE},   % or {mathml-AF, tex-AF}
}
\documentclass{article}
\usepackage{unicode-math}          % LuaLaTeX: enables automatic MathML (luamml)
\title{…}                          % required for XMP dc:title (ISO 14289-2:2024 8.11.1)
```

`lualatex` → veraPDF `ua2` PASS, `4f` PASS, `wt1a` PASS ([`evidence/01-ua2-lualatex-mathml-se.txt`](evidence/01-ua2-lualatex-mathml-se.txt)). The pdfLaTeX variant with MathML from an external file passes from pass 2 onward ([`evidence/03-ua2-pdflatex-external-mathml.txt`](evidence/03-ua2-pdflatex-external-mathml.txt)).

## Key findings

| # | Finding | Evidence |
|---|---|---|
| 1 | Since 2025-11-01 **every** `\DocumentMetadata` loads tagpdf and 20 latex-lab modules, switches 8-bit engines to T1 and defaults to PDF 2.0. `tagging=on/off/draft` only toggles activation. `\RequirePackage{pdfmanagement}` is the documented way to get PDF management without the tagging code | [S] documentmetadata-support.dtx; [R] activation-modes.txt |
| 2 | `pdfstandard=UA-2` writes XMP identification (`pdfuaid:part=2`, `rev=2024`) and WTPDF declarations but enforces nothing and does not enable tagging | [S] l3pdfmeta.dtx; [R] 01-…txt |
| 3 | LaTeX tag names live in `https://www.latex-project.org/ns/dflt` (43 RoleMapNS entries) and `…/ns/book` (7), because ISO 14289-2 §8.2.4 forbids role mapping inside one namespace. A 55-entry fallback `/RoleMap` exists because consumers such as pdf.js read only `/RoleMap` | [R] 01-…txt; [S] pdf.js source |
| 4 | pdfTeX (generic mode) output fails ISO 14289-2 §8.2.2 on pass 1 and validates on pass 2. LuaTeX (Lua mode, MC injection in `pre_shipout_filter`) validates on pass 1 | [R] 03-…txt, 01-lualatex-pass-convergence.txt |
| 5 | Cost on a 49-page corpus: pdfTeX 0.23 s → 6.06 s; LuaTeX 1.85 s → 7.93 s (MathML AF) / 13.05 s (MathML SE, 4,684 structure elements) | [R] benchmark.txt |
| 6 | Validator PASS ≠ correct semantics: `multirow` tables pass veraPDF with no `RowSpan`, while native `\tagpdfsetup{table/multirow=2}` produces `/RowSpan 2` and pdf.js exposes `aria-rowspan`. `soul` fails ISO/TS 32005 Table 5 (`P` inside `P`) | [R] 06-08-…txt, 07-…txt |
| 7 | Clipboard: pdf.js 6.5.438 ignores content-stream `/ActualText` (poppler honours it); structure-level ActualText becomes an accessible name only. Soft hyphens are U+00AD only with LuaTeX `activate/softhyphen=char`; unicode-math text copies as Math Alphanumerics (`𝛼`) | [R] clipboard.txt; [S] pdf.js source |
| 8 | MathML reaches pdf.js two ways: AF (`/Supplement`, `application/mathml+xml`, injected through the browser Sanitizer API) or MathML-namespace structure elements (native MathML DOM) | [S] pdf.js source; [R] clipboard.txt |
| 9 | Of 2,246 classes/packages in the status DB: 1,136 compatible, 322 partially, 522 currently incompatible, 62 no-support, 204 unchecked. `check-tagging-status` needs `latex-tagging-status` (collection-latexextra) and is silent without it | [R] YAML count; [S] documentmetadata-support.dtx |
| 10 | Key order matters: `pdfversion=1.7, pdfstandard=UA-2` silently yields PDF 2.0, while `pdfstandard=UA-2, pdfversion=1.7` yields PDF 1.7 with only a warning (`PDF version 1.7 is too low for standard 'pdf/UA-2'`) | [R] version-standard-conflict.txt |

## Repository layout

```text
docs/       layer documents (00 method, 01–07 layers, 08 cross-layer, sources)
examples/   compiled configurations and reproducers (production, legacy baseline, friction, clipboard, modes, conflict)
tools/      run-audit.sh, inspect_pdf.py (pikepdf), verapdf_report.py, pdfjs_probe.mjs, gen_bench.py
evidence/   captured outputs of tools/run-audit.sh (veraPDF reports, structure dumps, benchmarks)
```

## Reproduce

```bash
VERAPDF=/path/to/verapdf TEXBIN=/path/to/texlive/2026/bin/x86_64-linux PDFJS=/path/to/node_modules/pdfjs-dist tools/run-audit.sh
```

Requirements and package list: [`docs/00-method-and-environment.md`](docs/00-method-and-environment.md).

## License

Apache License 2.0 (see `LICENSE`). Quoted identifiers, key names and short excerpts from LPPL-licensed LaTeX sources are reproduced for reference; the sources themselves remain under their original licenses.
