# Layer 5: Package ecosystem: CTAN dependency management and MathML conversion hooks

Scope: which distribution packages carry the tagging stack, how they resolve at runtime, how external packages are suppressed, emulated or patched, how compatibility is tracked, and how MathML gets from TeX math into the PDF on each engine.

Evidence tags: **[S]** source/official docs, **[R]** reproduced (`evidence/`), **[U]** under-documented.

---

## A. Technical mechanics and data flow

### A.1 Distribution map (TeX Live 2026, packages as installed 2026-11-02) [R: `tlmgr info`, logs]

| CTAN / TeX Live package | TL collection | Version observed | Contents relevant to tagging |
|---|---|---|---|
| `latex` (latex-base) | collection-latex | LaTeX 2026-11-01 PL0 | `\DocumentMetadata` stub, lthooks, ltpara, ltshipout, ltsockets, lttagging, ltproperties, ltmarks, lttemplates |
| `latex-lab` | collection-latex | `latex-lab-testphase-latest` v0.1h (2026-08-06); `documentmetadata-support` v1.1f (2026-10-20) | `\DocumentMetadata` implementation; all tagging modules |
| `tagpdf` | collection-latex | 1.0i (2026-10-29) | Structure/MC engine, Lua backend `tagpdf.lua` |
| `pdfmanagement` (CTAN page also exists under the legacy name `pdfmanagement-testphase`) | collection-latex | 0.97d (2026-10-06) | `l3pdfmeta`, `l3pdfdict`, `l3pdffile`, `l3pdfannot`, `l3pdftools`, `hgeneric-testphase.def`, `l3backend-pdf-*.def`, `pdfmanagement-firstaid` |
| `l3kernel` | collection-latex | L3 programming layer 2026-09-09 | `l3pdf`; core backend files `l3backend-<engine>.def` now ship inside `l3kernel` (`tex/latex/l3kernel/`) |
| `hyperref` | collection-latex | 7.01s (2026-10-09) | Uses `hgeneric-testphase.def` under PDF management |
| `luamml` | collection-luatex | file header v0.9.3 (2026-10-30); TL catalogue says 0.9.2 | TeX→MathML conversion (LuaTeX) |
| `luaotfload` | collection-luatex | 3.29 | OpenType loading, HarfBuzz mode, `pre_/post_shaping_filter` |
| `unicode-math` | collection-latexrecommended | 0.8r (2023/08/13) | Unicode math fonts; **trigger** for automatic luamml loading |
| `lua-unicode-math` | – | – | Alternative trigger for luamml |
| `latex-tagging-status` | **collection-latexextra** | rev. 80400 | `latex-tagging-status.ltx` data for `check-tagging-status` |
| `ltx-talk` | – | v0.6.10 cited in LaTeX News 44 | Tagging-native presentation class (beamer replacement) |

Consequence: schemes without `collection-latexextra` lack `latex-tagging-status`, and `check-tagging-status` then produces **no report and no error**, because the file is read with `\file_if_exist_input:n` **[S: documentmetadata-support.dtx]**.

Release channels: LaTeX ships twice a year (June/November). Pre-releases run as `pdflatex-dev` / `lualatex-dev`. latex-lab is a separate bundle so `\DocumentMetadata` code can change between kernel releases **[S]**. The tagging team says "It is _strongly_ recommended that you use a current LaTeX Release". As of 2026-06-11 Overleaf offered 2025-11-01 only with its rolling TeX Live option **[S: usage-instructions.md]**.

### A.2 Runtime dependency resolution (LuaLaTeX, `tagging=on`, unicode-math) [R: log of `examples/01-…`]

