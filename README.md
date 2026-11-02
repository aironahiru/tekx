# tekx

**A math-first typesetter.** You write mathematics the way you say it:

```
theorem "Gauss" #gauss
  For every natural number $n$,

      sum k for k from 1 to n = n(n + 1)/2
```

`tekx paper.tekx` writes `paper.pdf`. It does so silently, in one self-contained
binary of C17, libc and libm. There are no packages to install, no auxiliary
files, and no second run.

![A page of examples/showcase.pdf](examples/showcase.png)

## Three things no other system does together

**1. Math that means something.** One formula source gives you four outputs:

| from `limit of sin x / x as x approaches 0 = 1` | |
|---|---|
| typeset | the formula, laid out with the font's OpenType MATH table |
| MathML | `<munder><mo>lim</mo><mrow><mi>𝑥</mi><mo>→</mo><mn>0</mn></mrow></munder>…`, attached to the PDF |
| speech | *the limit as x approaches 0 of the fraction with numerator sine x and denominator x end fraction equals 1* |
| copy text | `lim_(x→0) (sin x)/x = 1` |

The copy text is not a lossy rendering. **tekx reads it back as the same formula.**
The test suite checks this property on every formula in
[`test/formulas.txt`](test/formulas.txt).

**2. One compiler, start to finish.** The macro language, the parser, math
layout, Knuth–Plass line breaking with Liang hyphenation, pagination,
cross-references, font subsetting (CFF and TrueType), compression and the PDF
writer are all in this repository and all in the binary. The STIX Two and
Libertinus fonts are compiled in.

**3. Accessible and copy-perfect by construction.** Every PDF that tekx writes is
tagged PDF 2.0 and validates with veraPDF as **PDF/UA-2**, **WTPDF**
(accessibility and reuse) and **PDF/A-4** for archiving (PDF/A-4f when it carries
files). Each formula is tagged with MathML structure, has spoken
alternative text, and carries its MathML and tekx source as associated files.
Copy and paste gives you real text:
- spaces are real space characters;
- hyphenated words come back whole;
- formulas come back as linear text you can paste into a tekx file;
- page numbers and decoration are marked as artifacts and stay out of the way.

The test suite checks the paste in the engines behind the viewers people use
(`test/paste.sh`):

| pastes back as itself | poppler (Evince, Okular) | PDFium (Chrome, Edge) | pdf.js (Firefox) |
|---|---|---|---|
| 50 test formulas, inline | 50 | 50 | 47 |
| 50 test formulas, displayed | 50 | 50 | 47 |
| hyphenated words | whole | whole | split (pdf.js ignores ActualText) |

## The language

The design takes after Ruby: it reads like prose, and it has a little structure
where the meaning needs it. Indentation, or `do … end`, makes blocks. Any line
indented further than the text around it is display mathematics.

```
title "On Primes"
author "Ada Lovelace"
date today

define function zeta
define block note(title = "Note")
  quote
    **{title}.** {body}

abstract
  We revisit Euler's product.

section Euler's product #euler

For $s > 1$ the zeta function factors over the primes:

    zeta(s) = sum 1/n^s for n from 1 to infinity
            = prod_(p prime) 1/(1 - p^(-s))       #euler-product

note "History"
  Euler found @euler-product in 1737.

The absolute value is a function defined by cases:

    abs(x) = cases
      x    if x >= 0
      -x   otherwise
```

- **Mathematics:**
  - **Operators and limits:** `sum … for k from 1 to n`, `integral of f(x) dx from a to b`, `limit of … as x approaches 0`.
  - **Roots and functions:** `sqrt`, `square root of`, `abs`, `norm`, `floor`, `binom(n, k)`, `n choose k`.
  - **Arrays:** matrices as `[1, 2; 3, 4]` or as indented rows, and `cases`.
  - **Styles and accents:** `hat`, `vec`, `bb`, `cal`, `frak`, `bold`.
  - **Letters and symbols:** Greek by name, `RR` `NN` `ZZ`, every Unicode symbol, `x1` for x₁.
  - **Spacing groups:** `1/2 x` means ½·x and `h/2pi` means h/(2π).
- **Structure:** `section` or `#`, theorem-like blocks with numbering, `proof` with ∎, lists, tables, quotes, code, `abstract`, `contents`, `references`.
- **Figures:** PNG or JPEG with a numbered caption. Alt text is required, so a figure cannot be inaccessible.
- **Cross-references:** mark a target with `#label` and refer to it with `@label`. The reference reads "Section 2", "Theorem 3", "(4)" or "[1]" and links to its target.
- **Macros:**
  - text macros, `{name}`;
  - block macros, which take parameters, defaults and a `{body}`;
  - math notation, `define norm2(v) as ||v||_2 read as "the two-norm of {v}"`;
  - new theorem-like environments, `define axiom like theorem`.

The full reference is [doc/language.md](doc/language.md).

## Writing a journal paper

```bash
tekx new paper
```

```bash
tekx -w paper.tekx
```

`tekx new` writes a paper to start from: front matter, a theorem and its proof,
citations, a display and an appendix, with a `.bib` file beside it. `tekx -w`
rebuilds on every save, including when the bibliography, an image or a package
changes. A build takes tens of milliseconds.

```
title "Gaps Between Sums of Two Squares"
author "Ada Lovelace", affiliation: "University of London", email: "ada@example.org"
msc "11N36 (primary); 11E25 (secondary)"
numbering by section
bibliography "refs.bib"

abstract
  We improve the exponent $1/2$ of [@hooley, Thm. 2; @bambah].

section Introduction

theorem "Main" #main
  Every interval $[x, x + x^(1/4 + epsilon)]$ contains a sum of two squares.

By Theorem @main, and as Landau showed [@landau], ...

appendix

section Computations
```

