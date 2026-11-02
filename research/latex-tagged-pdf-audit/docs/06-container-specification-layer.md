# Layer 6: Container specification: ISO 32000-2 (PDF 2.0) and PDF/UA-2 schemas

Scope: the normative object model the LaTeX stack writes into (PDF 2.0 logical structure, marked content, namespaces, associated files, XMP identification), the conformance profiles it targets (PDF/UA-2, WTPDF 1.0, PDF/A-4), and how validators decide pass/fail.

Evidence tags: **[S]** source/official docs, **[R]** reproduced (`evidence/`), **[U]** under-documented. Clause numbers below are cited as they appear verbatim in veraPDF 1.30.3's `PDFUA-2-ISO32005.xml` profile, unless marked otherwise.

---

## A. Technical mechanics and data flow

### A.1 Object graph written by LaTeX 2026-11-01 (raw syntax from `examples/02-…`) [R]

```text
% Catalog (subset)
<< /Type /Catalog /StructTreeRoot 5 0 R /MarkInfo << /Marked true >> /Lang (en-US)
   /Metadata 75 0 R /ViewerPreferences << /DisplayDocTitle true >> /AF 110 0 R >>

% StructTreeRoot (subset)
<< /Type /StructTreeRoot /K 22 0 R /ParentTree 6 0 R /Namespaces 8 0 R
   /ClassMap 120 0 R /IDTree 119 0 R /RoleMap … >>

% Namespace dictionary
<< /Type /Namespace /NS (https://www.latex-project.org/ns/book) /RoleMapNS 18 0 R >>
% RoleMapNS entry (latex dflt namespace): /text-block [ /P 11 0 R ]   where 11 0 R = http://iso.org/pdf2/ssn

% StructElem
<< /Type /StructElem /S /Formula /NS 11 0 R /P 41 0 R /ID (ID.015) /T <feff…"math"> /C /inline
   /K << /Type /MCR /MCID 7 /Pg 21 0 R >>  /AF [ 24 0 R 44 0 R ] >>

% Associated file (AF[0] of the Formula)
<< /Type /Filespec /F (mathml-1.xml) /UF <feff…> /Desc (mathml-1) /AFRelationship /Supplement
   /EF << /F 23 0 R /UF 23 0 R >> >>
% EmbeddedFile stream dictionary
<< /Type /EmbeddedFile /Subtype /application#2fmathml+xml /Params << /ModDate (D:20261102) >> >>

% Page
<< /Type /Page /StructParents 0 /Tabs /S … >>
% ParentTree /Nums: 0 → [ … ] (array indexed by MCID; 21 entries for page 1)

% Content stream
/section-heading <</MCID 4>> BDC … EMC        % real content, MCID → ParentTree → StructElem
/Artifact BMC … EMC                            % header/footer/page number
```

Data flow inside the container: the content-stream MCID plus the page `/StructParents` key the `ParentTree` number tree, which returns the owning `StructElem`. `/P` chains lead to the single `Document` child of `StructTreeRoot` `/K`. Each element's `/S` is resolved through its `/NS` namespace and that namespace's `/RoleMapNS` to a standard type in `http://iso.org/pdf2/ssn`, `http://iso.org/pdf/ssn` or MathML.

### A.2 Identification and metadata (XMP) [R: `evidence/01-ua2-lualatex-mathml-se.txt`]

```text
pdfuaid:part        2            ISO 14289-2:2024 clause 5, test 2
pdfuaid:rev         2024         clause 5, test 5 ("shall be 2024")
pdfaid:part         4
pdfaid:conformance  F
pdfaid:rev          2020
dc:title            Tagged PDF Audit Probe        (from \title; clause 8.11.1)
dc:language         en-US
pdfd:conformsTo     http://pdfa.org/declarations/wtpdf#accessibility1.0
                    http://pdfa.org/declarations/wtpdf#reuse1.0
pdf:Producer        luahbtex-1.24.0
```

The WTPDF declarations are added automatically by `l3pdfmeta` when `pdfstandard=UA-2` is set: two `\pdfmeta_xmp_add_declaration` calls at `begindocument/before`, claimant "LaTeX Project", date = compile date. Disable with `xmp/wtpdf={reuse=false, accessibility=false}` (key path `@@/xmp` in l3pdfmeta) **[S: `l3pdfmeta.dtx` §"Default declarations"]**. The same file passes veraPDF profiles `ua2`, `4f` and `wt1a` (WTPDF 1.0 Accessibility) **[R]**.