```text
\DocumentMetadata
 └─ documentmetadata-support.ltx
     ├─ pdfmanagement-init.sty ── color-ltx.sty, xcolor-patches-tmp-ltx.sty
     ├─ l3backend-luatex.def (l3kernel) + l3backend-pdf-luatex.def (pdfmanagement)
     ├─ latex-lab-testphase-latest.sty
     │    ├─ tagpdf.sty ── tagpdf-base, tagpdf-mc-code-lua, tagpdf-luatex.def, tagpdf.lua
     │    ├─ latex-lab-testphase-{names,new-or-2,template,block,context,sec,sec-template,toc,
     │    │      minipage,graphic,float,bib,text,marginpar,title,table,math,firstaid,tikz}
     │    └─ latex-lab-kernel-changes.sty
     └─ pdfmanagement-firstaid.sty
\usepackage{unicode-math}  ──►  (math module) luamml.sty + luamml-patches-kernel + luamml-patches-amsmath
\usepackage{hyperref}      ──►  hgeneric-testphase.def   (legacy path: hpdftex.def / hluatex.def)
\usepackage{enumitem}      ──►  latex-lab-enumitem.sty   (package itself not loaded)
\usepackage{hyperxmp|pdfx} ──►  loading disabled ("Package pdfmanagement Warning: The loading of package hyperxmp is disabled.")
```

### A.3 Mechanisms for handling external packages

| Mechanism | Implementation | Packages (verified in source/log) |
|---|---|---|
| Suppression | pdfmanagement disables loading and warns | `hyperxmp`, `pdfx` **[R/S: status DB "loading is suppressed"]** |
| Emulation | latex-lab provides the user interface; the original package is not loaded | `enumitem` (`latex-lab-enumitem` v0.80i), `titlesec` (`latex-lab-titlesec`, partial) |
| First aid (latex-lab) | `package/<name>/after` hooks in `latex-lab-firstaid` | `apacite`, `booktabs`, `cleveref`, `fancyvrb`, `float`, `fncychap`, `listings`, `pgfrcs` **[S: `latex-lab-firstaid.dtx`]** |
| First aid (pdfmanagement) | `package/<name>/after` hooks in `pdfmanagement-firstaid` | `colorspace`, `doclicense`, `xcolor` **[S]**; disable selectively with `debug={firstaidoff={…}}` |
| Driver swap | hyperref driver chosen by PDF-management state | `hgeneric-testphase.def` vs `hpdftex.def` **[R]** |
| Package-side support | Package implements sockets/tags itself (`supported-through: package` in status DB) | e.g. `ltx-talk`, `lualineno` |

### A.4 Compatibility tracking

- Database: `_data/tagging-status.yml` in `latex3/tagging-project`, with fields `name`, `type`, `status`, `included-in` (tlc3, arxiv10…arxiv001, ol…), `priority`, `comments`, `issues`, `luatex-only`, `supported-through`, `tests`, `updated`. Status values: `compatible`, `partially-compatible`, `currently-incompatible`, `no-support`, `unchecked` **[S]**.
- Snapshot @ `d0001d5` (2026-11-01): **2,246** entries, of which 1,136 compatible, 322 partially-compatible, 522 currently-incompatible, 62 no-support, 204 unchecked **[R: counted from YAML]**.
- Runtime check: `\DocumentMetadata{check-tagging-status}` reads `latex-tagging-status.ltx`, which defines `\t@status@<file.ext>` chardefs (0 unknown, 1 unsupported, 2 currently-incompatible, 3 partially-compatible, 4 compatible). At `begindocument` it snapshots `\@filelist`; at `enddocument/info` it prints classes A/B with sections 1–6. Team-internal files are hidden by assigning status 8 **[S+R: `evidence/09-check-tagging-status.txt`]**.

### A.5 MathML conversion: LuaTeX route (luamml)

Loading condition: `lualatex` + `unicode-math` or `lua-unicode-math` → luamml loaded automatically. Force with `math/mathml/luamml/load=true` or `math/mathml/luamml/write=true` ("various symbols [may be] mapped to the wrong Unicode code points") **[S: latex-lab-math §luamml]**.

Conversion hooks **[S: luamml source @ `c5b9bb0`]**:

| Hook | Registered by | Function |
|---|---|---|
| `pre_mlist_to_hlist_filter` → `luamml.to_mathml` | `luamml-tex.lua` | Converts the noad list to an XML table per formula |
| `pre_mlist_to_hlist_filter` → `luamml.mathflatten` (rule: after `luamml.to_mathml`) | `luamml-mathflatten.lua` | Flattens math groups |
| `hpack_filter` | `luamml-table.lua` | Table/alignment cell collection |
| Lua-bound control sequences (`token.set_lua`): `\__luamml_amsmath_*`, `\__luamml_array_*`, `\luamml_get_last_mathml_stream:e`, `\RegisterFamilyMapping`, `\RegisterTextFamily` | `luamml-amsmath.lua`, `luamml-array.lua`, `luamml-tex.lua` | amsmath/array integration via tagging sockets `math/luamml/*` |

