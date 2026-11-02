# Layer 1: Syntax: LaTeX code markup and `\DocumentMetadata`

Scope: the author-facing input surface. It covers the `\DocumentMetadata` key/value interface and the markup keys an author must add because no algorithm can infer them: alternative text, artifact status, table headers, MathML intent and suspension points.

Evidence tags: **[S]** read in source/official docs (file named), **[R]** reproduced in this audit (file under `evidence/`), **[U]** under-documented runtime detail.

---

## A. Technical mechanics and data flow

### A.1 Entry point: kernel stub → support file

`\DocumentMetadata` is a kernel command (`ltmeta.dtx` v1.0h, 2026-09-15). The kernel holds only a stub. The working code is loaded from the `latex-lab` bundle so it can be updated without a format rebuild **[S: `base/ltmeta.dtx`]**:

```latex
% ltmeta.dtx (abridged, verbatim logic)
\let \IfDocumentMetadataTF \@secondoftwo          % default: "no DocumentMetadata"
\protected\def\DocumentMetadata{%
  \InputIfFileExists{documentmetadata-support.ltx}%
     {\let \IfDocumentMetadataTF \@firstoftwo ...}% flip conditionals
     {\@latex@error{No support files for \noexpand\DocumentMetadata found}...%
      \let\DocumentMetadata\@gobble}%
  \DocumentMetadata}                               % re-invoke the redefined command
```

`documentmetadata-support.ltx` (v1.1f, 2026-10-20, generated from `required/latex-lab/documentmetadata-support.dtx`) then redefines `\DocumentMetadata`. Its first and only legal call runs this fixed pipeline **[S: `documentmetadata-support.dtx`, implementation section; cross-checked against the installed 2026-11-01 `.ltx`]**:

| Step | Operation | Observable effect |
|---|---|---|
| 1 | `\cs_if_eq:NNTF \documentclass \@twoclasseserror` | Error `meta/after-class` if `\documentclass` already ran |
| 2 | `\@kernel@before@DocumentMetadata`: if `\Umathcode` is undefined (8-bit engine), `\fontencoding{T1}` and `\encodingdefault{T1}` | pdfTeX output switches from OT1/CM to T1/cm-super (`SFRM1000` …) **[R: `evidence/activation-modes.txt`]** |
| 3 | `\bool_gset_true:N \g__pdfmanagement_active_bool`; process key group `init` (`backend`); `\sys_load_backend:n{}` if no backend is loaded yet | l3backend is loaded *now*, so a non-detectable backend (`dvipdfmx`) must be given here |
| 4 | `\file_input:n {l3backend-testphase-<backend>.def}` | PDF-management backend layer (installed as `l3backend-pdf-*.def` + compatibility names) |
| 5 | `\pdf_version_gset:n {2.0}` | Default PDF 2.0 (since 2025-06-01) |
| 6 | Process key group `pdf` (`pdfversion`, `pdfstandard`) | Must precede tagpdf: "tagpdf currently requires that the pdf version is set first" |
| 7 | `\RequirePackage{latex-lab-testphase-latest}` | Loads `tagpdf` + 20 modules (list in Layer 4) |
| 8 | `\AddToDocumentProperties[document]{tagging}{inactive}` | Property `document/tagging` |
| 9 | Process all remaining keys (`\keys_set_exclude_groups:nnn … {init,pdf}`) | `tagging`, `lang`, `language`, `debug`, … |
| 10 | Language fallback: if `document/language` is empty, use `document/lang`, then update `\BCPdata` | Empty if neither key given |
| 11 | Append to `\@kernel@after@begindocument`: if Catalog `/Lang` is unset, take `\BCPdata{main.language}` or `\BCPdata{language}` | babel/polyglossia can supply `/Lang`; fallback `en`, applied silently since 2026-06-01 **[S: LaTeX News 43]** |
| 12 | `\hook_use_once:n {pdfmanagement/add}` | Flushes the queued `\pdfmanagement_add:nnn` calls |
| 13 | Redefine `\DocumentMetadata` to `\@latex@error{Two \DocumentMetadata declarations}` | Second call → error **[R: `! LaTeX Error: Two \DocumentMetadata declarations.`]** |
| 14 | Run `\g__pdfmanagement_testphase_tl` (extra modules), `\RequirePackage{pdfmanagement-firstaid}` | Patches for external packages |
| 15 | `\AddToHookNext{class/before}{\__tag_document_set_tagging:}` | `tagging=on` → `\tagpdfsetup{activate, para/tagging, activate/spaces}`; otherwise all `activate/*` false, `\tag_suspend:n{global}`, `\tagpdfsetup` becomes a no-op |

