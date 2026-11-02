# Layer 4: Format kernel: the LaTeX3 core framework and macro interception architecture

Scope: the mechanisms inside the `latex.ltx` format, and the `latex-lab` modules loaded on top of it, that let tagging code intercept document processing: hooks, paragraph hooks, sockets and plugs, templates and instances, properties, marks, math capture, and the API stack from the tagging calls down to `l3pdf` and the backend.

Evidence tags: **[S]** source/official docs, **[R]** reproduced (`evidence/`), **[U]** under-documented.

---

## A. Technical mechanics and data flow

### A.1 Layered call stack

```text
 document / class / package code
        │  (unchanged user commands: \section, \item, $…$, \includegraphics …)
        ▼
 ┌─ interception ───────────────────────────────────────────────────────────┐
 │ lthooks: cmd/…, env/…, package/…, class/before, begindocument, shipout/… │
 │ ltpara:  para/before, para/begin, para/end, para/after                  │
 │ ltsockets + lttagging: \UseTaggingSocket{<name>} → tagsupport/<name>    │
 │ lttemplates (+ latex-lab-template, -context): \UseInstance{type}{inst}  │
 │ latex-lab-math: math capture via \everymath/\everydisplay, env grabbing │
 └──────────────────────────────────────────────────────────────────────────┘
        ▼
 tagpdf API (expl3):  \tag_struct_begin:n  \tag_mc_begin:n  \tag_mc_end:  \tag_struct_end:
                      \tag_struct_use:n  \tag_mc_end_push:  \tag_mc_begin_pop:n  …
        ▼
 PDF management (pdfmanagement bundle / l3pdf*): \pdfmanagement_add:nnn, \pdfdict_put:nnn,
                      \pdf_object_new:n, \pdf_object_ref:n, \pdf_bdc:nn, \pdf_bmc:n, \pdf_emc:,
                      \pdf_bdc_shipout:ee, \pdffile_embed_stream:nnN (AF)
        ▼
 l3backend (l3backend-<engine>.def + l3backend-pdf-<engine>.def): \pdfliteral / \pdfextension / pdf.* Lua
        ▼
 engine (Layer 3)
```

`tagpdf-base.sty` ("provide base, no-op versions") keeps the tagging API callable when tagging is inactive. Packages can call `\tag_mc_begin:n` unconditionally **[R: log line `Package: tagpdf-base 2026-10-29 v1.0i part of tagpdf - provide base, no-op versions`]**.

### A.2 Interception primitives in the kernel