### A.3 Standard properties enforced by `l3pdfmeta` [S: `l3pdfmeta.dtx` 0.97c/d]

| Standard | `name` | `year` (→ `rev`) | `min_pdf_version` | `max_pdf_version` | Extra requirements set |
|---|---|---|---|---|---|
| UA-1 | pdf/UA-1 | 2014 | 1.4 | 1.7 | `tagged`, `link_Contents` (link annotations need `/Contents`), `omit_CID` |
| UA-2 | pdf/UA-2 | 2024 | 2.0 | – | `tagged`; WTPDF declarations; `Trailer_no_Info` deliberately not forced ("only a should not a shall in UA-2") |
| A-4F | (A-4 family) | 2020 | 2.0 (A-4 "should use PDF 2.0") | – | `Catalog_EmbeddedFiles` (ISO 19005-4:2020 clause 6.9 test 5 `containsEmbeddedFiles`); dummy `pdf-A4f.txt` embedded if empty |

These are *metadata and version* actions. `l3pdfmeta` does not check structural accessibility requirements, and `UA-*` does not switch tagging on.

---

## B. Core entities, commands, and specifications

### B.1 Normative documents

| Document | Identifier | Role in the stack | Verification in this audit |
|---|---|---|---|
| PDF 2.0 | ISO 32000-2:2020 | Container syntax: logical structure, namespaces, associated files | Cited as `specification="ISO 32000-2:2020"` in 50 veraPDF UA-2 rule texts |
| PDF 1.7 | ISO 32000-1:2008 | Legacy target (`pdfversion=1.7`); default namespace semantics | Free copy hosted by Adobe; TOC read: 14.3 Metadata, 14.6 Marked Content, 14.7 Logical Structure, 14.8 Tagged PDF, 14.9 Accessibility Support |
| PDF/UA-2 | ISO 14289-2:2024 | Accessibility conformance for PDF 2.0 | 92 rules `ISO_14289_2` in veraPDF profile; failures reported as "ISO 14289-2:2024" |
| PDF/UA-1 | ISO 14289-1:2014 | PDF ≤ 1.7 accessibility (`pdfstandard=UA-1`) | veraPDF profile `ua1`; `l3pdfmeta` year 2014 |
| Structure namespace inclusion | ISO/TS 32005:2023 | Parent/child containment matrix (Table 5) | 1,636 rules `ISO_32005` in veraPDF UA-2 profile; failures reported as "ISO 32005:2023 … Table 5" |
| Well-Tagged PDF | WTPDF 1.0 (PDF Association) | Reuse and accessibility declarations | veraPDF profiles `wt1r`, `wt1a`; XMP declaration URIs |
| PDF/A-4 | ISO 19005-4:2020 | Archival (`A-4`, `A-4E`, `A-4F`) | veraPDF profile `4f`; `pdfaid:rev 2020` |
| MathML | W3C MathML 3 (namespace `http://www.w3.org/1998/Math/MathML`); W3C MathML Core / MathML 4 for consumers | Math structure elements and AF payload | ISO 14289-2 clause 8.2.4 lists the MathML namespace |

### B.2 ISO 32000-2:2020 clauses exercised by LaTeX output

| Clause / table (as cited in veraPDF profile text) | Subject | LaTeX producer |
|---|---|---|
| 14.3 | Metadata streams (XMP) | `l3pdfmeta` |
| 14.7, 14.7.2 + Table 323 | Logical structure; StructElem `/P` entry | tagpdf tree code |
| 14.8.5.7 | Table header cell association (TH scope/headers) | `latex-lab-table` (`/Scope` via attribute classes) |
| 14.8.6 | Standard structure namespaces (PDF 1.7, PDF 2.0, MathML) | tagpdf namespaces |
| 14.9.2 | Natural-language identification (`/Lang`, RFC 3066 Language-Tag) | `lang`/`language` keys, structure `lang` key |
| 14.9.3 | Alternate descriptions (`/Alt`) | `alt` keys |
| 9.9 | Embedded font programs | engines (all fonts embedded) |
| Table 31 | Page `/Tabs` | `page/tabsorder` (written as `/S`) |
| Table 167 | Annotation flags (Invisible → artifact) | hyperref/pdfmanagement annotations |
| Marked content (ISO 32000-1:2008 numbering: 14.6) | `BDC`/`BMC`/`EMC`, `MCID`, `/Artifact` | tagpdf MC code |
| Associated files (`/AF`, `/AFRelationship`); **[U]** ISO 32000-2 clause number not machine-verified (standard not freely fetchable; pdfa.org/iso.org refuse automated access) | MathML/TeX/CSS attachments | latex-lab-math, tagpdf |