Output channels:

| `math/setup` value | Data written | Passes needed | PDF artefact |
|---|---|---|---|
| `mathml-SE` | structure elements emitted directly by `luamml-structelemwriter.lua` through tagpdf | 1 | `Formula` → `math` → `mi`/`mo`/`mn`/`msup`… in the MathML namespace **[R]** |
| `mathml-AF` (LuaLaTeX default with unicode-math) | `<jobname>-luamml-mathml.html` written, read back at `begindocument` on the next run | 2 | Embedded file `mathml-N.xml`, `/AFRelationship /Supplement`, `/Subtype /application/mathml+xml` on `Formula` **[R: pass 1 has no MathML AF, pass 2 attaches 2]** |
| `tex-AF` | captured TeX source | 1 | `tag-AFfileN.tex`, `/AFRelationship /Source`, `/Subtype /application/x-tex` |
| `mathml-MS` | MathML copied into an `/O /MSFT_Office` attribute `MSFT_MathML` (Word convention); requires the exported file (2 passes) | 2 | attribute on `Formula` |

### A.6 MathML conversion: pdfTeX route (external MathML)

```latex
\DocumentMetadata{tagging=on,
  tagging-setup={math/mathml/sources={mydoc-luamml-mathml, curated}}}  % *.html files, extension implicit
% or produce a template with empty <math> elements to fill in:
\tagpdfsetup{math/mathml/write-dummy}     % writes \jobname-mathml-dummy.html
```

File format (parsed with special catcodes; the keywords are delimiters) **[S: latex-lab-math §Associated Files; R: generated file]**:

```html
<div>
 <h2>\mml 1</h2>
 <p>$a^2+b^2=c^2$</p>
 <p>E1AFFC0A43F89AE2DA953B789D29E42C</p>        <!-- \str_mdfive_hash:n of the captured source -->
 <math xmlns="http://www.w3.org/1998/Math/MathML"> … </math>
</div>
```

Matching rules: lookup by MD5 hash; the first matching snippet wins; `\jobname-mathml.html` is read before `\jobname-luamml-mathml.html`, so curated MathML overrides generated MathML. **All** snippets in the listed files are embedded, used or not. Disable globally with `math/mathml/sources=` and locally with `math/mathml/AF=false`. Cross-engine reuse works: `examples/03` (pdfLaTeX) attached both MathML AFs produced by LuaLaTeX (`2 MathML AF attached`) and validated UA-2/A-4f **[R: `evidence/03-ua2-pdflatex-external-mathml.txt`]**.

### A.7 Semantic enrichment hook: MathML intent

```latex
\newcommand\abs[1]{\MathMLintent{absolute-value($x)}{{\lvert\MathMLarg{x}{{#1}}\rvert}}}
% luamml output for \abs{y}:
% <mrow intent="absolute-value($x)"><mo>|</mo><mi arg="x">y</mi><mo>|</mo></mrow>
```

`\MathMLintent`/`\MathMLarg` are kernel no-ops (they expand to argument 2) unless luamml redefines them **[S: LaTeX News 42; lttagging.dtx]**.

---

## B. Core entities, commands, and specifications

| Entity | Kind | Role |
|---|---|---|
| `latex-lab-testphase-latest.sty` | package | Module manifest loaded by every `\DocumentMetadata` |
| `<module>-latex-lab-testphase.ltx` | file convention | Extra/experimental modules via `testphase=<module>` or `tagging-setup={extra-modules=…}` (e.g. `verbatim-af`, `verbatim-alt`, `verbatim-mo`) |
| `latex-tagging-status.ltx` | data file | Runtime compatibility table |
| `\t@status@<file>` | chardef | Status code per file |
| `<jobname>-luamml-mathml.html` | aux file (luamml) | Generated MathML store, keyed by hash |
| `<jobname>-mathml.html` | user file | Curated MathML, read first |
| `<jobname>-mathml-dummy.html` | aux file | Template from `math/mathml/write-dummy` |
| `mathml-N.xml`, `tag-AFfileN.tex` | embedded file names | MathML / TeX-source associated files |
| `latex-list-css.html`, `latex-align-css.html` | tagpdf-supplied files | CSS for PDF→HTML derivation (`attach-css`) |
| MathML statistic block in log | diagnostic | `MathML fragments read / different / math fragments found / fitting MathML AF found / MathML AF attached` |
| `\RegisterFamilyMapping`, `\RegisterTextFamily` | luamml commands | Map legacy math families to Unicode for conversion |