The things a journal expects:
- **Front matter:** affiliations, emails and ORCID under each author, the 2020
  Mathematics Subject Classification and key words after the abstract,
  running heads (`running "Short Title"`), and referee line numbers
  (`linenumbers on`).
- **Numbering:** sections, theorems and equations numbered within sections
  (Theorem 2.3, (2.3)), appendices (Lemma A.1), and unnumbered `theorem*`.
- **References:**
  - your existing `.bib` file, read as BibTeX reads it (crossref, biblatex
    fields, accents, mathematics in titles);
  - only the cited entries printed, in the style of the AMS;
  - numeric labels sorted and ranged ([1–3, 5]), in citation order, or
    alphabetic as amsalpha gives them ([Hoo71]).
- **LaTeX habits understood:** `Theorem @main` reads "Theorem 1.1", `(@eq)`
  reads "(2.1)", and LaTeX notation works inside any formula.
- **Typography:** inline fractions are set on the line (x^{1/4}, (1 + √5)/2), as
  print sets them, and stacked in displays.

## Packages, including private ones

```
use house-notation 1.2
```

A package carries what a group's documents share: notation, environments,
macros, and programs (filters) that extend tekx with blocks of their own.

`tekx pkg add` fetches packages from a directory, a git repository or a
registry. A registry is any directory or git repository of packages, so an
in-house tool can stay in-house. Each package is pinned in `tekx.lock` by
version, source and SHA-256 content hash. Every build checks the hash, and
`tekx pkg install` reproduces exactly the locked packages on another machine.
See [doc/packages.md](doc/packages.md).

## Compatibility

A document that builds with tekx 0.2 keeps building, with the same structure,
numbering and meaning. A `tekx 0.2` line says which version a document was
written for. The test suite's golden files are the contract, and every change
to output is listed in [CHANGELOG.md](CHANGELOG.md). See
[doc/compatibility.md](doc/compatibility.md).

<!-- LATEX-IMPORT-EXPORT -->

## One source, two accessible outputs

`tekx paper.tekx` writes the PDF; `tekx -o paper.html paper.tekx` writes a web
page from the same source. The page uses semantic HTML, native MathML for every
formula (which browsers render and screen readers can navigate), and the same
numbers, cross-references and footnotes as the PDF. The HTML back end is one
more filter reading the same tree as the typesetter:
`tekx pp | tekx parse | tekx eqn | tekx html`.

## The hidden machinery

Inside, tekx is a Unix pipeline of small filters connected by text streams. Each stage can be run alone and its output read by eye:

```
tekx pp    <paper.tekx |   # macros, comments, do/end            (cf. cpp, m4)
tekx parse |               # document structure as an indented tree
tekx eqn   |               # mathematics: layout, MathML, speech  (cf. eqn)
tekx set   |               # lines and pages: a page description  (cf. troff)
tekx pdf   >paper.pdf      # device back end: tagged PDF/UA-2     (cf. ditroff)
```

`tekx -E`, `-P`, `-M` and `-S` stop after each stage, the way `cc -E` and `cc -S`
do. Diagnostics follow the compiler convention, `file:line:col: error: …`, and
come with suggestions (*did you mean @start?*). The formats and the design are
described in [doc/machinery.md](doc/machinery.md).

## Building

```bash
make
```

```bash
./tekx examples/tour.tekx
```

```bash
make test
```

```bash
make install PREFIX=$HOME/.local
```

You need a C17 compiler and `make`; nothing else. `make test` builds an
AddressSanitizer/UBSan binary and runs the suite on it:
- unit tests;
- the copy-text round trip;
- golden trees and MathML;
- expected diagnostics;
- reproducible, identical debug and optimized builds;
- the shell pipeline against the driver.

Set `VERAPDF=/path/to/verapdf` to add PDF/UA-2, WTPDF accessibility, WTPDF
reuse and PDF/A-4 validation of every example.

Output is reproducible: set `SOURCE_DATE_EPOCH` and the same input gives the same
bytes.

## Status

tekx 0.2 typesets journal articles: front matter, abstracts, sections and
appendices, theorems and proofs, lists, tables, figures, code, displayed and
inline mathematics, cross-references, footnotes, contents, and bibliographies
from BibTeX. It does not yet do:
- multiple columns;
- line breaks inside long displayed equations;
- microtypography (character protrusion);
- hyphenation for languages other than English.

The built-in fonts are STIX Two (text and math) and Libertinus Mono. Replace any
of them with `font text|italic|bold|bolditalic|math|mono FILE`.

## Repository

| path | contents |
|---|---|
| `src/` | the compiler: one file per stage, plus the font engine (`font.c`, `cff.c`, `ttf.c`, `otl.c`, `mathtab.c`), images (`img.c`), `deflate.c`, `md5.c` and `hyph.c` |
| `doc/` | the language reference, the machinery, [packages](doc/packages.md), [compatibility](doc/compatibility.md), the manual page `tekx.1`, and [`review.md`](doc/review.md): the strongest criticisms of tekx, what was done about each, and the roadmap |
| `editors/` | syntax highlighting for Vim and VS Code |
| `examples/` | example documents with their PDFs |
| `test/` | the test suite |
| `assets/` | fonts (OFL) and hyphenation patterns, compiled into the binary |
| `research/` | the audit of the LaTeX tagged-PDF stack that motivated tekx |

## License

tekx is licensed under the Apache License 2.0; see [LICENSE](LICENSE). The bundled
fonts are under the SIL Open Font License, and the hyphenation patterns are under
their own permissive license; see [assets/fonts/NOTICE](assets/fonts/NOTICE) and
[assets/hyphenation/NOTICE](assets/hyphenation/NOTICE).