Consequence: since release 2025-11-01, **any** `\DocumentMetadata` call loads the complete tagging code base. `tagging=on/off/draft` only switches activation **[S: documentmetadata-support.dtx §2; LaTeX News 42 §"Expanding the \DocumentMetadata command"]**.

### A.2 Output produced per activation mode (pdfLaTeX, identical body) [R: `evidence/activation-modes.txt`]

| Input | PDF version | `/StructTreeRoot` | `/MarkInfo /Marked` | `BDC`/`BMC` on p.1 | XMP | `/Lang` | Font set |
|---|---|---|---|---|---|---|---|
| no `\DocumentMetadata` | 1.7 | no | no | 0/0 | no | – | CMR10, CMBX12 (OT1) |
| `\DocumentMetadata{}` | 2.0 | no | no | 0/0 | yes | `en` | SFRM1000, SFBX1440 (T1) + PdfTeX-Space |
| `tagging=off` | 2.0 | no | no | 0/0 | yes | `en` | same as above |
| `tagging=draft` | 2.0 | **no** | no | 4/2 | yes | `en` | same |
| `tagging=on` | 2.0 | yes | true | 4/2 | yes | `en` | same |
| `\RequirePackage{pdfmanagement}` + `pdfversion=1.7` | 1.7 | no | no | 0/0 | yes | – | CMR10, CMBX12 (OT1) |

`draft` keeps the marked-content operators and all tagpdf checks but suppresses tree output: it adds `\tagpdfsetup{activate/tree=false}` to hook `shipout/lastpage`, ordered before label `tagpdf`. The `PdfTeX-Space` font still appears with `off`, because the template `order` mechanism emits an explicit `\pdffakespace` after heading-number separators (`latex-lab-template.dtx`, `\__template_insert_collected_separators:`). Interword space glyphs are emitted only with `on` **[R+S]**.

### A.3 Content-level markup → downstream data

| Author markup | Consumed by | Emitted PDF data |
|---|---|---|
| `\includegraphics[alt={…}]` | hook `Gin/alt` → `latex-lab-graphic` sockets | `Figure` structure element with `/Alt` |
| `\includegraphics[actualtext={…}]` | hook `Gin/actualtext` | `Span` structure element with `/ActualText` **[R: `evidence/clipboard.txt`]** |
| `\includegraphics[artifact]` | hook `Gin/artifact` | `/Artifact` marked content, no structure element |
| same keys on `tikzpicture`, `\tikz`, `picture` | `latex-lab-tikz`, `latex-lab-graphic` | as above |
| `\tagpdfsetup{table/header-rows={1}}` | `latex-lab-table` | `TH` + attribute class `/TH-col` (`/O /Table /Scope /Column`) |
| `\tagpdfsetup{table/multirow=2}` *inside a cell* | `latex-lab-table` | attribute class `/rowspan-2` (`/RowSpan 2`); covered cell not emitted **[R: `evidence/06-08-table-semantics.txt`]** |
| `\MathMLintent{…}{…}`, `\MathMLarg{…}{…}` | luamml (LuaTeX only; no-ops otherwise) | `intent="…"`, `arg="…"` attributes in generated MathML |
| `\SuspendTagging{id}` / `\ResumeTagging{id}` | `lttagging` | stops/starts all `\UseTaggingSocket` plugs (the argument is a debug label only) |
| `\MathCollectFalse` / `\MathCollectTrue`, `\m@th` | `latex-lab-math` | disables math capture → no `Formula` |

---

## B. Core entities, commands, and specifications

### B.1 `\DocumentMetadata` keys (LaTeX 2026-11-01, documentmetadata-support v1.1f) [S]

