# Layer 2: Semantic map: global configuration hooks and tagging declarations

Scope: how LaTeX's semantic vocabulary (`section`, `text-block`, `itemize`, …) is declared, namespaced and role-mapped onto the standard PDF structure types; the global configuration surface (`\tagpdfsetup`, tagging sockets, symbolic structure names, attribute classes); and the containment rules that constrain the resulting tree.

Evidence tags: **[S]** source/official docs, **[R]** reproduced (`evidence/`), **[U]** under-documented.

---

## A. Technical mechanics and data flow

### A.1 Tag → namespace → standard role: the resolution chain

1. Kernel or latex-lab code opens a structure: `\tag_struct_begin:n {tag=<name>}`. In migrated code the name comes from `\UseStructureName{<symbolic>}` rather than a literal **[S: `lttagging.dtx`, LaTeX News 42 "Symbolic structure names"]**.
2. tagpdf looks up `<name>` in its role tables. Namespaces are declared in `tagpdf-roles.dtx` (`\__tag_role_NS_new:nnn {latex}{https://www.latex-project.org/ns/dflt}{}`, likewise `latex-book`). Their type inventories come from data files: `tagpdf-ns-pdf.def`, `tagpdf-ns-pdf2.def`, `tagpdf-ns-mathml.def`, `tagpdf-ns-latex-book.def` (tagpdf, generated from `tagpdf-data.dtx`) and `tagpdf-ns-latex-lab.def`, `tagpdf-ns-latex-lab-book.def` (latex-lab) **[S: installed file list, `tagpdf-roles.dtx`]**.
3. The `StructElem` dictionary is written with `/S /<name>` and, under PDF 2.0, `/NS <ref to namespace dict>`.
4. The namespace dictionary for the LaTeX namespaces carries `/RoleMapNS`, mapping each LaTeX type to `[/<StdType> <ref to pdf2 or pdf namespace>]`.
5. For processors that ignore `/Namespaces`, tagpdf additionally writes a flat `/RoleMap` into the StructTreeRoot (since tagpdf 0.98t: "Fallback RoleMap") **[S: tagpdf manual §"Fallback RoleMap"]**.

Measured output of `examples/01-ua2-lualatex-mathml-se.tex` **[R: `evidence/01-ua2-lualatex-mathml-se.txt`]**:

```text
StructTreeRoot keys: /ClassMap /IDTree /K /Namespaces /ParentTree /RoleMap /Type
  Namespace http://iso.org/pdf/ssn                        RoleMapNS entries=0
  Namespace http://iso.org/pdf2/ssn                       RoleMapNS entries=0
  Namespace http://www.w3.org/1998/Math/MathML            RoleMapNS entries=0
  Namespace https://www.latex-project.org/ns/dflt         RoleMapNS entries=43
  Namespace https://www.latex-project.org/ns/book         RoleMapNS entries=7
  Namespace data:,455B1960-CD1C-6B9C-EA1C-132AD7DA1908     RoleMapNS entries=0
  Fallback /RoleMap entries: 55
```

The last entry is the **user namespace**. Its default URI is a `data:` URI holding a per-run random UUID; `\tagpdfsetup{role/user-NS=<string>}` replaces it with `https://www.latex-project.org/ns/local/<string>` **[S: tagpdf manual §"The user namespace"; R: observed `data:` form]**. Do not rely on the default URI being reproducible: three audit compiles of the same source produced `data:,66912890-713B-37D4-8811-F9C06271EA94`, `data:,C2B5D242-942-237E-2268-84A7E9774D61` and the value shown above **[R]**.

### A.2 The LaTeX namespace RoleMapNS (verbatim from the produced PDF) [R]

`https://www.latex-project.org/ns/dflt` (43 entries):