### B.3 ISO 14289-2:2024 requirements and the LaTeX feature that satisfies each

| Clause / test | Requirement (abridged from veraPDF profile) | Satisfied by |
|---|---|---|
| 5 t1–t5 | PDF/UA identification in XMP; `pdfuaid:part` = part number; `pdfuaid:rev` = "2024" | `pdfstandard=UA-2` |
| 6.1 | File version 2.0 | `\DocumentMetadata` default 2.0 |
| 6.2 | `MarkInfo /Marked true` | `tagging=on` |
| 8.2.1 | Structure hierarchy rooted in StructTreeRoot (ISO 32000-2 14.7) | tagpdf tree |
| 8.2.2 | Content not considered real shall be an artifact | Lua mode `activate/tagunmarked`; pdfTeX after convergence |
| 8.2.4 t1–t4 | Namespaces/role maps: standard or MathML; no circular mapping; no same-namespace mapping | `latex` namespace + RoleMapNS |
| 8.2.5.2 | StructTreeRoot has a single `Document` child (Annex L, ISO/TS 32005) | `activate` key opens `Document` |
| 8.2.5.12 | `H` shall not be used | H1–H6 only |
| 8.2.5.20 | Link annotations enclosed in `Link` or `Reference` | hyperref + tagging |
| 8.2.5.25 | `Lbl` in lists requires `ListNumbering` on `L` | latex-lab block: attribute class `/itemize` = `/O /List /ListNumbering /Unordered` **[R]** |
| 8.2.5.26 | Tables shall be regular; no cell intersection | `table/multirow` suppression of covered cells |
| 8.2.5.28.2 | `Figure` needs `Alt` or `ActualText` | `alt=` / `actualtext=` keys |
| 8.2.5.29 | `math` only as child of `Formula` | latex-lab-math + luamml |
| 8.4.3 | PUA content needs ActualText/Alt; Alt/ActualText shall not contain PUA | author responsibility (symbol fonts) |
| 8.4.4 | Catalog `/Lang` non-empty | `lang`/`language` or babel fallback |
| 8.4.5.5.1, 8.4.5.8, 8.4.5.9 | Fonts embedded; every used code maps to Unicode; no `.notdef` | engine + glyphtounicode / OpenType |
| 8.8 | Internal destinations shall be structure destinations | tagpdf `activate/struct-dest` (default true with hyperref) |
| 8.9.3.3 | Pages with annotations need `/Tabs` A, W or S | `page/tabsorder` |
| 8.11.1 | XMP has `dc:title` | `\title{…}` via `latex-lab-title` |
| 8.11.2 | `ViewerPreferences /DisplayDocTitle true` | `latex-lab-title`: forced when `document/pdfstandard-UA` is non-empty (absent without a UA standard **[R: `modes/m4-on`]**) |
| 8.14.1 | `Desc` on file specs in the EmbeddedFiles name tree | l3pdffile (`Desc (mathml-1)`, `Desc (TeX source)`) |

---

## C. System interdependencies and package friction

### C.1 Legacy PDF vs PDF/UA-2 output: what veraPDF reports [R: `evidence/04-legacy-baseline.txt`]

The untagged baseline (identical body, no `\DocumentMetadata`) fails **9** UA-2 clauses:

| Clause | Failed checks | Legacy cause |
|---|---|---|
| 6.1 t1 | 1 | PDF 1.7 |
| 6.2 t1 | 1 | no `MarkInfo` |
| 8.2.1 t1 | 1 | no StructTreeRoot |
| 8.2.2 t1 | 41 | unmarked content |
| 8.4.4 t1 | 1 | no Catalog `/Lang` |
| 8.8 t2 | 2 | named destinations instead of structure destinations |
| 8.9.3.3 t1 | 1 | no page `/Tabs` (page has link annotations) |
| 8.11.1 t2 | 1 | no XMP metadata stream |
| 8.11.2 t1 | 1 | no `DisplayDocTitle` |