| Key | Values | Key group | Effect / output artefact |
|---|---|---|---|
| `backend` | `dvipdfmx`, `dvips`, `dvisvgm`, `luatex`, `pdftex`, `pdfmode`, `xdvipdfmx`, `xetex` | `init` | `\sys_load_backend:n`; mandatory only when not auto-detectable |
| `pdfversion` | e.g. `1.7`, `2.0` | `pdf` | Overrides the 2.0 default (key defined in `l3pdfmeta`) |
| `pdfstandard` | `A-1b`,`A-2a`,`A-2b`,`A-2u`,`A-3a`,`A-3b`,`A-3u`,`A-4`,`A-4E`,`A-4F`; `X-4`,`X-4p`,`X-5g`,`X-5n`,`X-5pg`,`X-6`,`X-6n`,`X-6p`; `UA-1`,`UA-2` (case-insensitive, repeatable or list) | `pdf` | XMP identification (`pdfaid:*`, `pdfuaid:part`/`rev`); version min/max checks; A-standards add OutputIntent and suppress JavaScript actions. **Does not validate or enforce anything, and `UA-*` does not activate tagging** |
| `xmp` | boolean (initial `true`); sub-key `xmp/wtpdf={reuse=…, accessibility=…}` in l3pdfmeta | – | XMP packet on/off; WTPDF declarations |
| `colorprofiles` | ICC profile list | – | See `l3pdfmeta` |
| `uncompress` | none | – | `\pdf_uncompress:` (debug) |
| `language` | BCP 47, optional `[short]{full}` | – | `document/language` property, updates `\BCPdata`, sets `lang` |
| `other-languages` | comma list of BCP 47 tags | – | `document/other-languages` property only |
| `lang` | BCP 47 | – | Catalog `/Lang` + `document/lang` |
| `tagging` | `on`, `off`, `draft` | – | Activation switch (see A.2) |
| `tagging-setup` | any `\tagpdfsetup` key, plus `modules`, `extra-modules` | – | Implies `tagging=on` unless `tagging=off` is also present (either order: `off` wins); load-group keys (`modules`, `extra-modules`) run immediately, all other keys at `class/before` |
| `check-tagging-status` | none / `listfiles` / `off` | – | Reads `latex-tagging-status.ltx`, prints a compatibility report at `enddocument/info` |
| `testphase` | `latest`, `tagpdf`, `phase-I/II/III` (activate tagging only); standard module names (ignored: already loaded); any other name → input `<name>-latex-lab-testphase.ltx` | – | Legacy/developer key |
| `debug` | `para`, `log=…`, `uncompress`, `pdfmanagement` (deprecated), `firstaidoff={…}`, `xmp-export[=file]`, `tagpdf`, `BBox` | – | Debug switches; `tagpdf` loads `tagpdf-debug` |

### B.2 Content and package-author commands [S: `lttagging.dtx` v1.1a, LaTeX News 41–44]

| Command | Defined in | Role |
|---|---|---|
| `\IfDocumentMetadataTF`, `…T`, `…F` | kernel `ltmeta` | Test whether `\DocumentMetadata` ran |
| `\NeedsDocumentMetadata` | kernel (`latex.ltx`) | Error in a class/package that requires the new code (used by `ltx-talk.cls`) |
| `\IfPDFManagementActiveTF`, `…T`, `…F` | kernel (`latex.ltx`, default false branch) | Test for active PDF management |
| `\AddToDocumentProperties[label]{prop}{val}`, `\GetDocumentProperty{label/prop}` (expandable), `\ShowDocumentProperties`, `\LogDocumentProperties` | kernel `ltmeta` (2026-06-01) | Document-wide property store; label `document` reserved |
| `\SuspendTagging{…}`, `\ResumeTagging{…}`, `\tag_suspend:n`, `\tag_resume:n` | kernel `lttagging` | Global switch over tagging sockets |
| `\NewStructureName`, `\UseStructureName` (expandable), `\AssignStructureRole` | kernel `lttagging` | Symbolic tag names, role-mapped when tagging is loaded |
| `\MathMLintent`, `\MathMLarg` | kernel `lttagging` (no-op stubs) | MathML `intent`/`arg` |
| `\MathCollectTrue`, `\MathCollectFalse` | kernel stub; behaviour in `latex-lab-math` | Control math capture |
| `\RegisterMathEnvironment[arg-spec=…]{env}` / `\math_register_env:nn` | `latex-lab-math` | Declare top-level math environments for capture |
| `\dollardollar@begin`, `\dollardollar@end` | kernel (2025-06-01) | Mandatory replacement for literal `$$` in package code |
| `\tagpdfsetup{…}` | tagpdf | Runtime configuration; no-op after `tagging=off` |