Installation commands for the full stack:

```bash
tlmgr install latex latex-lab tagpdf pdfmanagement l3kernel hyperref luamml luaotfload unicode-math latex-tagging-status
```

```bash
tlmgr info --only-installed latex-tagging-status
```

---

## C. System interdependencies and package friction

### C.1 High-usage packages with tagging friction (status DB @ `d0001d5`, filtered to entries flagged in TLC3 or the arXiv usage top-1%/5%)

| Package | Status | Recorded reason (verbatim or condensed from `comments`) |
|---|---|---|
| `beamer` | no-support | "Internals rebox material repeatedly in a way that is not compatible with any form of tagging support." Use `ltx-talk` |
| `tabu` | no-support | "broken and not maintained" |
| `mdframed` | no-support | Replace with `tcolorbox` |
| `pdfx`, `hyperxmp` | no-support | Incompatible with PDF management; loading suppressed |
| `axessibility` | no-support | "Loads tagpdf but it is not compatible with the current version. Its patches clash with the math module." |
| `soul` | currently-incompatible | Nested `P` structures (reproduced, Layer 2) |
| `ulem` | currently-incompatible | "Missing TextDecoration attributes" |
| `multirow` | currently-incompatible | No RowSpan semantics (reproduced, Layer 2) |
| `caption`, `subcaption`, `subfig` | currently-incompatible | Configuration overwritten; `hyperref` + `\listoffigures` issue |
| `wrapfig` | currently-incompatible | "Generated tagging structure is incorrect" |
| `lineno` | currently-incompatible | "Loses line numbers …"; use `lualineno` |
| `pdfpages` | currently-incompatible | "duplicated structures as graphics are processed more than once in trial typesetting" |
| `minted` | currently-incompatible | Listings not tagged; see `fvextra`/`fancyvrb` |
| `pstricks` | currently-incompatible | "dvips cannot properly support PDF/UA" |
| `pgfplots` | currently-incompatible | Depends on tikz, needs own adjustments |
| `tikz-cd`, `xy` | currently-incompatible | `xy`: no tagging for `\xymatrix` in text mode, no luamml support |
| `varwidth`, `threeparttable`, `todonotes`, `framed`, `sidecap`, `ltablex`, `titletoc`, `minitoc`, `authblk`, `arydshln` | currently-incompatible | various (see DB) |
| `memoir`, `scrartcl`, `scrbook`, `llncs`, `IEEEtran`, `revtex4-1` | currently-incompatible (classes) | Class internals |
| `hyperref` | partially-compatible | "Forms not supported; use l3pdffield-testphase for forms" |
| `amsmath`, `amsthm`, `amsart`, `amsbook` | partially-compatible | AMS `\maketitle`/`\@starttoc` adjustments |
| `mathtools` | partially-compatible | "`mathic` key produces errors … luamml support is limited to `multlined` and `\shortintertext`" |
| `newtxmath`, `mathptmx`, `stmaryrd` | partially-compatible | "Incompatible with unicode-math. No luamml support." |
| `biblatex` | partially-compatible | "Requires hyperref" |
| `cleveref` | partially-compatible | "Must be loaded after amsmath" |
| `tcolorbox` | partially-compatible | "Simple boxes are now tagged correctly" |
| `listings` | partially-compatible | Style auto-loaded since 2026-11-01; open: frames, `mathescape`, brace-delimited `\lstinline` |
| `enumitem`, `titlesec` | partially-compatible | Emulated, not loaded |
| `tikz` | partially-compatible | Tagging code defines `alt`/`actualtext`/`artifact` keys |
| `tabularx`, `longtable`, `booktabs`, `multicol`, `geometry`, `fancyhdr`, `microtype`, `siunitx`, `natbib`, `xcolor`, `fontspec`, `unicode-math`, `lua-unicode-math`, `luamml` | compatible | (`booktabs` needs firstaid; `longtable` page breaks with pdfTeX need latex-dev ≥ 2025-02) |