| Mechanism | Kernel file (introduced) | How tagging uses it |
|---|---|---|
| Generic hooks with rules/labels: `\AddToHook{name}[label]{code}`, `\DeclareHookRule{hook}{l1}{before\|after}{l2}`, `\AddToHookNext`, `\hook_gput_code:nnn` | `lthooks.dtx` (2020-10-01) | `class/before` activation switch; `begindocument` label `tagpdf/mc` for Lua callback registration; `shipout/lastpage` with rule `tagging-setup` before `tagpdf` for `tagging=draft`; `Gin/alt`, `Gin/actualtext`, `Gin/artifact` (2025-11-01) |
| Command/environment hooks: `cmd/<cs>/before\|after`, `env/<env>/begin\|end\|before\|after` | `ltcmdhooks.dtx` | Package patches without redefinition; `env/<math env>/end` fixed for math environments in 2026-11-01 |
| Paragraph hooks: `para/before`, `para/begin`, `para/end`, `para/after` | `ltpara.dtx` (2021-06-01) | Automatic `semantic-para`/`text-block` structures (sockets `para/semantic/*`, `para/textblock/*`) |
| Shipout: `\ShipoutBox`, hooks `shipout/before`, `/background`, `/foreground`, `/firstpage`, `/lastpage`; LuaTeX `pre_shipout_filter` callback | `ltshipout.dtx` (2020-10-01) | Lua-mode MC injection; tree written at `shipout/lastpage` |
| Sockets/plugs: `\NewSocket{name}{nargs}`, `\NewSocketPlug`, `\AssignSocketPlug`, `\UseSocket`, `\ShowSocket`, `\DebugSocketsOn` | `ltsockets.dtx` (2023-11-01) | Base for tagging sockets |
| Tagging sockets: `\NewTaggingSocket{name}{nargs}`, `\NewTaggingSocketPlug`, `\AssignTaggingSocketPlug`, `\UseTaggingSocket`, `\UseExpandableTaggingSocket`, `\tag_socket_use:n/nn/nnn` | `lttagging.dtx` (v1.1a, 2026-08-19) | Every tagging insertion point in kernel and latex-lab |
| Templates: `\NewTemplateType`, `\DeclareTemplateInterface`, `\DeclareTemplateCode`, `\DeclareInstance`, `\DeclareInstanceCopy`, `\UseInstance`, `\EditInstance` | `lttemplates.dtx` (+ `latex-lab-template`) | Block/list/heading/theorem layouts with built-in tagging; `order` key mechanism (2026-11-01) |
| Contexts: instance lookup `inst:<primary>:<secondary>` → `inst:<primary>` → `inst` | `latex-lab-context` | Context-sensitive layouts (footnote, float, caption, header/footer; font-size tertiary context) |
| Properties: `\property_new:nnnn`, `\property_record:nn`, `\property_ref:nn` | `ltproperties.dtx` (2023-06-01) | Cross-run data for generic-mode MCIDs, structure numbers, math-style detection |
| Marks: mark classes, `\mark_insert:nn`, `\mark_use_*` | `ltmarks.dtx` (2022-06-01) | Detecting MC chunks split by page breaks (generic mode) |
| Document properties: `\AddToDocumentProperties`, `\GetDocumentProperty` | `ltmeta.dtx` (2026-06-01) | `document/tagging`, `document/language`, `document/pdfstandard-UA` (`{2}{2024}`) |
| Link targets: `\MakeLinkTarget`, `\LinkTargetOn/Off`, `\NextLinkTarget`, hook `target/setname/after`, socket `recordtarget` | `ltmeta.dtx` (tagging support 2024-09-21) | Structure destinations and reference structures |

### A.3 Tagging socket semantics [S: `lttagging.dtx`]

