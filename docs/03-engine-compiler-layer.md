# Layer 3: Engine/compiler: binary execution, Unicode and font processing (LuaTeX, pdfTeX)

Scope: how the TeX binaries turn tagging requests into PDF operators: when marked content is injected, how MCIDs are numbered, how real space glyphs, soft hyphens and Unicode mappings get into the content stream, and where each engine hits hard limits.

Measured toolchain: LuaHBTeX 1.24.0 and pdfTeX 1.40.29 (TeX Live 2026), LaTeX 2026-11-01, tagpdf 1.0i, luaotfload 3.29, luamml 0.9.3 **[R: `evidence/environment.txt`]**.

Evidence tags: **[S]** source/official docs, **[R]** reproduced (`evidence/`), **[U]** under-documented.

---

## A. Technical mechanics and data flow

### A.1 Engine support matrix [S: LaTeX News 40 §"Engine support: An important update"; tagpdf manual §"Engines and modes"]

| Engine | Tagging | Automatic MathML | Real space glyphs | Status statement |
|---|---|---|---|---|
| LuaTeX (`lualatex`, PDF mode) | Lua mode | yes (luamml, needs `unicode-math` or `lua-unicode-math`) | yes (Lua) | Recommended for new documents |
| LuaTeX DVI mode (`dvilualatex`) | Lua mode (since tagpdf 0.98k) | – | – | Supported by tagpdf |
| pdfTeX (`pdflatex`) | generic mode | no (external MathML or TeX-source AF only) | yes (`\pdfinterwordspaceon`) | "continues to be supported for existing material" |
| XeTeX (`xelatex`) | generic mode, untested | no | no | "impossible to reliably produce tagged PDFs due to engine limitations"; no new features from the LaTeX team |
| latex → dvips | generic mode, untested | no | no | Not recommended |
| (u)pTeX | unknown | – | – | No statement possible (LaTeX team) |

Mode selection: since tagpdf 0.99m, Lua mode is forced whenever LuaTeX is detected; every other engine uses generic mode **[S]**.

### A.2 Generic mode (pdfTeX): eager literal injection

```text
\tag_mc_begin:n{tag=P}  ──►  \pdf_bdc:nn → l3backend → \pdfliteral page {/P <</MCID n>> BDC}   (whatsit node)
text …                   ──►  glyph nodes
\tag_mc_end:             ──►  \pdfliteral page {EMC}
```

- Marked-content operators are **whatsit nodes in the main vertical/horizontal list**. TeX cannot look back past a whatsit, so consecutive glue that would normally collapse can survive, and breakpoints shift. Display math needs macro-level re-implementation of `\postdisplaypenalty`/`\belowdisplayskip` handling to keep structure data on the same page as the formula **[S: LaTeX News 41]**.
- `BDC` and `EMC` must sit on the same page. For paragraphs that break across pages, every MC command also emits a mark (`ltmarks`), and the output routine inspects the marks and closes/reopens chunks at the break (tagpdf ≥ 0.92; F. Mittelbach, TUG 2021 "Taming the beast") **[S: tagpdf manual §"Split paragraphs"]**. The related `\tagmcbegin`/`\tagmcend` must be at the same box level; boxes hide marks, which makes `longtable` problematic.
- MCIDs, parent pointers and structure numbers are resolved through the kernel property module (`ltproperties`, `\property_record:nn`, `\property_ref:nn`) across runs. Pass 1 therefore produces a structurally invalid file **[R: `evidence/03-ua2-pdflatex-external-mathml.txt`]**:

| pdfLaTeX pass | veraPDF `ua2` | Failure | "Rerun" warnings |
|---|---|---|---|
| 1 | FAIL | ISO 14289-2:2024 §8.2.2 t1 ×79: "Content that is not considered real shall be an artifact" (MCs not yet linked) | 1 |
| 2 | PASS | – | 1 |
| 3 | PASS | – | 0 |

### A.3 Lua mode (LuaTeX): deferred, attribute-driven injection