### C.2 Math-specific friction

| Problem | Cause | Effect |
|---|---|---|
| No MathML from math packages without luamml mappings (`abraces`, `amscd`, `bigdelim`, `breqn`, `chemfig`, parts of `mathtools`) | luamml has no conversion rules for their constructs/fonts | No or wrong MathML; `chemfig` "Figures are tagged as a sequence of many formulas" |
| Legacy 8-bit math fonts with LuaTeX | luamml maps families to Unicode only via `unicode-math` or `\RegisterFamilyMapping` | Wrong code points |
| Edited formula, stale external MathML (pdfTeX) | MD5 of source changes | AF silently not attached; check "fitting MathML AF found" count |
| Numbered equations with MathML AF | `Lbl` substructure ignored when MathML is used (Foxit behaviour, github.com/foxitsoftware/PDF_UA-2) | Put the number inside the MathML |
| Consumers reading only the first AF | Foxit sees only `AF[0]` | latex-lab places MathML before the TeX source in `/AF` **[S+R]** |
| `math/mathml/structelem` with PDF < 2.0 | MathML namespace needs PDF 2.0 | Warning `tag/PDF-2.0-recommended` |

### C.3 Legacy vs tagged dependency footprint

| Aspect | Legacy | Tagged (2026-11-01) |
|---|---|---|
| Files loaded for empty `article` | class + size file | + pdfmanagement, l3backend-pdf, tagpdf, 20 latex-lab modules, firstaid layers |
| Package policy | any package loads | suppress / emulate / patch / report |
| hyperref driver | `hpdftex.def` / `hluatex.def` | `hgeneric-testphase.def` |
| Math | typesetting only | capture + MathML (LuaTeX automatic; pdfTeX external) + TeX-source AF |
| Compatibility knowledge | none | status DB (2,246 entries) + `check-tagging-status` |

---

## D. Verified primary sources

| Source | URL / location | Verification |
|---|---|---|
| CTAN: tagpdf | https://ctan.org/pkg/tagpdf | fetched (200) |
| CTAN: latex-lab | https://ctan.org/pkg/latex-lab | fetched (200) |
| CTAN: pdfmanagement / pdfmanagement-testphase | https://ctan.org/pkg/pdfmanagement ; https://ctan.org/pkg/pdfmanagement-testphase | fetched (200) |
| CTAN: luamml | https://ctan.org/pkg/luamml ; doc https://texdoc.org/serve/luamml/0 | fetched |
| CTAN: latex-tagging-status | https://ctan.org/pkg/latex-tagging-status | fetched (200) |
| CTAN: unicode-math, lua-unicode-math, luaotfload, ltx-talk, hyperref, l3kernel, l3backend | https://ctan.org/pkg/unicode-math ; https://ctan.org/pkg/lua-unicode-math ; https://ctan.org/pkg/luaotfload ; https://ctan.org/pkg/ltx-talk ; https://ctan.org/pkg/hyperref ; https://ctan.org/pkg/l3kernel ; https://ctan.org/pkg/l3backend | fetched (200) |
| ltx-talk repository | https://github.com/josephwright/ltx-talk | verified (`git ls-remote`) |
| Tagging status (web table) and data | https://latex3.github.io/tagging-project/tagging-status/ ; https://github.com/latex3/tagging-project | fetched; cloned @ `d0001d5` |
| Usage instructions (math, MathML file format, Overleaf note) | https://latex3.github.io/tagging-project/documentation/usage-instructions | fetched (200) |
| WTPDF/STEM examples from the tagging project | https://latex3.github.io/tagging-project/documentation/wtpdf/ | fetched (200) |
| Foxit PDF/UA-2 MathML proof of concept (AF processing rules) | https://github.com/foxitsoftware/PDF_UA-2 | verified (`git ls-remote`); README fetched via raw.githubusercontent.com |
| TeX Live | https://www.tug.org/texlive/ | fetched (200) |
