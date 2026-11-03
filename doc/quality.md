# Quality

How tekx output compares with LaTeX, what is checked on every build, and what
tekx does not do yet. Every number here comes from the test suite or from a
command given on this page.

| | |
|---|---|
| [Formulas](#formulas) | glyph positions measured against LuaTeX |
| [Paragraphs and pages](#paragraphs-and-pages) | line breaking, hyphenation, protrusion |
| [Validation](#validation) | PDF/UA-2, WTPDF, PDF/A-4 |
| [Copy and paste](#copy-and-paste) | what a reader gets from a viewer |
| [Limits](#limits) | what is not done |

## Formulas

`test/luatex/formulas.txt` holds 104 formulas: scripts, fractions, radicals,
fences, big operators, accents, matrices, binomials, stacks and arrows. With
`LUALATEX` set, `test/luatex/run.sh` sets each one with LuaLaTeX, unicode-math
and the same STIX Two Math font, and compares the position of every glyph.

```bash
LUALATEX=/usr/local/texlive/2026/bin/x86_64-linux/lualatex sh test/luatex/run.sh
```

| | formulas within 2 em/1000 (0.02 pt at 10 pt) of LuaTeX |
|---|---|
| tekx 1.0.1 | 91 of 104 |

The 13 others differ for these reasons:

| formulas | difference |
|---|---|
| 7 | the reference sets operator names (`\sin`, `\lim`, `\log`) and text in Latin Modern; tekx uses STIX Two Text |
| 1 | `\xrightarrow`: amsmath draws a minus and an arrowhead, tekx the font's arrow assembly; the width is the same |
| 1 | `:=` is one symbol in tekx |
| 1 | `\left( b \right)` is spaced as a plain parenthesis; TeX adds thin spaces |
| 1 | `\int\limits`: tekx centres the limits on the sign, as the OpenType MATH specification says |
| 2 | tall assembled delimiters: overlaps differ by less than 30 em/1000 on a 6 em delimiter |

Without `LUALATEX`, the same script checks that the layout of all 104 formulas
is unchanged.

The rules that produce this agreement:
- every math glyph is followed by its italic correction;
- scripts attach to a glyph by TeX's character rules and use the font's
  staircase kerns (MathKernInfo);
- brackets in tekx notation grow only when their content is taller than they
  are; in LaTeX notation they follow `\left`, `\right` and `\big`;
- script-size delimiters, radicals and operators use the font's `ssty` forms,
  and tall bases take flattened accents (`flac`);
- spaces around relations and binary operators stretch and shrink as TeX's
  `\thickmuskip` and `\medmuskip`.

## Paragraphs and pages

| feature | tekx |
|---|---|
| line breaking | Knuth-Plass with TeX's passes: without hyphenation at tolerance 100, with it at 200, then with emergency stretch |
| hyphen costs | TeX's: 10000 for consecutive hyphenated lines, 5000 for a hyphen before the last line |
| compound words | break after their hyphen, and paste back whole |
| protrusion | microtype's default table: hyphens and punctuation hang into the margins |
| last lines | shrink when that saves a line, as TeX's do |
| long displays | broken before a relation or operator, aligned, number on the last line |
| widows and orphans | the first two and last two lines of a paragraph stay together |
| listings | break across pages, except after the first line and before the last |

A two-page example builds in 25 to 40 ms:

```bash
time ./tekx examples/tour.tekx
```

## Validation

Every example in `examples/` is built under AddressSanitizer and
UndefinedBehaviorSanitizer, rebuilt to check that the bytes are identical, and,
with `VERAPDF` set, validated by veraPDF against four profiles:

```bash
VERAPDF=/path/to/verapdf make test
```

| profile | |
|---|---|
| PDF/UA-2 | accessibility, ISO 14289-2 |
| WTPDF 1.0 accessibility | the PDF Association's Well-Tagged PDF |
| WTPDF 1.0 reuse | the same, for reuse of content |
| PDF/A-4, or PDF/A-4f when formulas carry MathML files | archiving, ISO 19005-4 |

Every formula is a `Formula` structure element with MathML children in the
MathML namespace, spoken alternative text, and its MathML as an associated
file.

## Copy and paste

`test/paste.sh` builds documents, extracts their text with the engines behind
common viewers, and compares it with what was written: poppler (Evince,
Okular) when `pdftotext` is installed, PDFium (Chrome, Edge) with `PDFIUM`
set, and pdf.js (Firefox) with `PDFJS` set.

| check | poppler |
|---|---|
| inline formulas that paste back as themselves | 53 of 53 |
| displayed formulas that paste back as themselves | 53 of 53 |
| sentences with hyphenated words, ligatures, dashes and a footnote | 6 of 6 |

A formula pastes as linear text in tekx notation, and tekx reads that text back
as the same formula: `test/roundtrip.sh` checks this for 53 formulas.

## Limits

| area | not done |
|---|---|
| fonts | no font expansion (microtype's `expansion`); fonts are OpenType CFF or TrueType only |
| layout | one column; no landscape pages |
| hyphenation | English patterns only; words with non-ASCII letters are not hyphenated |
| pages | footnotes do not split across pages; float placement is simpler than LaTeX's; no `\topskip` |
| spacing | no extra space after sentences (TeX's `\frenchspacing` is always on) |
| LaTeX | `tekx import` keeps unknown commands as text and reports them |
