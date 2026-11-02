# Layer 7: Rendering/viewer: clipboard engines, structure-tree parsers, accessibility hooks

Scope: what consumers do with the produced container. This covers text extraction for copy/paste and search, structure-tree parsing and role resolution, the bridge to platform or DOM accessibility, and MathML delivery to assistive technology. Behaviour is documented from source code and measurements where possible. Closed-source viewers are documented only where a primary source states behaviour, and are otherwise marked **[U]**.

Measured consumers: **pdf.js 6.5.438** (`pdfjs-dist`, the PDF engine of Firefox), **poppler 24.02.0** (`pdftotext`), and the **PDFium** public structure-tree API (header `public/fpdf_structtree.h`, main branch).

Evidence tags: **[S]** source/official docs, **[R]** reproduced (`evidence/`), **[U]** under-documented.

---

## A. Technical mechanics and data flow

### A.1 Two independent consumption paths

```text
                       ┌───────────── text path (clipboard, search, reflow) ─────────────┐
 content stream ──────►│ glyph codes → /ToUnicode CMap → [content-stream /ActualText?]   │──► string
 (BDC/EMC, MCID,       │ word gaps → real U+0020 glyph or heuristic space inference       │
  Tj/TJ, Tf)           │ line ends → hyphen handling (U+002D / U+00AD / dehyphenation)    │
                       └───────────────────────────────────────────────────────────────────┘
                       ┌───────────── semantic path (screen readers, HTML derivation) ────┐
 StructTreeRoot ──────►│ /S → role resolution (RoleMap / RoleMapNS) → platform role      │──► AT
 ParentTree, MCIDs     │ /Alt, /ActualText, /Lang, attributes (/A, /C→ClassMap)            │
 /AF (MathML)          │ Formula → MathML (AF payload or MathML-namespace children)        │
                       └───────────────────────────────────────────────────────────────────┘
```

The paths are decoupled: a value set in one can be invisible in the other (see A.3 and C.1).

### A.2 pdf.js 6.5.438: verified internals [S: `build/pdf.worker.mjs`, `web/pdf_viewer.mjs`]

**Structure-tree parser (worker):**

- `class StructTreeRoot` constructor reads only `rootDict.get("RoleMap")` into a `Map`. Role resolution is single-level: `get role() { … return root.roleMap.get(name) ?? name; }`. `/Namespaces` and `/RoleMapNS` are not consulted when building the struct tree for display.
- `get attributes()` resolves attribute classes (`/C` → `StructTreeRoot /ClassMap`) and inline `/A`.
- `get tableAttributes()` (roles `Table`, `TH`, `TD`, owner `/O /Table`): `RowSpan`→`rowSpan`, `ColSpan`→`colSpan` (integers > 1), `Headers`→`headers`, `Scope` (`Row`/`Column`/`Both`) on `TH`, `Short`, `Summary`.
- Serialiser: `alt` = `/Alt`, else `/ActualText`; `lang`; `structId` on `TH`; table attributes; for `Formula`, `mathML` = the **first** `/AF` entry with `/AFRelationship /Supplement` whose `EF` stream has `/Subtype /application/mathml+xml`, else an attribute with `/O /MSFT_Office` key `MSFT_MathML`.

**Text extraction (`getTextContent`, worker):** for `BDC` it records only `{type:"beginMarkedContentProps", id:<page>_mc<MCID>, tag}`. A `/ActualText` in the property list is **not** substituted into the extracted text.

**Accessibility DOM (`StructTreeLayerBuilder`, viewer):**

| PDF role (after RoleMap) | ARIA role set | PDF role | ARIA role set |
|---|---|---|---|
| `P` | `paragraph` | `L` / `LI` | `list` / `listitem` |
| `Part`, `Sect`, `Div`, `Sub` | `group` | `Table` / `TR` | `table` / `row` |
| `H1`…`Hn` | `heading` + `aria-level=n` | `TH` | `columnheader`, or `rowheader` if `scope="Row"` or (TR in TBody) |
| `H` | `heading` | `TD` | `cell` (+ `aria-rowspan`/`aria-colspan`) |
| `Figure` | `figure` (image placed in text layer) | `Link` | `link` |
| `BlockQuote`, `Code`, `Em`, `Strong` | `blockquote`, `code`, `emphasis`, `strong` | `Aside`, `Note`, `FENote`, `Annot` | `note` |
| `NonStruct` | `none` | `Title`, `Lbl`, `Span`, `Formula`, `LBody`, `Document`, `Artifact` | no role |