| LaTeX type → standard type | LaTeX type → standard type | LaTeX type → standard type |
|---|---|---|
| `section` → pdf2:`Sect` | `section-heading` → pdf2:`H1` | `heading-number` → pdf2:`Lbl` |
| `subsection` → pdf2:`Sect` | `subsection-heading` → pdf2:`H2` | `heading-fragment` → pdf2:`Span` |
| `subsubsection` → pdf2:`Sect` | `subsubsection-heading` → pdf2:`H3` | `heading-minipage`/`-parbox` → pdf2:`Span` |
| `paragraph` → pdf2:`Sect` | `paragraph-heading` → pdf2:`H4` | `subparagraph-heading` → pdf2:`H5` |
| `part` → pdf2:`Part` | `part-heading` → pdf2:`Title` | `title-text` → pdf2:`Title` |
| `title-block` → pdf2:`Div` | `title-author`, `title-date` → pdf2:`P` | `semantic-para` → pdf2:`Part` |
| `text-block` → pdf2:`P` | `text-fragment` → pdf2:`Span` | `text-minipage`, `text-parbox` → pdf2:`Div` |
| `itemize`, `enumerate`, `description`, `list` → pdf2:`L` | `item` → pdf2:`LI` | `itemlabel` → pdf2:`Lbl`, `itembody` → pdf2:`LBody` |
| `footnote` → pdf2:`FENote` | `footnotemark`, `footnotelabel` → pdf2:`Lbl` | `float` → pdf2:`Aside` |
| `figures`, `tables` → pdf2:`Sect` | `theorem-like` → pdf2:`Sect` | `codeline` → pdf2:`Sub` |
| `quote`, `quotation` → **pdf**:`BlockQuote` | `verbatim` → **pdf**:`Code` | |

`https://www.latex-project.org/ns/book` (7 entries) shifts heading levels for classes with `\chapter`: `chapter` → `Sect`, `chapter-heading` → `H1`, `section-heading` → `H2`, … `subparagraph-heading` → `H6`.

Fallback `/RoleMap` (PDF 1.7 vocabulary, 55 entries) maps PDF 2.0-only types downwards: `Title→P`, `FENote→Note`, `Aside→Note`, `Em→Span`, `Strong→Span`, `Sub→Span`, `DocumentFragment→Art`, `H7…H10→H6`, `Artifact→Private`. LaTeX types map directly to 1.7 types: `title-text→P`, `part-heading→P`, `float→Note`, `footnote→Note`, `text-block→P`, `semantic-para→Part`, … **[R]**.

### A.3 Paragraph semantics (non-HTML model)

ISO 32000 forbids nested `P`. LaTeX models a "semantic paragraph" with a display block (quote, list) in the middle as follows **[S: LaTeX News 44 "Tags for paragraphs"; tagpdf manual §"Automatic tagging of paragraphs"]**:

```text
<semantic-para>            % → Part
  <text-block> … </text-block>      % → P
  <quote><text-block> … </text-block></quote>
  <text-block> … </text-block>
</semantic-para>
```

The structures are opened and closed by the kernel paragraph hooks `para/begin` and `para/end` (`ltpara.dtx`), through the tagging sockets `para/semantic/begin|end`. Names before 2026-11-01: `text-unit`, `text`. Disable the outer level:

```latex
\AssignTaggingSocketPlug{para/semantic/begin}{noop}
\AssignTaggingSocketPlug{para/semantic/end}{noop}
```

### A.4 Attributes and attribute classes

Layout and table semantics are written as attribute *classes* (`/C` on the element, definitions in `/ClassMap`), not as inline `/A` dictionaries. Observed ClassMap keys: `/inline`, `/display`, `/center`, `/justify`, `/itemize`, `/TH-row`, `/TH-col`, `/TH-both`, `/aria-banner` **[R]**. Resolved examples:

```text
title-block [latex]  attrs=class:/aria-banner,/O:/ARIA-1.1,/role:banner
Formula [pdf2]       attrs=class:/inline,/O:/Layout,/Placement:/Inline
TH                   [{'class': '/TH-col', '/O': '/Table', '/Scope': '/Column'}]
TD                   [{'class': '/rowspan-2', '/O': '/Table', '/RowSpan': '2'}]
```

### A.5 Global configuration hooks

