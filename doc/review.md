# tekx under review

This document argues against tekx as hard as its strongest critics would,
and then argues for it. Each criticism is stated in its strongest form,
checked against the program, and marked:

- **fixed:** a real problem, solved; the commit and the test that guards it are named;
- **answered:** a real concern, met by evidence or by design;
- **open:** still true, with the plan for it.

Every problem marked fixed was reproduced before it was fixed, and a test now
holds each fix in place.

## How the criticisms were found

Each kind of critic got their own attack:
- the language was probed with the inputs people actually write;
- LaTeX was pasted in as-is;
- the accessibility claims were checked against the source code of pdf.js;
- the compiler was fuzzed with 42,000 mutated documents, images and BibTeX
  databases under AddressSanitizer and UBSan;
- a journal paper was written the way a mathematician would write it, and
  every place tekx failed or got in the way was recorded;
- copy and paste was measured in the engines behind Chrome, Firefox and
  Evince;
- the same document was built with the conventional LaTeX toolchain on the same
  machine, for comparison.

## 1. The language

**"Natural-language mathematics is ambiguous. I cannot tell what I will get."**
*Answered* (d3c7845; test/explain.txt).
- The grammar is a precedence parser with a documented table
  (doc/language.md), and spacing groups by rule.
- `tekx explain` shows the reading with every implicit grouping written out:

  ```
  $ tekx explain 'sin x/y + 1/2 x'
  reads as   sin(x/y) + (1/2)·x
  says       sine the fraction x over y end fraction plus one half x
  ```

- Speech says where a fraction ends when that would otherwise be unclear:
  `a/b c` is "the fraction a over b end fraction c".
- A golden test pins these readings, so they cannot drift.

The criticism keeps some of its edge. Spacing that changes meaning (`1/2x`
against `1/2 x`) is a rule users must learn. `explain` makes the rule visible,
but doesn't make it unnecessary.

**"`$5 and $10` turns into a formula."**
*Fixed* (12c79d5; test/golden/language.tekx). Inline mathematics follows a
stricter form of Pandoc's rule:
- the opening `$` must touch the formula;
- the next `$` must close it, with no blank before it and no digit after it;
- otherwise the `$` is a dollar sign.

**"A word inside a formula, such as velocity, becomes v·e·l·o·c·i·t·y, and the
screen reader spells it out."**
*Fixed* (12c79d5; test/formulas.txt).
- An unknown word of four or more letters is one upright name, and speech reads
  it as a word.
- Words of three letters or fewer stay products of letters: `abc` is `a·b·c`.
- TeX notation keeps TeX's own rule, under which a run of letters is a product.

**"Nobody will rewrite their mathematics. Every mathematician thinks in
LaTeX."**
*Fixed* (4510bbc; test/golden/tex.tekx, test/errors/tex.tekx). A formula may be
written in LaTeX notation:
- `\frac`, `\sqrt[n]`, `\binom`, `\text`;
- `\mathbb` and the other styles, and `\operatorname`;
- every Greek letter, relation and arrow;
- `\left` and `\right`, `\pmod`;
- the pmatrix, cases, aligned and array environments.

It gives the same tree, MathML, speech and copy text as tekx notation.
- An unknown command gets a suggestion: *unknown TeX command \fracc; did you
  mean \frac?*
- The copy text of TeX input reads back as the same formula.

**"Significant indentation is fragile: a display after a theorem can end up
inside the theorem."**
*Answered* (86ca947). The rule is inherent: a block's body is everything
indented more deeply than its header.
- tekx now warns when a block ends in a display after a blank line, indented
  only a little past the block's text. That is the pattern that usually means
  the display was meant to stand outside.
- The warning found two such displays in tekx's own examples. Both had been
  silently tagged as part of the block before them.
- `do … end`, or a line of prose between the block and the display, removes
  the ambiguity.

**"A probe paper written the way a mathematician writes fails in ways no
validator sees."**
*Fixed* (8616514; test/golden/journal.tekx, test/errors/journal.tekx). The
probe found lines that were silently set as prose:
- `author "…", affiliation: …` became a mangled name;
- `msc`, `numbering` and `appendix` became paragraphs;
- `[@landau]` resolved to the *equation* labelled `#landau`.