- MathML-namespace roles (26 names: `math`, `mi`, `mn`, `mo`, `mrow`, `msup`, `msub`, `msubsup`, `mfrac`, `msqrt`, `mroot`, `mtable`, `mtr`, `mtd`, …) are created as real MathML DOM elements (`createElementNS(MathMLNamespace, role)`), which take over the associated text-layer spans.
- A `Formula` with `mathML` (from the AF) is filled via `element.setHTML(mathML, {sanitizer})`, restricted to the MathML element/attribute allow-list (`intent` and `arg` are allowed). The text-layer spans are hidden. This path runs **only if the browser supports the Sanitizer API** (`FeatureTest.isSanitizerSupported`).

### A.3 Measured clipboard behaviour [R: `evidence/clipboard.txt`]

Probe body: ligatures, a narrow paragraph forcing three hyphenations, `$\alpha\le\beta$`, and a graphic with `actualtext={X}`.

| Probe | poppler `pdftotext -raw` | pdf.js `getTextContent()` |
|---|---|---|
| Ligatures `office affine flow` (all variants) | intact | intact |
| Line-end hyphen, legacy / tagged pdfTeX / tagged LuaTeX (default `artifact`) | `in-` | `in-` |
| Line-end hyphen, LuaTeX `activate/softhyphen=char` | `in\xad` (U+00AD) | – |
| `$\alpha$`, pdfTeX (CM + glyphtounicode) | `α` U+03B1 | `α` |
| `$\alpha$`, LuaTeX + unicode-math | `𝛼` U+1D6FC | `𝛼` |
| `\includegraphics[actualtext={X}]` (structure-level `/ActualText` on `Span`) | text drawn inside the image (`Image`) | `Image`; struct tree exposes `Span alt="X"` |
| Content-stream span `/Span <</ActualText (XYZ)>> BDC abc EMC` (`examples/clipboard/c5-actualtext-span.tex`) | **`XYZ`** (substituted) | **`abc`** (not substituted) |

The default `pdftotext` mode (without `-raw`) additionally joins hyphenated line ends: the default-mode output reads `internationalization` **[R]**.

### A.4 Measured structure-tree view (pdf.js `getStructTree()`) [R]

```text
Root
  Document
    Sect                      ← section        (fallback /RoleMap)
      H1                      ← section-heading
        Lbl                   ← heading-number
      Part                    ← semantic-para
        P                     ← text-block
          Formula mathML="<math xmlns=…> <mi>𝛼</mi>…"   ← MathML AF (LuaLaTeX default)
          Span alt="X"        ← /ActualText promoted to alt
```

For `math/setup=mathml-SE`, `getStructTree()` returns `Formula → math → msup → mi/mn …` and the viewer builds a native MathML DOM. For native tables: `TH scope="Column"`, `TD rowSpan=2` → `aria-rowspan`. For the `multirow` package: plain `TD`s with no span **[R: `evidence/06-08-table-semantics.txt`]**.

### A.5 Other consumers