```text
\tag_mc_begin:n  ──► sets global node attributes (no node inserted)
                       g__tag_mc_type_attr, g__tag_mc_cnt_attr, g__tag_structnum_attr
typesetting       ──► every glyph/rule/box node inherits current attribute values
\shipout          ──► kernel ltshipout: __shipout_finalize_box: → call_callback('pre_shipout_filter', ShipoutBox)
                       ├─ 'tagpdf/space' → ltx.__tag.func.space_chars_shipout(box)   (ordered after 'luaotfload.dvi')
                       └─ 'tagpdf/mc'    → ltx.__tag.func.mark_shipout(box)
                             walks the shipped box, inserts BDC/BMC/EMC page literals where
                             attribute values change, numbers MCIDs per page,
                             wraps unattributed content in /Artifact (activate/tagunmarked=true)
end of document   ──► ltx.__tag.func.output_parenttree(abspage) writes ParentTree /Nums lines
```

Source anchors **[S: `tagpdf-mc-luacode.dtx`, `tagpdf-backend.dtx` (generates `tagpdf.lua`, `tagpdf-luatex.def`); kernel `latex.ltx` ltshipout section]**:

- Attribute registers allocated in `tagpdf.lua`: `g__tag_mc_type_attr`, `g__tag_mc_cnt_attr`, `g__tag_structnum_attr`, `g__tag_interwordspace_attr`, `g__tag_interwordspaceOff_attr`, `g__tag_interwordfont_attr`, `l__tag_softhyphen_attr` (`luatexbase.new_attribute`).
- `pre_shipout_filter` is a `list` callback **created by the LaTeX kernel** (`luatexbase.create_callback('pre_shipout_filter','list')` in `\everyjob`, ltshipout). If it is absent, tagpdf falls back to the `shipout/before` hook operating on `tex.box["ShipoutBox"]`.
- Consequences: page breaks inside an MC are harmless (at worst empty chunks), nothing is inserted into the typesetting stream so line/page breaking is unaffected, and no cross-run label system is needed for MCIDs. Lua mode validated on pass 1 **[R: `evidence/01-lualatex-pass-convergence.txt`]**.

### A.4 Interword space glyphs

| | pdfTeX | LuaTeX |
|---|---|---|
| Activation | `activate/spaces` → primitive `\pdfinterwordspaceon` (global whatsit) | `activate/spaces` → Lua `__tag_mark_spaces` in `pre_linebreak_filter` and `hpack_filter` (callback id `markspaces`) |
| Glyph source | dedicated font "dummy-space" → embedded as `PdfTeX-Space` **[R: font list]** | current font's U+0020 glyph if present, else default Latin Modern |
| Injection time | at shipout by the engine | `space_chars_shipout` in `pre_shipout_filter` |
| Manual insert | `\pdffakespace` | `\pdffakespace` (defined by tagpdf; may create breakpoints) |
| Tunability | none (primitive on/off) | attributes, `debug/show=spaces` visualises insert points |
| Cost | font switch to dummy-space and back at every space (larger PDF) | glyph insertion per space |

Observed operator stream (pdfLaTeX, `tagging=on`): `TJ 'Office'` → `Tf /F48` (PdfTeX-Space) → `Tj ' '` → `TJ 'text.'`. With `tagging=off` the same words are a single `TJ` with a kern **[R: `evidence/activation-modes.txt`]**.

### A.5 Hyphenation: hard vs soft hyphen (Lua mode only)

`tagpdf.lua` registers two callbacks under id `tagpdf.rewrite-softhyphen` **[S]**:

1. `pre_shaping_filter` (callback created by luaotfload: `luatexbase.create_callback('pre_shaping_filter','list')`): walks discretionary nodes of subtype explicit/regular and flags every char in `disc.pre` via `node.get_properties_table()`.
2. `post_shaping_filter`: for flagged glyphs equal to U+002D:
   - `activate/softhyphen=char` and the font is OpenType/TrueType containing U+00AD → `n.char = 0xAD`;
   - otherwise (default `artifact` while tagging) → MC attributes set to `-2147483647`, i.e. the hyphen is emitted as `/Artifact` content.

