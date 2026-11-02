# Changes

Every change to what a document produces is listed under *Changed output*.
[doc/compatibility.md](doc/compatibility.md) says what stays the same.

## 0.2.0

The release from which the compatibility promise holds.

### Added

- **Bibliographies from BibTeX.**
  - `bibliography "refs.bib"` reads BibTeX databases as they are.
  - `[@key]`, `[@a; @b]` and `[@key, Thm. 2]` cite: sorted, ranged and linked.
  - Only cited entries are printed, in the style of the AMS.
  - `citations alphabetic` gives amsalpha labels (Hoo71).
- **Journal front matter.**
  - `author "Name", affiliation: "…", email: "…", orcid: "…"`.
  - `msc` and `keywords`, printed after the abstract.
  - `numbering by section` (Theorem 2.3, (2.3)).
  - `appendix` (Appendix A, Lemma A.1).
  - Unnumbered `theorem*`.
- **Packages.**
  - `use NAME [VERSION]`.
  - `tekx pkg` adds, installs and verifies packages, from directories, git
    repositories and private registries.
  - Packages are pinned by SHA-256 in `tekx.lock`.
  - Packages may ship filters (programs that rewrite the document tree) and
    the custom blocks those filters implement.
- **Daily use.**
  - `tekx -w` rebuilds on every change.
  - `tekx new NAME` starts a paper.
  - A `tekx 0.2` line declares the version a document is written for.
- **Archival PDF.**
  - Every PDF is PDF/A-4 (PDF/A-4f when it carries files), as well as PDF/UA-2
    and WTPDF, with a generated sRGB output intent.
- **Habits from LaTeX are understood.**
  - `Theorem @main` reads "Theorem 1.1".
  - `(@eq)` reads "(2.1)".
  - `Lemmas @a and @b` reads "Lemmas 2.2 and 2.3".
- **Errors instead of silence.**
  - A line that looks like a setting but isn't one is an error with a
    suggestion, and so is a misspelled `appendix`.
  - A citation key that is also a label is an error.
  - So are an unknown author detail, a missing `.bib` file and a custom block
    no filter replaced.
- **Tests.**
  - The tag tree is pinned by golden files, because a validator can't judge
    meaning.
  - `.bib` files and package scenarios are tested, under the sanitizers.

### Changed output

- **Fractions.** A fraction written `a/b` is set on the line outside display
  style: in running text, in scripts and inside stacked fractions. Compound
  parts get parentheses, so `x^(1/4)` gives x^{1/4}. `frac`, `over` and
  `\frac` still stack. The MathML of such a fraction is a row with a `/`, not
  an `mfrac`.
- **MathML spacing.** There is no space around an ordinary `/`, and a thin
  space follows `log` in `log x`.
- **Reference entries.** Entries are labelled in their own namespace
  (`cite:KEY`). A bare `@key` names a `#label` when one exists.
- **Keywords.** `keywords` are printed after the abstract, as well as stored
  in the PDF.
- **Numbering.** All numbers come from one numbering pass shared by the PDF
  and HTML back ends.

### Fixed

- A use-after-free in the PDF writer's name tree, found by fuzzing before
  release.
- Inflate's handling of a corrupt Huffman code now has a regression test.

## 0.1.0

The first release.
- A pipeline of six small filters (`pp`, `parse`, `eqn`, `set`, `pdf`,
  `html`) in C17, with no dependencies beyond libc.
- Mathematics written the way it is spoken, or in LaTeX notation, typeset by
  TeX's rules with OpenType MATH fonts.
- Knuth–Plass line breaking with Liang hyphenation.
- Tagged PDF 2.0 that validates as PDF/UA-2 and WTPDF, with MathML structure,
  spoken alternatives and copy text that reads back as the same formula.
- Figures, footnotes, theorems, references and contents.
- An HTML and MathML back end.
- `tekx explain`.