| Consumer | Verified behaviour | Basis |
|---|---|---|
| PDFium (Chromium PDF engine) | Public API exposes per-page structure trees: `FPDF_StructTree_GetForPage`, `FPDF_StructTree_CountChildren/GetChildAtIndex`, `FPDF_StructElement_GetType`, `…GetObjType`, `…GetAltText`, `…GetActualText`, `…GetLang`, `…GetTitle`, `…GetID`, `…GetMarkedContentID`, `…GetMarkedContentIdCount/AtIndex`, `…GetParent`, attribute accessors `FPDF_StructElement_Attr_*`. No accessor for `/AF` or `/NS` appears in the header | **[S: `fpdf_structtree.h`]**. Chrome's use of these for MathML/namespaces: **[U]** |
| Foxit (PDF/UA-2 PoC) | For `Formula`: use only AFs with `AFRelationship == Supplement` **and** media type `application/mathml+xml`; accept AF as array or dictionary; if no suitable AF, process kids; `InlineMath` has priority over `AF`; `AF` has priority over `Alt`; a `<math>` child → substructure used, `Alt` ignored; `Alternative` relationship not implemented | **[S: github.com/foxitsoftware/PDF_UA-2 README]** |
| Foxit + NVDA + MathCAT | "Foxit can handle MathML associated files and to some extent MathML structure elements and, together with development versions of NVDA and MathCat, reading of equations is already quite good" (beginning of 2025) | **[S: tagpdf manual]**; current release state **[U]** |
| Foxit (older behaviour noted by LaTeX team) | Saw only the first AF; ignores `Lbl` when MathML is used → put equation numbers inside the MathML | **[S: latex-lab-math §Associated Files]** |
| Adobe Acrobat / Reader | Honours `/ActualText` for copy-paste ("Adobe Reader does, Sumatra does not"); `/Alt` and `/ActualText` affect text-to-speech only in fully tagged documents | **[S: tagpdf manual §"Alternative text, ActualText …"]**; PDF 2.0 namespace/MathML handling **[U]** |
| ngPDF (Duallab) | Converts tagged PDF to HTML using PDF Association derivation rules; reads CSS AFs (`.css` → `<link>`, `.html` `<style>` blocks → `<head>`) | **[S: tagpdf manual §"CSS style definition for derivation to html"]** |
| `texlive.net/showtags` | LaTeX-team tool: structure as XML, schema validation | **[S: tagpdf manual §Validation]** |
| PAC 2026 | Suitable "only for PDF 1.7 and older"; no longer crashes on PDF 2.0 | **[S: tagpdf manual]** |
| Generic viewers, multiple spaces | "The PDF viewers I tried all copied four real space glyphs as one space" | **[S: tagpdf manual §"Real space glyphs"]** |

---

## B. Core entities, commands, and specifications

| Entity | Consumer | Purpose |
|---|---|---|
| `page.getTextContent({includeMarkedContent:true})` | pdf.js | Text items + marked-content begin/end with MCID ids |
| `page.getStructTree()` | pdf.js | Serialised tree: `role`, `alt`, `lang`, `mathML`, `scope`, `rowSpan`, `colSpan`, `headers`, `summary`, `structId`, `bbox` |
| `doc.getMetadata()` | pdf.js | `info.Language`, `PDFFormatVersion`, XMP |
| `PDF_ROLE_TO_HTML_ROLE`, `MathMLElements`, `MathMLSanitizer` | pdf.js viewer | Role mapping, MathML allow-list |
| `FPDF_StructTree_*`, `FPDF_StructElement_*` | PDFium | Structure access for embedders |
| `pdftotext -raw`, `-layout`, default mode | poppler | Content-stream order, layout preservation, dehyphenated reading order |
| `/ActualText` (content-stream `BDC` property list) | poppler: honoured; pdf.js text: not honoured; Acrobat: honoured | Clipboard replacement text |
| `/ActualText`, `/Alt` (structure element) | pdf.js: exposed as `alt`; Foxit: AF > Alt | Accessible name |
| `/AF` with `/AFRelationship /Supplement`, `/Subtype /application/mathml+xml` | pdf.js, Foxit | MathML delivery |

Reproduction commands:

```bash
pdftotext -raw -enc UTF-8 file.pdf -
```

```bash
PDFJS=/path/to/node_modules/pdfjs-dist node tools/pdfjs_probe.mjs file.pdf
```

```bash
python3 -I tools/inspect_pdf.py file.pdf tree
```

---

## C. System interdependencies and package friction

### C.1 Producer choices vs consumer behaviour

| Producer decision (upper layers) | Consumer effect (measured or sourced) | Recommendation |
|---|---|---|
| LaTeX tag names in a custom namespace (Layer 2) | pdf.js ignores RoleMapNS; resolves via fallback `/RoleMap` → standard roles | Keep the fallback RoleMap (tagpdf default) |
| `title-text` → `Title` (default) | pdf.js: no ARIA role; fallback maps `Title→P` | `tagging-setup={title=H1}` for navigable titles |
| MathML as AF (LuaLaTeX default) | pdf.js: rendered only with Sanitizer API support; Foxit: preferred over Alt | Combine `mathml-SE` + `mathml-AF` when both consumer types matter: `math/setup={mathml-SE,mathml-AF}` |
| MathML as SE | pdf.js: native MathML DOM, independent of Sanitizer | 1 pass, larger files (+21% vs AF in benchmark) |
| ActualText on structure elements (graphics `actualtext=`) | Exposed as accessible name; **not** used by pdf.js or poppler for clipboard | Do not rely on it for copy-paste |
| ActualText in content stream (HarfBuzz clusters, `accsupp`) | poppler and Acrobat substitute; pdf.js 6.5.438 does not | Prefer correct ToUnicode where a 1:1 mapping exists |
| Real space glyphs (`activate/spaces`) | Removes heuristic word-boundary inference; runs of spaces still collapse in viewers | Keep on (default with `tagging=on`) |
| Soft hyphen `artifact` (LuaTeX default) | Naive extractors still copy `-`; structure-aware AT can skip the artifact | `char` mode emits U+00AD (risk: some fonts render U+00AD invisibly) |
| unicode-math italic letters | Clipboard yields Math Alphanumeric code points (`𝛼`), not `α` | Expected Unicode semantics; search behaviour of viewers **[U]** |
| Table headers/spans via attribute classes | pdf.js resolves `/C`; PAC reports missing attributes (false positive) | Validate with veraPDF; inspect with pdf.js |