- A tagging socket is an ordinary socket named `tagsupport/<name>`. `\NewTaggingSocket{name}{n}` creates it, defines the plugs needed for the suspended state, and assigns a default plug.
- `\UseTaggingSocket{name}` behaves like `\UseSocket{tagsupport/name}`, except that `\SuspendTagging` flips a boolean instead of re-plugging every socket. While suspended:
  - 0-argument sockets: no-op;
  - 1-argument sockets: gobble the argument;
  - 2-argument sockets: drop argument 1 and **pass argument 2 through** (changed 2024-11-01, GitHub latex2e #1500), so a socket can wrap content that must still be typeset.
- `\UseExpandableTaggingSocket` exists for `\halign` contexts; it emits no debug output.

Kernel-declared tagging sockets (`lttagging.dtx`, `{name}{nargs}`):

```text
mc{2} struct-mc{2} struct/begin{1} struct/end{0} inline/begin{1} inline/end{0}
para/on{0} para/off{0} para/restore{0} para/begin{0} para/end{0}
para/semantic/begin{1} para/semantic/end{1} para/textblock/begin{1} para/textblock/end{0}
recordtarget{0} minipage/before{0} minipage/after{0} parbox/before{0} parbox/after{0}
block/list/label{2} block/recipe{1}
sec/begin{1} sec/end{1} sec/title/begin{1} sec/title/end{0} sec/title/hang{2} sec/title/init{1}
sec/title/split{0} sec/title/runin/number{2} sec/title/number{2}
toc/contentsline/before{1} toc/contentsline/after{1} toc/starttoc/before{1} toc/starttoc/after{1}
toc/leaders/before{0} toc/leaders/after{0} marginpar/begin{0} marginpar/end{0}
tbl/cell/begin{0} tbl/cell/end{0} tbl/row/begin{0} tbl/row/end{0} tbl/pcell/begin{0} tbl/pcell/end{0}
tbl/init{0} tbl/init/celldata{0} tbl/finalize{0} tbl/restore/celldata{0} tbl/colspan{1}
tbl/hmode/begin{0} tbl/hmode/end{0} tbl/vmode/begin{0} tbl/vmode/end{0}
tbl/longtable/init{0} tbl/longtable/finalize{0} tbl/longtable/head{0} tbl/longtable/foot{0}
tbl/leaders/begin{0} tbl/leaders/end{0}
float/hmode/begin{0} float/hmode/end{0} float/begin{0} float/end{0}
caption/begin{1} caption/end{0} caption/label/begin{0} caption/label/end{0}
build/page/header{2} build/page/footer{2} build/column/outputbox{0} build/column/footins{0}
math/inline/begin{0} math/inline/end{0} math/inline/formula/begin{2} math/inline/formula/end{0}
math/display/begin{0} math/display/end{0} math/display/formula/begin{2} math/display/formula/end{0}
math/display/tag/begin{0} math/display/tag/end{0}
math/luamml/…{…}  (17 luamml sockets: save, annotate, array, mtable, hbox, artifact …)
```

Additional latex-lab sockets include `graphic/begin{1}`, `graphic/end{2}`, `graphic/init{1}`, `fnmark{2}`, `fntext/*`, `bib/cite/before{1}`, `float/H/*`, `tikz/picture/*`, `math/content{0}`, `math/struct/begin|end{0}`, `math/mathml/write{0}`, `lstlisting/number{2}`, `rule{2}`.

### A.4 Modules loaded by `\DocumentMetadata` (`latex-lab-testphase-latest.sty` v0.1h, 2026-08-06) [S+R]

```latex
\RequirePackage{tagpdf}
\RequirePackage{latex-lab-testphase-names}         % tag names
\RequirePackage{latex-lab-testphase-new-or-2}      % output-routine sockets, para tagging, new footnotes
\RequirePackage{latex-lab-testphase-template}      % lttemplates extensions (order key …)
\RequirePackage{latex-lab-testphase-block}         % lists + display blocks via templates
\RequirePackage{latex-lab-testphase-context}       % typesetting contexts
\RequirePackage{latex-lab-kernel-changes}          % generated by sec, toc
\RequirePackage{latex-lab-testphase-sec}           % headings
\RequirePackage{latex-lab-testphase-sec-template}  % template-based headings (\@startsection, \secdef)
\RequirePackage{latex-lab-testphase-toc}
\RequirePackage{latex-lab-testphase-minipage}
\RequirePackage{latex-lab-testphase-graphic}
\RequirePackage{latex-lab-testphase-float}
\RequirePackage{latex-lab-testphase-bib}
\RequirePackage{latex-lab-testphase-text}
\RequirePackage{latex-lab-testphase-marginpar}
\RequirePackage{latex-lab-testphase-title}
\RequirePackage{latex-lab-testphase-table}
\RequirePackage{latex-lab-testphase-math}
\RequirePackage{latex-lab-testphase-firstaid}
\RequirePackage{latex-lab-testphase-tikz}
```

The modules redefine core internals: `\@startsection`, `\secdef` (limited), list internals and `\item`, `\@makecaption`/float internals, `\@starttoc`/`\contentsline`, `\maketitle`, `\parbox`/`minipage`, the output routine via sockets (`build/page/*`, `build/column/*`), footnotes, the `tabular`/`array` preamble builders and math entry points. Since 2026-06-01 the kernel no longer uses "fake math" for `\textsuperscript`, `\textsubscript`, `\parbox`, `minipage` or around `tabular`; `\showoutput` regression baselines change (`\mathon`/`\mathoff` pairs removed) **[S: LaTeX News 43]**.

### A.5 Math capture (macro interception of `$`)

```text
$ \( \[ $$  → \everymath / \everydisplay hook → if \l__math_collected_bool is false: grab tokens up to the closing delimiter
registered environments (equation, equation*, align, …) → grab body (always)
captured tokens → \math_processor:n (debug) → socket math/inline|display/formula/begin {…}{<math>}
              → math/content (Alt/ActualText/AF source vars) → math/struct/begin (\tag_struct_begin:n{tag=Formula …})
              → re-insert tokens for typesetting → [LuaTeX: luamml pre_mlist_to_hlist_filter]
              → math/struct/end
```

- Grabbing happens at the **outermost** level: `\[ a \in A \text{ for all $a<5$} \]` is captured once **[S: latex-lab-math §Introduction]**.
- Opt-outs: `\m@th` inside captured math skips processing (fake math); `\m@th` before `$` sets the collected flag (no capture for that group); `\MathCollectFalse`/`\MathCollectTrue`; `\SuspendTagging{}`.
- Registration of new top-level math environments: `\RegisterMathEnvironment[arg-spec=…]{env}` / `\math_register_env:nn`. The MathML-AF hash is `\str_mdfive_hash:n` of the captured source.
- Display tagging under pdfTeX: `\postdisplaypenalty=10000` and a negative `\belowdisplayskip` locally, control regained via `\aftergroup`, then the structure is inserted and the real penalty/space re-added **[S: LaTeX News 41]**.

---

## B. Core entities, commands, and specifications

### B.1 tagpdf structure/MC API (expl3 and document level)

| expl3 | Document level | Purpose |
|---|---|---|
| `\tag_struct_begin:n {…}`; keys: `tag`, `stash`, `label`, `parent`, `firstkid`, `title`, `title-o`, `alt`, `actualtext`, `lang`, `ref`, `E`, `AF`, `AFref`, `AFinline`, `AFinline-o`, `texsource`, `mathml`, `attribute`, `attribute-class` | `\tagstructbegin{…}` | Open structure element |
| `\tag_struct_end:` / `\tag_struct_end:n {tag}` | `\tagstructend` | Close |
| `\tag_struct_use:n {label}`, `\tag_struct_use_num:n {n}` | `\tagstructuse{…}` | Insert a structure created elsewhere |
| `\tag_mc_begin:n {…}`; keys: `tag`, `artifact[=pagination\|pagination/header\|pagination/footer\|layout\|page\|background\|notype]`, `stash`, `label`, `alt`, `actualtext`, `lang`, `raw` | `\tagmcbegin{…}` | Open marked content (no group, not nestable) |
| `\tag_mc_end:` | `\tagmcend` | Close MC |
| `\tag_mc_end_push:` / `\tag_mc_begin_pop:n` | – | Interrupt/resume the current MC (used by inline math sockets) |
| `\tag_suspend:n {id}` / `\tag_resume:n {id}` | `\SuspendTagging{id}` / `\ResumeTagging{id}` | Suspend all tagging-socket output |
| `\tag_spacechar_on:` / `\tag_spacechar_off:` | – | Toggle real spaces (pdfTeX: `\pdfinterwordspaceon/off`) |
| `\tagpdfsetup{…}` | same | Configuration (Layer 2 B.2) |

### B.2 PDF management API used by tagging [S: `pdfresources/*.dtx`, `l3kernel/l3pdf.dtx`]

| Function | Package | Role |
|---|---|---|
| `\pdf_object_new:n`, `\pdf_object_write:nnn`, `\pdf_object_ref:n`, `\pdf_object_new_indexed:nn` (2024-04-01), `\pdf_object_ref_last:` | l3kernel `l3pdf` | Object allocation for StructElem, namespaces, ParentTree |
| `\pdf_version_gset:n`, `\pdf_version_compare:NnTF`, `\pdf_uncompress:` | l3kernel `l3pdf` | Version control (`\DocumentMetadata` sets 2.0) |
| `\pdf_bdc:nn`, `\pdf_bmc:n`, `\pdf_emc:`, `\pdf_bdc_shipout:ee`, `\pdf_bdcobject:nn` | pdfmanagement `l3pdftools` | Marked-content operator emission (generic mode, ActualText spans) |
| `\pdfmanagement_add:nnn {Catalog}{Lang}{(…)}`, `\pdfdict_put:nnn` | pdfmanagement `l3pdfmanagement`, `l3pdfdict` | Catalog/page/StructTreeRoot dictionary entries |
| `\pdf_string_from_unicode:nnN {utf16/hex}{…}\l_tmpa_tl` | pdfmanagement `l3pdftools` | Encode `/ActualText`, `/Alt` strings |
| `\pdffile_embed_stream:nnN` | pdfmanagement `l3pdffile` | Embedded-file streams for associated files (MathML, TeX source, CSS) |
| `\pdfmeta_standard_verify:n…`, `\pdfmeta_xmp_add_declaration:nnnnn` | pdfmanagement `l3pdfmeta` | Standard requirement checks, XMP declarations |

### B.3 Package-author recipe (tagging-aware code)

```latex
% Wrap trial typesetting so no structures are created for discarded material
\SuspendTagging{mypkg/measure}
\settowidth\mylen{<material>}
\ResumeTagging{mypkg/measure}

% Provide a tagging insertion point instead of hard-coding tagpdf calls
\NewTaggingSocket{mypkg/box/begin}{0}
\NewTaggingSocketPlug{mypkg/box/begin}{default}
  {\tag_struct_begin:n{tag=\UseStructureName{mypkg/box}}}
\AssignTaggingSocketPlug{mypkg/box/begin}{default}
% ... in the implementation:
\UseTaggingSocket{mypkg/box/begin}

% Display math in environment definitions
\providecommand\dollardollar@begin{$$}
\providecommand\dollardollar@end{$$}

% Only for the new code paths
\IfDocumentMetadataTF{<tagging-aware path>}{<legacy path>}
```

---

## C. System interdependencies and package friction

### C.1 Why internal-patching packages collide

latex-lab replaces kernel internals with template- and socket-based implementations. A package that captures an internal with `\let` (or patches it with `etoolbox`/`xpatch`) before or after latex-lab loads either loses its change or reinstates legacy code that has no tagging:

| Kernel internal (replaced) | Typical legacy patchers | Observed/recorded effect |
|---|---|---|
| `\@startsection`, `\secdef`, `\@sect`, `\@ssect` | `titlesec`, `sectsty`, class code (memoir, KOMA) | `titlesec` partially emulated (`latex-lab-titlesec`); `memoir`, `scrartcl`, `scrbook` currently-incompatible |
| list internals (`\list`, `\@item`, `\@listi…`) | `enumitem` | `enumitem` not loaded; `latex-lab-enumitem` emulates `\newlist`, `\setlist` and most keys |
| `\@makecaption`, float internals | `caption`, `subcaption`, `subfig`, `sidecap` | `caption`: "configurations … overwritten by the tagging code" |
| `\@outputpage`, `\@makecol`, `\@opcol`, `\@startcolumn` | `vruler`, `gentombow`, `lineno` | latex-lab tracks dependants (`changing-@outputpage-dependencies.md`: "probably works, but not with tagging", "could probably be done much simpler these days using hooks"); `lineno` currently-incompatible, `lualineno` compatible |
| `\@starttoc`, `\contentsline` | `titletoc`, `tocloft`, `minitoc` | `titletoc`, `minitoc` currently-incompatible |
| `\maketitle` | `authblk`, `amsaddr`, class titles | `authblk` currently-incompatible; `amsaddr`: "`\maketitle` tagging is not correct" |
| theorem internals | `amsthm`, `\newtheoremstyle` | Emulated via block templates (2026-06-01, restrictions remain) |
| internal commands that `nameref` patched (theorem names, description items) | `nameref` | Broken by the new theorem/list code, repaired in 2026-11-01 via `\@currentlabelname` |

Sources: tagging-status DB comments; LaTeX News 43/44; `latex-lab/changing-@outputpage-dependencies.md`.

### C.2 Mathematical/programmatic collision classes

| Class | Mechanism | Example |
|---|---|---|
| Trial typesetting without suspension | Measuring passes produce MCs/structures that are never shipped; in generic mode MCID numbering gets holes/duplicates; in Lua mode attributes are cloned into discarded boxes | `pdfpages` ("duplicated structures as graphics are processed more than once in trial typesetting"), `tabularx` (fixed: `\SuspendTagging{tabularx}`), `beamer` (no-support) |
| Literal `$$` in package code | Bypasses `\dollardollar@begin/end`; display tagging cannot place structure before `\postdisplaypenalty` | legacy math environment packages |
| Math environments created by `\let`-copying | Copy is not registered for capture; captured/uncaptured mismatch | user code `\let\myeq\equation` (team guidance: generate math environments with a dedicated command, do not copy) |
| Tokens split across macros | Grabber cannot see the closing delimiter | `>{$}c<{$}` (handled by slow grabbing in tabular), `breqn` (catcode changes, not covered) |
| Hidden marks | Generic-mode MC split detection relies on marks at galley level | content in boxes, `longtable` head/foot boxes |
| Socket misuse | Calling `\UseSocket{tagsupport/…}` directly keeps output active during `\SuspendTagging` | custom package code |

### C.3 Legacy vs tagged kernel behaviour

| Aspect | Legacy format path | `\DocumentMetadata` path |
|---|---|---|
| Section commands | `\@startsection` (2.09-era code) | Template instances (`sec-template`), `order` key, tagging sockets |
| Lists | `\list` primitive interface | Block templates, `enumitem`-style keys built in |
| Paragraph start/end | `\everypar` only | `para/*` hooks + tagging sockets |
| Output routine | monolithic `\@outputpage` | socketed (`new-or-2`): `build/page/header`, `build/page/footer`, `build/column/*` |
| Math entry | `$` → math mode | `$` → capture → Formula structure → typeset |
| Superscripts in text, `\parbox` | fake math (`\ensuremath`, `$\vcenter$`) | no math mode (2026-06-01) |
| Configuration of layouts | redefining macros | `\EditInstance`, contexts |

---

## D. Verified primary sources

| Source | URL / location | Verification |
|---|---|---|
| `source2e` (kernel source, includes lthooks, ltpara, ltshipout, ltsockets, lttagging, ltmeta) | https://www.latex-project.org/help/documentation/source2e.pdf | fetched (200) |
| `cmdguide` (consolidated programmer reference, started 2026-06-01) | https://texdoc.org/serve/cmdguide/0 | PDF fetched |
| `lttagging` documentation | https://texdoc.org/serve/lttagging/0 | PDF fetched |
| `ltsockets` documentation | https://texdoc.org/serve/ltsockets-doc/0 | PDF fetched |
| `interface3` (L3 programming layer reference) | https://texdoc.org/serve/interface3/0 | PDF fetched |
| `latex-lab-math` (capture architecture, sockets, AF format) | https://texdoc.org/serve/latex-lab-math/0 | PDF fetched; source read |
| `l3pdfmeta` (standards, XMP) | https://texdoc.org/serve/l3pdfmeta/0 | PDF fetched; source `pdfresources/l3pdfmeta.dtx` @ `0fd0d9e` read |
| Kernel repository | https://github.com/latex3/latex2e | cloned @ `2ec6765` (develop; dev format 2027-07-01). Release behaviour cross-checked against installed 2026-11-01 files |
| L3 repository (`l3kernel/l3pdf.dtx`) | https://github.com/latex3/latex3 | cloned @ `65b3640` |
| PDF management repository | https://github.com/latex3/pdfresources | cloned @ `0fd0d9e` |
| Blueprint: Mittelbach & Rowley, "LaTeX Tagged PDF — A blueprint for a large project" (2020) | https://www.latex-project.org/publications/indexbyyear/2020/ | index page fetched (200) |
