# Compatibility

A paper is written once and built for years. It is built again for each
revision, for the journal, for the arXiv, and for the reader who asks for
the source a decade later. tekx makes the following promises about that.
The test suite enforces them.

## The promise, from tekx 0.2

1. **Documents keep building, with the same meaning.** A document that builds
   with tekx 0.2 builds with every later release, and these stay the same:
   - its structure (the tags a screen reader is given);
   - its numbering, cross-references and citations;
   - every formula's MathML, speech and copy text.

   Typography may improve, so line and page breaks can move. The meaning does
   not change.
2. **A document can say which tekx it was written for.** The first line
   `tekx 0.2` declares it. An older tekx refuses such a document with a clear
   message rather than guessing. When a later release has to change
   behaviour, documents that declare an earlier version keep the earlier
   behaviour for at least the next major release.
3. **Nothing disappears without warning.** A feature is deprecated, with a
   warning that says what to write instead, for at least one minor release
   before a major release removes it.
4. **The same inputs give the same bytes.** These produce an identical PDF:
   - the same tekx release;
   - the same sources, including `tekx.lock` and the packages it pins;
   - the same `SOURCE_DATE_EPOCH`.
5. **The formats between stages are interfaces.**
   - The page description declares its version (`x tekx-pdl 1`).
   - The document tree (Format 2 in [machinery.md](machinery.md)) is what
     package filters read and write. Minor releases change it only by
     addition: new node kinds and new optional children. A filter must pass
     nodes it does not know through untouched.
6. **Packages stay pinned.**
   - The `tekx.lock` format is stable.
   - A package's `requires` names the oldest tekx it supports, and is honoured.
   - A build never uses package content other than what the lock pins.
7. **The output meets its standards.** Every release's output validates as:
   - PDF/UA-2;
   - WTPDF 1.0, for accessibility and for reuse;
   - PDF/A-4 (PDF/A-4f when the document carries files).

   The test suite checks this with veraPDF.

## How the promise is kept

- The golden files are the compatibility contract:
  - `test/golden/*.out`: the document tree, each formula's MathML, speech and
    copy text, and the tag tree;
  - `test/tour.structure`;
  - `test/explain.out`;
  - the expected diagnostics in `test/errors`.

  A change to any expected output is deliberate. It is reviewed, and listed in
  [CHANGELOG.md](../CHANGELOG.md) under *Changed output*.
- Every example is built twice and must give the same bytes. The pipeline run
  by hand must equal the driver. Copy text must read back as the same
  formula.

## What tekx is compatible with

| from | how |
|---|---|
| LaTeX mathematics | `\frac`, `\sqrt`, environments, Greek, relations and arrows inside any formula, read as tekx reads its own notation (see [language.md](language.md#latex-notation)) |
| BibTeX | `.bib` files read as they are: macros, concatenation, accents, mathematics, all three name forms |
| LaTeX documents | `tekx import paper.tex` converts an article; `tekx -o paper.tex` writes one for submission |
| OpenType fonts | any font with a MATH table, for mathematics (`font math FILE`) |
| PDF readers | tagged PDF 2.0 with MathML structure and associated files, readable by every PDF/UA-2 consumer, with a spoken alternative for those without MathML |

## Before 0.2

tekx 0.1 was the first release. The promise starts with 0.2. The changes
between them that alter output (on-the-line fractions in running text,
numbering through one shared pass, references and citations in separate
namespaces) are listed in the changelog.