### C.2 Legacy vs tagged at the viewer

| Viewer capability | Legacy PDF | Tagged PDF/UA-2 (2026-11-01) |
|---|---|---|
| Screen-reader structure | none: reading order = content order | `Document/Sect/H1/Part/P/L/Table/Figure/Formula` tree with ARIA roles (pdf.js) |
| Headings navigation | not available | `H1`–`H6` (`aria-level`) |
| Table navigation | not available | `columnheader`/`rowheader`, `aria-rowspan`/`aria-colspan` |
| Image description | none | `/Alt` on `Figure` |
| Math | glyph soup (`α ≤ β` or `𝛼 ≤ 𝛽`) | MathML (AF or SE) for MathML-aware consumers |
| Document language | none | `/Lang` → `info.Language` (`en-US`) |
| Window title | file name | `DisplayDocTitle` + `dc:title` |
| Clipboard: spaces | heuristic | real glyphs |
| Clipboard: page furniture | page numbers and headers mixed into text | marked `/Artifact` (ignored by AT; still extracted by poppler/pdf.js text APIs) |

### C.3 Open/under-documented consumer behaviour

- Adobe Acrobat handling of PDF 2.0 `/Namespaces`, `RoleMapNS` and MathML AF: no primary source reviewed in this audit **[U]**.
- Chrome (PDFium) mapping of MathML-namespace structure elements and AFs to platform accessibility APIs: the public header lacks AF/namespace accessors; internal Chrome behaviour **[U]**.
- NVDA/JAWS release-channel support for MathML delivered through PDF viewers: only the tagpdf manual's early-2025 statement on development versions was available **[U]**.
- pdf.js MathML-from-AF rendering in browsers without the Sanitizer API: code path is skipped; no fallback observed in source **[S]**, user-visible effect per browser **[U]**.

---

## D. Verified primary sources

| Source | URL / location | Verification |
|---|---|---|
| pdf.js repository | https://github.com/mozilla/pdf.js | verified (`git ls-remote`) |
| pdf.js project page | https://mozilla.github.io/pdf.js/ | fetched (200) |
| `pdfjs-dist` 6.5.438 (npm) | `npm install pdfjs-dist@6.5.438` | installed; `build/pdf.worker.mjs` and `web/pdf_viewer.mjs` read |
| PDFium structure-tree API | https://pdfium.googlesource.com/pdfium/+/refs/heads/main/public/fpdf_structtree.h ; mirror https://github.com/chromium/pdfium | fetched (200); repo verified |
| poppler `pdftotext` | poppler-utils 24.02.0 (Ubuntu 24.04) | executed |
| Foxit PDF/UA-2 PoC (MathML AF processing rules) | https://github.com/foxitsoftware/PDF_UA-2 | verified; README fetched via raw.githubusercontent.com |
| MathCAT | https://github.com/NSoiffer/MathCAT | verified (`git ls-remote`) |
| NVDA | https://www.nvaccess.org/ ; https://github.com/nvaccess/nvda | fetched (200); repo verified |
| ngPDF | https://ngpdf.com/ | fetched (200) |
| LaTeX structure viewer | https://texlive.net/showtags | fetched (200) |
| PAC (PDF Accessibility Checker) | https://pac.pdf-accessibility.org/ | fetched (200) |
| WAI-ARIA 1.2 (role vocabulary used by pdf.js) | https://www.w3.org/TR/wai-aria-1.2/ | fetched (200) |
| tagpdf manual (consumer notes: Acrobat, Foxit, NVDA, PAC, ngPDF) | https://texdoc.org/serve/tagpdf/0 | PDF fetched |