| Mode | `pdftotext -raw` line end | pdf.js `getTextContent()` | Structure-aware AT |
|---|---|---|---|
| legacy / pdfTeX tagged | `in-` | `in-` | hyphen is real content |
| LuaTeX default (`artifact`) | `in-` | `in-` | hyphen excluded as artifact |
| LuaTeX `char` | `in\xad` (U+00AD) | – | soft hyphen semantics |

**[R: `evidence/clipboard.txt`]**. pdfTeX: "For pdftex no solution is known" **[S: tagpdf manual §"Accessibility is not only tagging"]**.

### A.6 Unicode mapping of glyphs

| Path | Mechanism | Evidence |
|---|---|---|
| pdfTeX, Type 1 fonts | Kernel loads `glyphtounicode.tex` + `glyphtounicode-cmex.tex` (`\everyjob` injection for pdfTeX ≥ 1.40.22) and sets `\pdfgentounicode=1` → `/ToUnicode` CMaps | **[S: `latex.ltx`]**; all fonts `ToUnicode=True` **[R]** |
| LuaTeX ≥ 1.24, legacy Type 1 fonts in PDF mode | Kernel wraps `\pdfextension glyphtounicode` (active only when `\outputmode=1`) and sets `\pdfvariable gentounicode=1`; LuaTeX 1.24 adds an extended `\pdfglyphtounicode` syntax for symbol fonts | **[S: `latex.ltx`; LaTeX News 43 §"Improved copy & paste"]** |
| LuaTeX, OpenType (luaotfload, node mode) | Type0/CID fonts with ToUnicode from font cmap | `LMRoman10-Regular Subtype=/Type0 ToUnicode=True` **[R]** |
| LuaTeX, HarfBuzz mode (`luaotfload-harf-plug.lua`) | Cluster with 1 glyph → font-level ToUnicode; cluster with >1 glyph, or glyph with no character → page literal `/Span<</ActualText<FEFF…>>>BDC … EMC` | **[S]** |
| Ligatures (`fi`, `ffi`, `fl`) | ToUnicode multi-char mapping | `office affine flow` extracted intact by poppler and pdf.js in all variants **[R]** |
| unicode-math italics | Math Alphanumeric Symbols block | `$\alpha$` extracts as `𝛼` (U+1D6FC) with unicode-math, `α` (U+03B1) with pdfTeX/CM **[R]** |

### A.7 Engine-level metadata control

PDF/A-4 prohibits the trailer `/Info` dictionary (unless `PieceInfo` exists). l3backend uses the engine switches `\pdfomitinfodict` / `\pdfomitcharset` (pdfTeX) and `\pdfvariable omitinfodict` / `omitcharset` (LuaTeX) **[S: `l3backend-pdf-pdftex.def`, `l3backend-pdf-luatex.def`]**. Observed: `Trailer /Info present: False` for `pdfstandard={UA-2,A-4F}` **[R]**. For UA-2 alone, `Trailer_no_Info` is "only a should not a shall" and is not forced **[S: `l3pdfmeta.dtx`, 2025-06-11 note]**.

### A.8 Math to MathML inside the engine (LuaTeX only)

`luamml-tex.lua` registers `'luamml.to_mathml'` on **`pre_mlist_to_hlist_filter`**, which the kernel emulates (`ltluatex`: `pre_mlist_to_hlist_filter` → exclusive `mlist_to_hlist` → `post_mlist_to_hlist_filter`). It is declared to run before `luamml.mathflatten` **[S]**. At each math list:

1. `process_mlist(mlist, style, …)` converts the noad list into an XML table *before* `node.mlist_to_hlist` builds boxes.
2. `\l__luamml_flag_int` bits select actions (bitfield documented in `luamml-tex.lua`): bit 0 = save MathML on the math-start node for later use (inline only); bit 1 = `save_result`: build the root element and emit it to the output file and/or the registered output hook. latex-lab-math opens `\jobname-luamml-mathml.html` with `\iow_open:Nn` and registers `\__luamml_register_output_hook:N \__math_luamml_output_hook:n`, which writes the `<div><h2>\mml n</h2>…` records. Bit 2 = replace the root element name (`\l__luamml_root_tl`). Bit 3 = generate structure elements via `luamml-structelemwriter.lua`, which pushes tagpdf's `tag_struct_begin:n`, `tag_mc_begin:n`, `tag_mc_end:`, `tag_struct_end:` as tokens. Bits 4–7 override the math style.
3. Fonts: correct Unicode MathML needs `unicode-math`/`lua-unicode-math`; otherwise tagpdf warns `Neither unicode-math nor lua-unicode-math has been loaded` and symbols may be mapped to the wrong code points **[R: warning in `07-friction-soul` log; S: latex-lab-math §luamml]**.