| Mechanism | Granularity | Example |
|---|---|---|
| `\DocumentMetadata{tagging-setup={…}}` | document, applied at `class/before` | `tagging-setup={math/setup=mathml-SE, title=H1}` |
| `\tagpdfsetup{…}` | preamble or local (key dependent) | `\tagpdfsetup{table/header-rows={1}}` |
| Tagging sockets: `\AssignTaggingSocketPlug{<socket>}{<plug>}` | global or group-local | `para/semantic/begin` → `noop` |
| Symbolic names: `\AssignStructureRole{<symbolic>}{<role>}` | document | re-role a LaTeX name |
| `\tagpdfsetup{role/new-tag={tag=…, tag-namespace=…, role=…, role-namespace=…}}` | document | declare a custom type in the `user` namespace |
| `\tagpdfsetup{role/map-tags=pdf}` | document | emit standard PDF names instead of LaTeX names |
| Document properties | query | `\GetDocumentProperty{document/tagging}` → `active`/`inactive`; also `tagging/para`, `tagging/interwordspace` |
| Associated CSS for derivation | document | `attach-css`, `css-list`, `css-list-add`, `css-list-remove`, `root-supplemental-file` |

CSS associated files `latex-list-css.html` and `latex-align-css.html` (`/AFRelationship /Supplement`, `/Subtype /text/html`) were observed in the **Catalog** `/AF` array of the produced file, while StructTreeRoot carried no `/AF` **[R]**. The tagpdf manual describes them as attached "to the StructTreeRoot". The observed placement is what 2026-11-01 writes.

---

## B. Core entities, commands, and specifications

### B.1 Namespaces

| Short name (tagpdf) | URI written | Public name (for attributes) | Normative basis |
|---|---|---|---|
| `pdf` | `http://iso.org/pdf/ssn` | `tag/NS/pdf` | ISO 32000-2:2020 §14.8.6 (PDF 1.7 standard structure namespace) |
| `pdf2` | `http://iso.org/pdf2/ssn` | `tag/NS/pdf2` | ISO 32000-2:2020 §14.8.6 (PDF 2.0 standard structure namespace) |
| `mathml` | `http://www.w3.org/1998/Math/MathML` | `tag/NS/mathml` | ISO 32000-2:2020 §14.8.6 (other namespace: MathML 3.0) |
| `latex` | `https://www.latex-project.org/ns/dflt` | – | LaTeX Project (`latex-lab-namespace`) |
| `latex-book` | `https://www.latex-project.org/ns/book` | – | LaTeX Project |
| `user` | `data:,<UUID>` (default) or `https://www.latex-project.org/ns/local/<s>` | `tag/NS/user` | tagpdf |

### B.2 `\tagpdfsetup` keys (from source, 2026-11-01) [S: `tagpdf*.dtx`, `latex-lab-*.dtx`]

| Path | Keys |
|---|---|
| `activate/` | `all`, `mc`, `struct`, `tree`, `spaces`, `softhyphen` (`false`/`off`/`char`/`artifact`/`true`/`on`), `tagunmarked`, `struct-dest`, `socket`, `collect-link-artifacts`; meta key `activate[=<root tag>]` |
| `para/` | `tagging`, `tag`, `maintag`, `flattened` |
| `page/` | `tabsorder` (`row`/`column`/`structure`/`none`), `exclude-header-footer`, `tag-header-footer` |
| `role/` | `new-tag`, `new-attribute`, `new-attribute*`, `map-tags` (`false`/`pdf`), `mathml-tags`, `new-NS`, `user-NS` |
| `table/` | `tagging` (`true`/`false`/`presentation`/`div`), `header-rows`, `header-columns`, `multirow` |
| `math/` | `setup` (`mathml-SE`, `mathml-AF`, `tex-AF`, `mathml-MS`), `mathml/sources`, `mathml/AF`, `tex/AF`, `alt/use`, `MS/use`, `mathml/structelem`, `mathml/ignore`, `mathml/luamml/load`, `mathml/luamml/write`, `mathml/write-dummy` |
| `title` | `Title` (default), `H1`, `html`, `aria` |
| `float/` | `defer`, `flush`, `here`, `new`, `split` |
| `sec/` | `end`, `grouping` |
| `viewer/` | `pane/mathml`, `pane/mathsource`, `startstructure` |
| `debug/` | `show` (`para`, `spaces`, `paraOff`, `spacesOff`), `log` (`none`,`v`,`vv`,`vvv`,`all`), `structures`, `uncompress` |
| CSS | `attach-css`, `css-list`, `css-list-add`, `css-list-remove`, `root-supplemental-file` |