Each is now either a feature or an error with a suggestion:
- author details, MSC, numbering by section and appendices are features;
- an unknown setting is an error with a suggestion;
- citation keys and labels live in separate namespaces, and a bare `@key`
  that could be either is an error.

LaTeX habits are understood: `Theorem @main` reads "Theorem 1.1", not
"Theorem Theorem 1.1".

**"Without a specification, a language is whatever the one implementation does
this week."**
*Partly answered* (d036b13).
- doc/compatibility.md promises what stays true from 0.2 on: structure,
  numbering, references and the meaning of every formula.
- A `tekx 0.2` line declares the version a document is written for.
- The golden files are the contract, and CHANGELOG.md lists every change to
  output.
- **Open:** a versioned specification separate from the implementation, and
  the golden tests published as a conformance suite.

## 2. Mathematics and typography

**"TeX has forty years of typographic refinement. This cannot match it."**
*Partly answered, partly open.*
- **Answered:** tekx uses the same algorithms as TeX:
  - formula layout follows TeX's Appendix G, driven by the font's OpenType MATH
    constants;
  - paragraphs are broken by Knuth and Plass's total-fit algorithm;
  - words are hyphenated by Liang's algorithm with TeX's US English patterns.
- **Fixed:** fractions written `a/b` are set on the line in running text and
  scripts, and stacked only in displays (x^{1/4}, (1 + √5)/2), as journals set
  them. Before, inline fractions stacked and disturbed the line spacing.
- **Open:** none of these exist yet:
  - microtypography (character protrusion and font expansion);
  - line breaking inside displayed equations, so a long display overflows;
  - breaks inside a parenthesised group in an inline formula, which TeX
    allows after a binary operator;
  - testing with math fonts other than STIX Two.
- **Plan:** protrusion first, as the most visible gain, then display breaking.

**"Speech generated by ad hoc rules will be wrong, and English only."**
*Answered in part.* tekx's speech is a fallback, not the main channel:
- Software that understands MathML ignores it, and reads the MathML with its
  own speech rules (MathCAT, for example) in the reader's own language.
- The audit established from the pdf.js source that pdf.js drops a Formula's
  `/Alt` when MathML is attached, and renders the MathML natively.
- Foxit also prefers the MathML to `/Alt`.
- Only software without MathML support, such as Acrobat, reads tekx's speech.

**Open:** the speech is English and follows no published style (ClearSpeak or
MathSpeak).
- **Plan:** align with ClearSpeak, and choose the speech language from the
  document's language.

## 3. Accessibility

**"Passing veraPDF is not the same as being accessible."**
*Fixed as far as a test can reach; open where only people can judge.*
- The criticism is correct. Finding 6 of the audit
  (research/latex-tagged-pdf-audit) shows LaTeX tables that pass veraPDF with
  their row spans missing.
- veraPDF still earns its place: run on every example, it caught two structural
  errors during development, and both are fixed:
  - a figure caption placed directly under `Document`;
  - an equation number labelled directly under `Document`, which happened only
    in documents without sections.
- What a validator cannot judge is whether a well-formed tree means the right
  thing, so the suite now pins the tree itself (this release;
  test/structure.awk, test/tour.structure and the structure section of every
  golden file). A person reviewed these outlines once and the tests keep them
  that way:

  ```
  pdf2:Link link="Equation 1" to=Formula
  pdf2:Link link="Footnote 1" to=FENote
  pdf2:Aside
    pdf2:Figure alt="The parabola y equals x squared: …"
    pdf2:Caption
  ```

- Links carry spoken names ("Equation 1", "Reference 2", "Footnote 1") instead
  of "(1)" and "[2]" (12c79d5).

**Open:** nothing has yet been tested by blind readers or with real screen
readers (NVDA with MathCAT, JAWS, VoiceOver).
- **Plan:** a written test protocol, then sessions with screen-reader users.
  This is the most important item in this document.

**"Copy and paste from a PDF is broken in the viewers people actually use."**
*Fixed for Chrome and Evince, nearly so for Firefox* (9c1e743;
test/paste.sh). Measured with the engines themselves:

| pastes back as itself | poppler (Evince) | PDFium (Chrome, Edge) | pdf.js (Firefox) |
|---|---|---|---|
| 50 formulas, inline and displayed | 50 | 50 (was 0) | 47 |
| hyphenated words | whole | whole (was missing) | split |