### B.3 Production configurations (each compiled and validated in this audit)

```latex
% LuaLaTeX, MathML as structure elements -- examples/01-ua2-lualatex-mathml-se.tex
\DocumentMetadata{
  lang          = en-US,
  pdfstandard   = {UA-2, A-4F},
  tagging       = on,
  tagging-setup = {math/setup = mathml-SE},
}
\documentclass{article}
\usepackage{unicode-math}   % luamml loads automatically with unicode-math/lua-unicode-math
\title{...}                 % required: feeds XMP dc:title (ISO 14289-2:2024 8.11.1)
```

```latex
% pdfLaTeX, MathML read from an external file -- examples/03-ua2-pdflatex-external-mathml.tex
\DocumentMetadata{
  lang          = en-US,
  pdfstandard   = {UA-2, A-4F},
  tagging       = on,
  tagging-setup = {math/mathml/sources = {<jobname-of-lua-run>-luamml-mathml}},
}
```

```latex
% PDF management only, no tagging code -- examples/05-pdfmanagement-without-tagging.tex
\RequirePackage{pdfmanagement}
\SetKeys[document/metadata]{pdfversion=1.7, pdfstandard=a-3b}
\documentclass{article}
```

```bash
lualatex examples/01-ua2-lualatex-mathml-se.tex
```

```bash
verapdf -f ua2 --format text 01-ua2-lualatex-mathml-se.pdf
```

Result: `PASS … ua2` and `PASS … 4f` **[R: `evidence/01-ua2-lualatex-mathml-se.txt`]**.

---

## C. System interdependencies and package friction

### C.1 Cascades triggered by syntax-level choices

| Choice | Downstream cascade |
|---|---|
| Adding `\DocumentMetadata{}` (any keys) | T1 encoding on 8-bit engines changes glyphs, metrics and possibly line breaks; PDF 2.0 default; `hyperref` switches driver to `hgeneric-testphase.def` (vs `hpdftex.def`) **[R: logs]**; block, list, heading, float, table, toc, title and math internals are replaced by latex-lab code even with `tagging=off` |
| `pdfstandard=UA-2` without `\title` | veraPDF FAIL ISO 14289-2:2024 clause 8.11.1 (`dc:title` missing) **[R: benchmark run before `\title` was added]** |
| `pdfstandard=UA-2` combined with `pdfversion=1.7` | **Key order decides.** `pdfversion=1.7, pdfstandard=UA-2` → the standard raises the version to 2.0 (no message); `pdfstandard=UA-2, pdfversion=1.7` → output stays 1.7 and `Package pdf Warning: PDF version 1.7 is too low for standard 'pdf/UA-2'` is issued (a warning, not an error, despite the l3pdfmeta prose saying "error") **[R: `evidence/version-standard-conflict.txt`]** |
| `pdfstandard=A-4F` and no embedded file | l3pdfmeta embeds dummy `pdf-A4f.txt` (ISO 19005-4:2020 clause 6.9, `containsEmbeddedFiles`) **[S+R]** |
| `pdfstandard=UA-1` | Forces `math/alt/use=true` (Formula `/Alt` from source) at `begindocument/end` |
| `tagging=off` | `\tagpdfsetup` is redefined to a no-op at `class/before`, so later setup keys are silently ignored; `activate/spaces` is off unless restored via `tagging-setup={activate/spaces}` |
| `language=de-AT` | Sets `/Lang` and `\BCPdata` only; hyphenation patterns and babel strings are untouched |
| `\includegraphics` without `alt`/`actualtext`/`artifact` | `Figure` without `/Alt` → ISO 14289-2:2024 clause 8.2.5.28.2 failure |