---

## B. Core entities, commands, and specifications

| Entity | Engine | Type | Role in stack |
|---|---|---|---|
| `\pdfliteral page {…}` | pdfTeX | primitive (whatsit) | Generic-mode carrier of `BDC`/`BMC`/`EMC` |
| `\pdfinterwordspaceon/off`, `\pdffakespace` | pdfTeX | primitives | Real space glyphs |
| `\pdfgentounicode`, `\pdfglyphtounicode` | pdfTeX; LuaTeX via `\pdfvariable`/`\pdfextension` | primitives | ToUnicode generation |
| `\pdfomitinfodict`, `\pdfomitcharset` | pdfTeX (LuaTeX: `\pdfvariable omitinfodict/omitcharset`) | primitives | PDF/A-4 trailer and CharSet control |
| `pre_shipout_filter` | LuaTeX | callback (`list`, created by the LaTeX kernel) | Lua-mode MC and space injection |
| `pre_linebreak_filter`, `hpack_filter` | LuaTeX | engine callbacks | Space marking (`markspaces`) |
| `pre_shaping_filter`, `post_shaping_filter` | LuaTeX | callbacks created by luaotfload | Soft-hyphen rewrite |
| `pre_mlist_to_hlist_filter` | LuaTeX | callback emulated by `ltluatex` | luamml conversion |
| `luatexbase.add_to_callback(name, fn, identifier)` | LuaTeX | kernel Lua API | Since 2026-11-01 the third argument is a unique **identifier**; duplicates warn (planned error) **[S: LaTeX News 44]** |
| `luatexbase.declare_callback_rule` | LuaTeX | kernel Lua API | Ordering (`tagpdf/space` after `luaotfload.dvi`) |
| `luatexbase.new_luacommand(csname, fn, …)` | LuaTeX | kernel Lua API (2026-11-01) | One-call Lua command binding |
| `tagpdf.lua` (`ltx.__tag.func.*`), `tagpdf-luatex.def` | LuaTeX | files | Lua-mode backend |
| `glyphtounicode.tex`, `glyphtounicode-cmex.tex` | both | data files | Glyph name → Unicode tables |

Direct engine calls for inspection:

```bash
lualatex --version | head -1
```

```bash
pdftex --version | head -1
```

```bash
python3 -I tools/inspect_pdf.py file.pdf fonts
```

---

## C. System interdependencies and package friction

### C.1 Generic vs Lua mode

| Property | Generic mode (pdfTeX) | Lua mode (LuaTeX) |
|---|---|---|
| MC insertion | Immediate whatsits | Attributes, injected at shipout |
| Effect on spacing/breaking | Can alter glue collapse and breakpoints | None from MC markers |
| Paragraph split over pages | Mark-based close/reopen in output routine; box-level restrictions | Transparent |
| MCID numbering | Cross-run properties; ≥ 2 passes **[R]** | At shipout; 1 pass valid **[R]** |
| Untagged content | Stays unmarked (veraPDF §8.2.2 failure if not artifact) | Auto-artifact (`activate/tagunmarked`) |
| MathML | External HTML keyed by MD5 of the source (`\str_mdfive_hash:n`) | luamml (AF and/or structure elements) |
| Soft hyphen | none | `activate/softhyphen` |
| Default text encoding under `\DocumentMetadata` | T1 (cm-super Type 1) | TU (OpenType via fontspec) |
| Benchmark, 49 pp. (median warm run) | 0.23 s → **6.06 s** tagged (×26) | 1.85 s → **7.93 s** tagged with MathML AF (×4.3); **13.05 s** with MathML SE (×7.1) **[R: `evidence/benchmark.txt`]** |