- PDFium reads ActualText only from the innermost marked-content level. tekx
  now repeats it there, under a property poppler ignores. This was verified
  against both engines' source.
- pdf.js ignores ActualText, so its remaining failures are listed in
  `test/paste/limits`.
- Acrobat and macOS Preview could not be tested here.

**"PDF is a poor format for accessible mathematics. The web is better."**
*Fixed* (dc08546; step 7 of test/run.sh). `tekx -o paper.html paper.tekx`
writes the same document as semantic HTML with native MathML.
- It comes from the same tree and the same numbering pass (number.c).
- The PDF and the page therefore cannot disagree.

**"Accessible today, unreadable in thirty years."**
*Fixed* (this release; test/run.sh with `VERAPDF`). Every tekx PDF is now also
PDF/A-4, the archival standard:
- an sRGB output intent, with the ICC profile written out from the sRGB primaries
  and tone curve rather than shipped as a binary;
- PDF/A-4f when the document carries files (each formula's MathML and source),
  with those files listed where viewers show attachments;
- plain PDF/A-4 when it carries none.

The test suite validates every example against all four profiles at once:
PDF/UA-2, WTPDF accessibility, WTPDF reuse, and PDF/A-4 or 4f. The profile has
been checked independently: LittleCMS reads it as sRGB, and black, mid grey,
red and white pass through it unchanged.

## 4. Engineering

**"A C program that parses documents, fonts and images is a security
liability."**
*Answered, with work remaining* (bc2b3ad).
- tools/mutate.c and test/fuzz.sh (`make fuzz`) have run 42,000 mutated
  documents, PNG images and BibTeX databases under the sanitizers:
  - 26,000 through the PDF back end;
  - 16,000 through both the PDF and HTML back ends.
- The first runs found three bugs in five failing cases. All three are fixed,
  and each now has a regression test:
  - set cut node text in place, so a formula laid out twice read truncated data
    (test/golden/headings.tekx);
  - a footnote drew a fraction's rule as a glyph (test/golden/notes.tekx);
  - inflate stored a failed Huffman decode as a code length of 255 (test/unit.c;
    the test reproduces the out-of-bounds read when the fix is removed).
- The fuzzer then caught a fourth bug, before it was ever committed. The
  PDF/A work in this release held a pointer into the object table across an
  allocation that could move the table, a use-after-free in 10 of 1,500 cases.
  The code was restructured so that the pointer is taken after the
  allocation, and the 1,200-file document that drives the same path is now in
  the suite.
- The cases run since then are clean. In one run, five consecutive cases
  timed out while the machine was loaded by parallel builds, and the same
  seeds pass when rerun alone.
- Fonts are read through a bounds-checked reader whose error flag is sticky.
- **Open:** fuzzing doesn't run continuously (no OSS-Fuzz), and user-supplied
  fonts are not mutated.
- **Plan:** a libFuzzer harness per stage, and font mutation.

**"It allocates and never frees. Unusable as a library."**
*By design.* Each stage is a short-lived filter. The text formats between the
stages are the interface, and anything that reads lines can use them.
- **Open:** a long-running process, such as an editor's language server, would
  need an arena per document.
- **Plan:** arenas when the language server is built.

**"One maintainer, one implementation."**
*Open, and now easier to live with.* Packages (ce17ad1) let a group extend
tekx without changing it. A package can bring notation, environments, macros
and filters, which are programs in any language that rewrite the document
tree. It is pinned by content hash, so it can't drift. The risk is also
reduced by the discipline already in place:
- small files, libc only, no dependencies;
- a man page, and a machinery document that describes every format;
- reproducible builds;
- a test suite that runs every program under the sanitizers.

## 5. Adoption

**"Journals and arXiv want LaTeX source."**
*Open.*
- **Plan:** a LaTeX back end. It would read the same tree as the HTML back end
  and write LaTeX for submission.

**"My bibliography is a .bib file. I'm not retyping it."**
*Fixed* (8616514, 86ca947; test/golden/journal.bib, fuzzed).
`bibliography "refs.bib"` reads BibTeX as BibTeX does:
- `@string` macros, `#` concatenation and month names;
- accents and mathematics in titles;
- all three name forms;
- crossref inheritance, as DBLP writes it;
- biblatex field names, as Zotero writes them.

Only cited entries are printed, in AMS style. Citations come in three styles:
numeric (sorted and ranged), in citation order, or amsalpha ([Hoo71]).

**"There is no TikZ, no beamer, and no hyphenation for my language."**
*Open, and some of it will stay that way.* tekx 0.2 typesets journal
articles.
- More languages are on the roadmap below.
- A drawing language is a larger question. Figures from any tool, as PNG or
  JPEG, already work. A package filter can turn a block of its own into a
  figure, so a group can plug in its own drawing tool.

**"Copy text in tekx's own syntax is yet another standard."**
*Partly answered.*
- The copy text is close to UnicodeMath, Murray Sargent's linear format that
  Microsoft Office reads. `∑_(k=1)^n`, `a/b` and `√(x + 1)` are written the same
  way in both.
- **Open:** binomials, matrices and cases differ. tekx copies `(n choose k)`,
  where UnicodeMath writes `(n¦k)`.
- **Plan:** write and read UnicodeMath's forms, so that a formula copied from a
  tekx PDF pastes into Word's equation editor as a formula.

## 6. Strengths to amplify

These are the places where tekx can lead, not catch up.

0. **A journal paper from the first minute.**
   - `tekx new paper` writes a paper that builds, with front matter,
     citations and an appendix.
   - `tekx -w` rebuilds in tens of milliseconds on every save.
   - Running heads and referee line numbers are one line each.
   - Vim and VS Code highlight the source.

1. **Meaning is the core, and every output is derived from it.** One tree
   gives all of these, so they cannot disagree:
   - the typeset formula;
   - MathML;
   - speech;
   - copy text that reads back;
   - `explain`;
   - HTML.

   More outputs follow from the same tree: EPUB 3 for accessible textbooks
   (mostly the HTML back end already), LaTeX for submission, and UnicodeMath for
   Office.
2. **Accessible, archival and reproducible, in one file, checked.**
   - A tekx PDF validates as PDF/UA-2, WTPDF and PDF/A-4 together, and the
     same source gives the same bytes.
   - In LaTeX, declaring a standard enforces nothing (audit finding 2). In tekx,
     the test suite validates every example against all four profiles.
3. **Accessibility is becoming a legal duty.**
   - The European Accessibility Act has applied since 28 June 2025.
   - In the United States, the Department of Justice's 2024 rule under ADA
     Title II requires WCAG 2.1 AA of state and local governments, public
     universities included. An interim final rule of April 2026 moved the first
     compliance date to April 2027.
   - Remediating PDFs of mathematics by hand is slow and expensive.

   tekx produces conformant, MathML-tagged output with no remediation step.
4. **The pipeline is an ecosystem.** Each stage reads and writes plain text.
   - A new back end needs only the tree format, as the HTML back end shows.
   - Linters, statistics and converters can be grep and awk; the structure test
     is 55 lines of awk.
   - A LaTeX-to-tree importer would let existing documents move over.
5. **Twenty-millisecond builds change the editing loop.** None of this needs a
   caching layer:
   - live preview;
   - a language server, with diagnostics at line and column and `explain` on
     hover.
6. **One portable program.** C17 with only libc and libm should compile to
   WebAssembly. That has not been tried yet. It would put tekx in the browser:
   an editor that shows the page, the speech and the copy text as you type,
   with nothing to install.

## Roadmap, in order

| | item | answers |
|---|---|---|
| 1 | screen-reader test protocol and sessions with users | the accessibility claim itself |
| 2 | Acrobat and Preview in the paste matrix | copy and paste where it can't yet be measured |
| 3 | UnicodeMath copy text | interoperability with Office |
| 4 | character protrusion; breaking long displays; breaks inside inline groups | typography |
| 5 | language server and WebAssembly build | the editing loop, adoption |
| 6 | ClearSpeak-aligned, localised speech | speech quality |
| 7 | continuous fuzzing, font mutation | security |
| 8 | a registry of public packages beside the private ones | the ecosystem |
| 9 | versioned language specification | longevity |
| 10 | more hyphenation languages; multiple columns | breadth |