### C.2 Markup patterns that collide with the tagging kernel

| Legacy pattern | Why it collides | Replacement |
|---|---|---|
| Literal `$$…$$` inside environment definitions | Display tagging must inject structure after the display but before `\postdisplaypenalty`; the kernel sets `\postdisplaypenalty=10000`, a negative `\belowdisplayskip`, and regains control via `\aftergroup`. Raw `$$` in package code defeats the capture | `\providecommand\dollardollar@begin{$$}` / `\providecommand\dollardollar@end{$$}` and use those **[S: LaTeX News 41]** |
| Verbatim inside `$…$`, `\[…\]`, `equation`, `equation*` | Math is now grabbed as an argument before typesetting (catcodes frozen) | Move verbatim out; `\MathCollectFalse` for non-formula math |
| `$` used for layout (`$\vcenter{…}$`, `$\langle$…`) | Every math shift is captured and becomes a `Formula` | `\m@th` inside the math (tagging disabled for that formula), `\MathCollectFalse`, or text glyphs (`\textlangle`) |
| Begin/end math hidden in separate macros (`>{$}c<{$}`) | Grabber cannot see the closing token | Supported in tabular via slower token-by-token grabbing (2025-11-01 fix) |
| `\begin{minipage}…$b$…` nested inside `$…$` | Grabber stops at the inner `$` | Wrap in an extra brace group **[S: latex-lab-math "Known current bugs"]** |
| `\DocumentMetadata` after `\documentclass` or twice | Backend/PDF-version decisions are already fixed | Single call, first line |

### C.3 Legacy vs tagged input contract

| Aspect | Legacy pipeline | Tagged pipeline (2026-11-01) |
|---|---|---|
| Required preamble | `\documentclass` | `\DocumentMetadata{…}` *before* `\documentclass` |
| Language | babel option only | `lang`/`language` key → `/Lang`; babel as fallback |
| Images | `\includegraphics{f}` | `alt=` or `artifact` mandatory for UA; `actualtext=` for symbols |
| Tables | layout only | header rows/columns declared; row spans declared per cell; layout tables need `table/tagging=presentation` |
| Math | any `$` usage | `$` is captured; MathML via luamml (LuaTeX) or external HTML (pdfTeX) |
| Standards | `pdfx`, `hyperxmp` packages | Loading of `pdfx`/`hyperxmp` suppressed; `pdfstandard` key **[R: `pdfmanagement Warning: The loading of package hyperxmp is disabled`]** |

---

## D. Verified primary sources

| Source | URL / location | Verification |
|---|---|---|
| `documentmetadata-support` documentation | https://texdoc.org/serve/documentmetadata-support/0 | PDF fetched (200, `%PDF-`) |
| Source: `documentmetadata-support.dtx` | https://github.com/latex3/latex2e/blob/develop/required/latex-lab/documentmetadata-support.dtx | cloned @ `2ec6765` |
| Kernel `ltmeta.dtx` (stub, document properties, link targets) | https://github.com/latex3/latex2e/blob/develop/base/ltmeta.dtx | cloned |
| Kernel `lttagging.dtx` (sockets, suspension, structure names) | https://texdoc.org/serve/lttagging/0 | PDF fetched |
| LaTeX News 41–44 (keys `tagging`, `tagging-setup`, `language`, single-call rule) | https://www.latex-project.org/news/latex2e-news/ltnews.pdf | fetched (200); sources `base/doc/ltnews4[1-4].tex` read |
| Tagging project usage instructions | https://latex3.github.io/tagging-project/documentation/usage-instructions | fetched (200); source `documentation/usage-instructions.md` @ `d0001d5` |
| `latex-lab-graphic` (alt/actualtext/artifact) | https://texdoc.org/serve/latex-lab-graphic/0 | fetched |
| `latex-lab-table` (header rows, multirow) | https://texdoc.org/serve/latex-lab-table/0 | fetched |
| CTAN `latex-lab` | https://ctan.org/pkg/latex-lab | fetched (200) |