Deprecated aliases still parsed: `activate-all`, `activate-mc`, `activate-struct`, `activate-tree`, `interwordspace`, `paratagging`, `paratagging-show`, `paratag`, `mathml-tags`.

### B.3 Containment rules

- tagpdf checks parent–child pairs against `tagpdf-parent-child.csv` (PDF 1.7) and `tagpdf-parent-child-2.csv` (PDF 2.0), both generated from `tagpdf-data.dtx`. Violations are logged, e.g. `Package tagpdf Warning: Parent-Child 'pdf2:P' --> 'pdf2:Part'` **[R: `evidence/07-friction-soul.txt`]**.
- veraPDF enforces the normative matrix of **ISO/TS 32005:2023 Table 5**: 1,636 of the 1,728 rules in `PDFUA-2-ISO32005.xml` (veraPDF 1.30.3) carry `specification="ISO_32005"`, clause `Table 5` **[R: profile extracted from `cli-1.30.3.jar`]**.

---

## C. System interdependencies and package friction

### C.1 Normative constraints that shaped the design

| Constraint (ISO 14289-2:2024, verbatim from veraPDF profile) | Design consequence in LaTeX |
|---|---|
| §8.2.4 t1/t4: all structure elements shall belong to, or be role mapped to, the PDF 1.7, PDF 2.0 or MathML namespace (ISO 32000-2:2020 §14.8.6) | Every LaTeX type has a RoleMapNS entry into `pdf2`/`pdf` |
| §8.2.4 t2: a circular mapping shall not exist | Role mapping is acyclic by construction; tagpdf "can't (or more precisely won't) check if some unusual role mapping makes really sense" for user mappings **[S]** |
| §8.2.4 t3: within a given explicitly provided namespace, types shall not be role mapped to types in the same namespace | LaTeX names cannot live in `pdf2` and map to `pdf2`: hence the dedicated `latex` namespace |
| §8.2.5.12: conforming files shall not use `H` | Only numbered `H1`…`H6`; deeper levels mapped `H7…H10→H6` in fallback |
| §8.2.5.2: StructTreeRoot shall contain a single `Document` element | tagpdf `activate` key opens `Document` at begin document |
| §8.2.5.29: `math` only as a child of `Formula` | Formula structure always wraps MathML structure elements |
| §8.2.5.26: tables shall be regular | `table/multirow` suppresses tagging of covered cells; the author must leave them empty |

### C.2 Consumer-side mismatch

| Consumer behaviour | Effect on LaTeX output |
|---|---|
| pdf.js 6.5.438 builds its role table from StructTreeRoot `/RoleMap` only, one level (`roleMap.get(name) ?? name`); `/Namespaces` and `/RoleMapNS` are not read by the struct-tree serializer **[R: `pdf.worker.mjs` class `StructTreeRoot`]** | Without the fallback RoleMap, pdf.js would expose raw `section-heading`. With it: `section→Sect`, `section-heading→H1`, `text-block→P` (aria `paragraph`) **[R: `evidence/clipboard.txt`]** |
| Fallback RoleMap maps `Title→P`, `title-text→P` | PDF 1.7-only consumers announce the title as a paragraph |
| PDF 2.0 `Title` has no defined Accessibility-API mapping; PDF Association "Deriving HTML from PDF 1.2" maps it to `<div>` **[S: LaTeX News 44]** | Use `tagging-setup={title=H1}` for the best current AT behaviour (title/part → H1, sections from H2) |
| PAC mis-handles attribute classes for `ColSpan` and reports a missing attribute **[S: `latex-lab-table.dtx` footnote]** | False positive in PAC; veraPDF accepts |

### C.3 Package friction at the semantic layer