| Container feature | Legacy pdfLaTeX | Tagged pdfLaTeX/LuaLaTeX (2026-11-01) |
|---|---|---|
| Header version | 1.7 | 2.0 |
| Trailer `/Info` | present | absent with A-4F (`omitinfodict`) |
| Catalog keys | `/Names /OpenAction /Outlines /PageMode /Pages /Type` | adds `/StructTreeRoot /MarkInfo /Lang /Metadata /ViewerPreferences /AF /OutputIntents /PageLabels` |
| Logical structure | none | ~1.6k StructElems per 49 pages (AF route), ~4.7k with MathML SE **[R: `evidence/benchmark.txt`]** |
| Namespaces | n/a | 6 namespace dictionaries + fallback RoleMap |
| Destinations | named | structure destinations |
| Embedded files | none | MathML/TeX/CSS AFs (+ `pdf-A4f.txt` if required) |

### C.2 Validator semantics and their limits

| Observation | Implication |
|---|---|
| `multirow` table without `RowSpan` passes veraPDF ua2 **[R]** | A machine PASS does not prove semantic correctness. Many UA requirements are human checks ("lots of the requirements for PDF/UA-1 and PDF/UA-2 are human checks", tagpdf manual §"Validation woes") |
| `soul` fails ISO/TS 32005 Table 5 (`P` ⊅ `P`, `P` ⊅ `Part`) **[R]** | The containment matrix catches structural nesting errors only |
| pdfTeX pass 1 fails 8.2.2, pass 2 passes **[R]** | Validate only converged output |
| Missing `\title` fails 8.11.1 **[R]** | Metadata clauses depend on author markup |
| PAC: "for PDF 1.7 and older"; PAC no longer crashes on PDF 2.0 (early 2025) **[S: tagpdf manual]** | Use veraPDF (PDF/UA-2 + ISO/TS 32005) as the reference validator for PDF 2.0 |
| UA-1 vs UA-2 version window (UA-1 ≤ 1.7, UA-2 = 2.0) | One file cannot claim both. A conflicting explicit `pdfversion` placed *after* `pdfstandard` survives with only a warning (`PDF version 1.7 is too low for standard 'pdf/UA-2'`) **[R: `evidence/version-standard-conflict.txt`]** |

### C.3 Spec-level design tension that LaTeX resolves locally

| Tension | Spec position | LaTeX handling |
|---|---|---|
| Nested paragraphs | `P` may not contain `P` (Table 5) | `semantic-para` → `Part`, `text-block` → `P` |
| Document title | PDF 2.0 `Title` exists, but has no defined AAPI mapping; "Deriving HTML from PDF 1.2" maps it to `<div>` | `title` key: `Title` (default) / `H1` / `html` / `aria` |
| Custom vocabulary | Role mapping within one explicit namespace is prohibited (UA-2 8.2.4 t3) | Separate `latex`/`latex-book`/`user` namespaces |
| Readers without namespace support | PDF 2.0 namespaces are optional for consumers in practice | Fallback `/RoleMap` duplicated into StructTreeRoot |
| Math | PDF 2.0 allows MathML namespace elements and AFs | Both offered; MathML placed first in `/AF` |

---

## D. Verified primary sources

| Source | URL / location | Verification |
|---|---|---|
| ISO 32000-2:2020 (PDF 2.0) OBP entry, cited by tagpdf bibliography | https://www.iso.org/obp/ui/#iso:std:iso:32000:-2:ed-2:v1:en | cited by primary source; iso.org refuses automated fetch (403) |
| PDF 2.0 no-cost access programme (PDF Association) | https://pdfa.org/resource/iso-32000-2/ | pdfa.org refuses automated fetch (403); URL not verified |
| ISO 32000-1:2008 (PDF 1.7), Adobe-hosted copy | https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf | downloaded; table of contents read |
| veraPDF validation profiles (PDF/UA-2 + ISO/TS 32005, WTPDF) | https://github.com/veraPDF/veraPDF-validation-profiles | verified (`git ls-remote`); `PDFUA-2-ISO32005.xml` read from veraPDF 1.30.3 jar |
| veraPDF documentation | https://docs.verapdf.org/validation/ ; https://verapdf.org/ | fetched (200) |
| `l3pdfmeta` documentation (standards, XMP, WTPDF declarations) | https://texdoc.org/serve/l3pdfmeta/0 | PDF fetched; source read |
| W3C MathML Core / MathML 4 | https://www.w3.org/TR/mathml-core/ ; https://www.w3.org/TR/mathml4/ | fetched (200) |
| LaTeX tagging project WTPDF examples (STEM article with AF and SE) | https://latex3.github.io/tagging-project/documentation/wtpdf/ | fetched (200) |
| tagpdf manual §"References and target PDF version", §"Validation" | https://texdoc.org/serve/tagpdf/0 | PDF fetched |