### C.2 Engine-level incompatibilities

| Component | Collision | Source |
|---|---|---|
| XeTeX documents and XeTeX-only packages (`xltxtra`: status *no-support*) | No reliable tagging on xdvipdfmx; no real space glyphs | LaTeX News 40; status DB |
| `pstricks`, `auto-pst-pdf`, `breakurl` (dvips route) | "dvips cannot properly support PDF/UA"; `breakurl` requires latex→dvips→ps2pdf | status DB comments |
| Legacy 8-bit math fonts (`newtxmath`, `mathptmx`, `stmaryrd`, `amscd`, `breqn`) | "Incompatible with unicode-math. No luamml support." Without unicode-math the Lua converter maps glyph slots to wrong code points | status DB; latex-lab-math |
| Fonts without Unicode mapping (bitmap PK/Type 3, custom symbol fonts) | ISO 14289-2:2024 §8.4.5.8 requires every used character code to map to Unicode; §8.4.3 requires ActualText/Alt for PUA | veraPDF profile; fix with `\pdfglyphtounicode` or `actualtext` |
| Packages that rebox or re-typeset material (`beamer`, `pdfpages`, `soul`) | Trial typesetting generates MCs/structures that never reach the page; Lua attributes duplicated | status DB (`beamer`: no-support; `pdfpages`: duplicated structures) |
| Lua code registering callbacks with a reused description string | 2026-11-01 warns on duplicate identifiers (future error) | LaTeX News 44 |
| `luatex85` | Required by some legacy packages (`attachfile`, `xy`) under LuaTeX | status DB |

### C.3 Legacy vs tagged engine output

| Output property | Legacy pdfLaTeX | Tagged pdfLaTeX | Legacy LuaLaTeX | Tagged LuaLaTeX |
|---|---|---|---|---|
| PDF version | 1.7 | 2.0 | 1.7 (default) | 2.0 |
| Text font encoding | OT1 (CMR10) | T1 (SFRM1000) | TU (LMRoman) | TU (LMRoman) |
| Space representation | positioning only | `PdfTeX-Space` glyph | positioning only | U+0020 from current font |
| Marked content | none | eager literals | none | shipout literals |
| File size, 49 pp. | 166 KiB | 331 KiB | 126 KiB | 260 KiB (AF) / 316 KiB (SE) |

---

## D. Verified primary sources

| Source | URL / location | Verification |
|---|---|---|
| LuaTeX project home / source | https://www.luatex.org/ ; https://gitlab.lisn.upsaclay.fr/texlive/luatex | fetched (200) |
| LuaTeX reference manual | https://texdoc.org/serve/luatex/0 | PDF fetched |
| pdfTeX home / manual | https://www.tug.org/applications/pdftex/ ; https://texdoc.org/serve/pdftex/0 | fetched |
| luaotfload (HarfBuzz mode, shaping callbacks) | https://github.com/latex3/luaotfload ; https://texdoc.org/serve/luaotfload/0 | repo verified; PDF fetched; installed `luaotfload-harf-plug.lua` v3.29 read |
| luamml source | https://github.com/latex3/luamml | cloned @ `c5b9bb0` (v0.9.3) |
| tagpdf Lua backend (`tagpdf-backend.dtx`, `tagpdf-mc-luacode.dtx`, `tagpdf-space.dtx`) | https://github.com/latex3/tagpdf | cloned @ `c669286` |
| Kernel `ltluatex.dtx` (callback API), `ltshipout` | https://github.com/latex3/latex2e/tree/develop/base | cloned @ `2ec6765`; installed `latex.ltx` 2026-11-01 read |
| LaTeX News 40 (engine statement), 41, 43, 44 | https://www.latex-project.org/news/latex2e-news/ltnews.pdf | fetched |
| TUG 2021, F. Mittelbach "Taming the beast" (paragraph tagging in pdfTeX), cited by tagpdf manual | https://youtu.be/SZHIeevyo3U?t=19551 | cited by primary source; not fetched |