| Package (tagging-status DB, `_data/tagging-status.yml` @ `d0001d5`) | Status | Mechanism of collision |
|---|---|---|
| `soul` | currently-incompatible | Re-typesets its argument piece by piece in measuring boxes; paragraph hooks fire inside, producing `P`→`P` and `P`→`Part` nesting. Compile error `The number of automatic begin (3) and end (1) text-block para hooks differ!`; veraPDF FAIL ISO/TS 32005 Table 5 **[R]** |
| `ulem`, `lua-ul` | currently-incompatible / partially-compatible | "Missing TextDecoration attributes": underline/strike semantics are not mapped to `/TextDecorationType` |
| `caption` | currently-incompatible | "the configurations from the package are overwritten by the tagging code" |
| `multirow` | currently-incompatible | No `RowSpan` attribute; covered cell emitted as an extra empty `TD`. Passes veraPDF, so the table is wrong without being flagged **[R: `evidence/06-08-table-semantics.txt`]** |
| `memoir`, `scrartcl`, `scrbook`, `llncs`, `IEEEtran` | currently-incompatible | Class-level redefinitions of sectioning/title/toc internals that latex-lab replaces with template-based, tagging-aware code |
| `amsart`, `amsbook` | partially-compatible | "`\maketitle` and `\@starttoc` will need adjustment" (heading/title instances for AMS classes added 2026-11-01) |
| `fncychap` | compatible since 2026-11-01 | Wrapped by a simple template; may emit empty `heading-fragment` structures (allowed, harmless) **[S: LaTeX News 44]** |
| `enumitem`, `titlesec` | partially-compatible | Not loaded; emulated by `latex-lab-enumitem` / `latex-lab-titlesec` **[R: log `Package: latex-lab-enumitem 2026-10-24 v0.80i Emulating enumitem`]** |

### C.4 Legacy vs tagged semantic output

| Aspect | Legacy (no `\DocumentMetadata`) | Tagged (2026-11-01) |
|---|---|---|
| Semantic layer in file | none (`/StructTreeRoot` absent) | StructTreeRoot with 6 namespaces, `/ParentTree`, `/IDTree`, `/ClassMap` |
| Heading semantics | visual only | `section` (Sect) + `section-heading` (H1) + `heading-number` (Lbl) |
| Paragraph | visual only | `semantic-para` (Part) / `text-block` (P) |
| Table headers | visual only | `TH` + `/Scope` via attribute class |
| Custom vocabulary | n/a | `user` namespace + RoleMapNS |
| Readers without namespace support | n/a | fallback `/RoleMap` (55 entries) |

---

## D. Verified primary sources

| Source | URL / location | Verification |
|---|---|---|
| tagpdf manual (§ Standard types and new tags, namespaces, fallback RoleMap, parent-child) | https://texdoc.org/serve/tagpdf/0 | PDF fetched; source `doc/tagpdf.tex` @ `c669286` |
| tagpdf source tree | https://github.com/latex3/tagpdf | cloned @ `c669286` (v1.0i, 2026-10-29) |
| `latex-lab-namespace` (LaTeX tag set and naming scheme) | https://texdoc.org/serve/latex-lab-namespace/0 | PDF fetched |
| `latex-lab-title` (`title` key: `Title`/`H1`/`html`/`aria`) | https://texdoc.org/serve/latex-lab-title/0 | PDF fetched |
| `lttagging` (sockets, `\NewStructureName`) | https://texdoc.org/serve/lttagging/0 | PDF fetched |
| Tagging status database | https://latex3.github.io/tagging-project/tagging-status/ ; raw data `_data/tagging-status.yml` in https://github.com/latex3/tagging-project | page fetched (200); repo cloned @ `d0001d5` |
| veraPDF validation profiles (PDF/UA-2 + ISO/TS 32005 Table 5) | https://github.com/veraPDF/veraPDF-validation-profiles | repo verified (`git ls-remote`); profile XML read from veraPDF 1.30.3 jar |
| PDF Association "Deriving HTML from PDF" v1.2 (cited by LaTeX News 44) | https://pdfa.org/deriving-pdf-to-html-version-1-2/ | cited by primary source; pdfa.org refuses automated fetch (HTTP 403), not fetched |
