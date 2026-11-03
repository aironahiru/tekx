# Machinery

tekx is one binary, but inside it is a Unix pipeline. This document describes
the design, the five stages (and the web back end that can stand in for the last
two) and the four text formats that join them. Everything here can be seen from
the shell.

## Principles, and where they come from

| principle | from | in tekx |
|---|---|---|
| filters joined by text streams | McIlroy, the Unix pipeline | five stages and a web back end; each reads text and writes text |
| say mathematics as you speak it | Kernighan & Cherry, *eqn* (1975) | the `eqn` stage and its `define` |
| a device-independent page description | Ossanna & Kernighan, *ditroff* | the page description (PDL) between `set` and `pdf` |
| UTF-8 everywhere | Thompson & Pike | every stream, every file, the copy text |
| fancy algorithms are slow when n is small; use simple data | Pike's rules | arrays and linear scans; no hash tables, no trees beyond the document |
| when in doubt, use brute force | Thompson | recompute instead of cache; read fonts in place |
| silence is golden | the rule of silence | `tekx paper.tekx` prints nothing when it succeeds |
| cc's conventions | the C compiler | `-E -P -M -S -o`, `file:line:col: error:`, `#line` markers |
| reproducible builds | `SOURCE_DATE_EPOCH` | same input, same bytes |

### C discipline

- **Language and libraries:** C17, using only libc and libm. The fonts and
  hyphenation patterns are compiled in by `tools/bin2c`.
- **Memory:** each stage is a short-lived filter. Memory is allocated and never
  freed; the operating system reclaims it at exit. Growable arrays (`VEC`) and
  byte buffers (`Buf`) are the only data structures.
- **Untrusted font data:** fonts are read through a bounds-checked reader whose
  error flag is sticky. A malformed font yields wrong metrics, never a crash.
- **Invariants:** `_Static_assert` ties every table to the enum it is indexed
  by (TeX's spacing table to the atom classes, the MATH constants to their
  count, the pipeline stages to their names). `INVARIANT()` stops with exit
  status 70 and the file and line when an internal assumption fails.
- **Warnings and sanitizers:** the build uses `-Wall -Wextra -Wpedantic -Wshadow
  -Wstrict-prototypes -Wmissing-prototypes -Werror`. The test suite runs
  everything under AddressSanitizer and UBSan, and requires the sanitized and
  optimized builds to produce identical bytes.
- **Atomic output:** the driver connects the stages with `open_memstream`. It
  writes the output only after every stage has succeeded, so a failed build
  never leaves a half-written PDF.

## The pipeline

```
paper.tekx
   │  tekx pp      macros, comments, do…end         src/pp.c
   ▼
core tekx (+ #line, #macro)
   │  tekx parse   blocks and inlines               src/parse.c
   ▼
document tree
   │  tekx eqn     mathematics                      src/eqn.c, eqnlayout.c, eqnsem.c
   ▼
document tree with laid-out formulas
   │  tekx set     lines, pages, numbers, tags      src/set.c, hyph.c
   ▼
page description (PDL)
   │  tekx pdf     fonts, structure, PDF/UA-2       src/pdf.c, font.c, cff.c, ttf.c, otl.c, mathtab.c, deflate.c, md5.c
   ▼
paper.pdf
```

`tekx -E`, `-P`, `-M` and `-S` stop after `pp`, `parse`, `eqn` and `set` and
write to standard output. `tekx STAGE [file]` runs one stage as a filter. The
test suite checks that

```bash
tekx pp paper.tekx | tekx parse | tekx eqn | tekx set | tekx pdf >paper.pdf
```

produces the same bytes as `tekx paper.tekx`.

After an error the later stages still run, so that one run reports as much as
it can, but no output is written and the device stage is skipped.

## Format 1: core tekx

`pp` writes the language without macros. It strips comments, turns `do … end`
into indentation (`end` becomes a line `#end` that closes the block), drops
definitions it has consumed, and expands block and text macros. Lines that moved
are preceded by a marker in the C preprocessor's form:

```
#macro cite
#line 12 "paper.tekx"
```

`#macro NAME` announces a text macro, so that `parse` can suggest it for a
misspelled `{name}`. `#line N "FILE"` gives the origin of the next line. A
document that uses packages also gets `#document "FILE"` (the document itself,
however many package files precede its first line), `#package NAME VERSION
DIR`, `#block NAME PACKAGE` for a package's custom blocks, and `#filter PROGRAM
DIR` for its filters. `parse` writes a custom block as a `custom` node (NAME
PACKAGE, children `args` and `verbatim`), and pipes the finished tree through
the filters in order. A `custom` node that survives the filters is an error in
the back ends. See [packages.md](packages.md).
Mathematical definitions (`define function`, `define operator`, notation macros)
and `env NAME KIND TITLE` lines (from `define … like …`) pass through to later
stages.

## Format 2: the document tree

`parse` writes one node per line, nested by two-space indentation:

```
kind[@line[:col]] [arguments] [text]
```

```
document
  meta@1 title A Tour of tekx
  section@19 1 1 intro
    heading
      text Introduction
    para@21 0
      text Let us begin with the golden ratio, 
      math@21:38 phi = (1 + sqrt 5)/2
      text . It satisfies
    display@23
      line@23:5 golden phi^2 = phi + 1
```

| node | arguments and text |
|---|---|
| `document` | the root; its text is the source file's name, so that later stages reading a pipe still report errors against it and find files beside it |
| `meta` | KEY VALUE; an `author` may have children `affiliation`, `email`, `orcid`, `url` |
| `bibfile` | PATH of a BibTeX database (read by `parse`, which writes its cited entries into `refs`) |
| `appendix` | none: the top-level sections after it are appendices |
| `env` | NAME KIND TITLE |
| `define` | a mathematical definition, verbatim |
| `section` | LEVEL NUMBERED LABEL; children `heading`, then blocks |
| `para` | CONTINUES (1 when it goes on after a display); inline children |
| `display` | children `line`: LABEL SOURCE, with `cont` children (rows) |
| `theorem` | KIND LABEL; optional `heading`, then blocks |
| `proof`, `quote`, `abstract` | optional `heading`, then blocks |
| `list` | ORDERED START; children `item` holding blocks |
| `table` | optional `align` (l c r …), then `row` HEADER with `cell` children |
| `code` | LANGUAGE; children `verbatim` LINE |
| `let` | MATH; optional child `prose` |
| `figure` | PATH (%-escaped) WIDTH LABEL; child `alt`, then caption blocks |
| `refs` | children `entry` KEY with inline children; BibTeX entries arrive here already formatted |
| `contents`, `newpage` | none |
| inline | `text`, `emph`, `strong`, `code`, `math` SOURCE, `link` URL, `url`, `ref` LABEL, `note` (a footnote, with inline children), `cite` with children `item` KEY (the note as inline children) |

`parse` decides what each `@key` is: a `ref` to a `#label`, or a `cite` of a
reference entry. Entries are labelled in their own namespace (`cite:KEY`), so
keys and labels never collide. The numbering pass (`number.c`), shared by both
back ends, gives every numbered node its number, and so the PDF and the HTML
always agree.

Positions (`@line:col`) are carried so that later stages can report errors in
the user's own coordinates.

## Format 3: laid-out formulas

`eqn` keeps the tree as it is and adds children to every `math`, display
`line` and `let` node. Lengths are thousandths of an em at the formula's base
size; y grows downwards from the baseline.

```
math@21:38 phi = (1 + sqrt 5)/2
  box 2635.244 829.7 40 -
  brk 1198 1420 500
  n 0 - math
  n 1 0 mrow
  n 2 1 mi
  g math 1000 0 0 3392 527 2 φ
  i text 700 569 -360 ^
  r 120 -263 1100 40
  mathml <math xmlns="http://www.w3.org/1998/Math/MathML" …>…</math>
  speech phi equals the fraction with numerator 1 plus the square root of 5 …
  linear φ = (1 + √5)/2
```

| line | meaning |
|---|---|
| `box W H D A` | width, height, depth; A is the alignment point (before the first relation, or at `&`) or `-` |
| `brk XA XB P` | inline formulas only: a line may end at XA and resume at XB, with penalty P (500 after relations, 700 after binary operators) |
| `n ID PARENT TAG [ACTUAL]` | a MathML element for the structure tree; ACTUAL (%-escaped) replaces the glyphs' text for assistive technology |
| `g FONT SCALE X Y GID ADV NODE TEXT` | a glyph: logical font (`math` or `text`), size in thousandths, position, glyph id, advance, owning element, and its Unicode text (%-escaped) |
| `i FONT SCALE X Y TEXT` | an invisible glyph that gives text extractors that ignore ActualText (pdf.js) a linear reading: `^`, `/`, parentheses |
| `r X Y W H` | a rule: fraction bars, radical overbars, overlines |
| `mathml`, `speech`, `linear` | the formula's three meanings |
| `mleft`, `mright` | display lines only: the MathML on each side of the alignment point |

Glyphs come in reading order, so the content stream reads in the same order as
the copy text.

### Inside eqn

- **Tokens:** numbers, words, symbols (longest match: `<=>`, `|->`, `...`),
  quoted text, Unicode super- and subscripts, and combining accents. Each token
  records whether a blank preceded it.
- **Parsing:** top-down operator precedence (Pratt, 1973). Juxtaposition is an
  operator whose binding power depends on that blank: 60 without one, 45 with
  one. `/` likewise binds at 50, or at 42 with blanks on both sides. Thus
  `1/2 x` is (1/2)x and `h/2pi` is h/(2π). Words such as `from`, `to`, `of`,
  `as` and the differentials `dx` stop an expression only inside the
  construction that expects them, through a set of stop flags.
- **Layout:** TeX's Appendix G as restated by the OpenType MATH specification
  and MathML Core:
  - atom classes and the 8×8 spacing table, with binary operators turned into
    ordinary atoms at the start of a list;
  - script styles with `ssty` alternates;
  - fractions, stacks, scripts and limits placed by the font's constants;
  - radicals, delimiters and big operators grown through size variants and glyph
    assemblies;
  - accents placed by top-accent attachment.
- **Copy text:** a linear Unicode form designed to be read back. The suite checks
  that `linear(parse(linear(parse(s))))` equals `linear(parse(s))`, with equal
  MathML, for every formula in `test/formulas.txt`.

## Format 4: the page description

`set` writes a device-independent page description, one command per line, in
points with the origin at the top left. `pdf` turns it into PDF.

```
x tekx-pdl 1
m title A Tour of tekx
m language en
f 0 builtin:STIXTwoText-Regular.otf
f 1 builtin:STIXTwoMath-Regular.otf
e 0 - pdf2 Document
e 1 0 tekx section
e 2 1 tekx section-heading
e 3 1 pdf2 Formula
e 4 3 mathml math
a 3 alt phi equals …
a 3 mathml <math …>…</math>
A 9 List ListNumbering n Disc
o 1 0 207.5 1 1 Introduction
p 595.276 841.89
b 2
t 0 14 117.6 230.1 1927:7.0:1 1:14:%20 270:8.0:I …
E
s φ = (1 + √5)/2
b 6
t 1 11 270 300 3392:5.8:φ
E
E
B pagination
t 0 10 295 800 1927:5:1
E
l 190 120 45 13 7 d 1 0 90 Section 1
```

| command | meaning |
|---|---|
| `x tekx-pdl 1` | format and version |
| `m KEY TEXT` | metadata: `title`, `author` (repeatable), `language`, `subject`, `keywords`, `date` (Unix time) |
| `f ID SOURCE` | a font, `builtin:NAME` or a file, declared in order |
| `e ID PARENT NS TAG` | a structure element; NS is `pdf2`, `pdf` (1.7), `mathml` or `tekx`; declared in order |
| `a ID KEY TEXT` | a property: `alt`, `actual`, `lang`, `title`, `ident`, `mathml`, `ref` |
| `A ID OWNER KEY TYPE VALUE` | an attribute; TYPE `n` name, `i` integer, `r` real, `s` string |
| `o LEVEL PAGE Y ELEM TITLE` | a bookmark to a structure destination |
| `p W H` | a new page |
| `t F SIZE X Y G…` | a glyph run; each G is `gid:advance:text`, text %-escaped |
| `T …` | the same, invisible (text render mode 3) |
| `r X Y W H` | a filled rectangle |
| `I X Y W H PATH` | an image (PNG or JPEG) filling the rectangle |
| `b ELEM` / `B KIND` / `s TEXT` / `E` | begin marked content for an element, an artifact (`pagination`, `layout`), an ActualText span; end |
| `l X Y W H ELEM u URL TEXT` | a link to a URL |
| `l X Y W H ELEM d TARGET PAGE Y TEXT` | a link to a structure element |

Elements in the `tekx` namespace (`section`, `theorem`, `equation`, `qed` …)
are role-mapped to standard types through the namespace's RoleMapNS, and
through a fallback `/RoleMap` for readers that ignore namespaces.

### Inside set

- **Words:** shaped with GSUB `liga` and GPOS kerning, then hyphenated by Liang's
  algorithm with the US English patterns and exceptions.
- **Line breaking:** Knuth and Plass's total-fit algorithm, with fitness classes,
  flagged demerits for consecutive hyphens, and tolerances 200, then 2000, then
  unlimited, then first fit.
- **Space:** every word space is a real U+0020 glyph, stretched by its advance
  in the glyph run. A line that ends at a space keeps that space.
- **Hyphenated words:** a word broken across lines is wrapped in an ActualText
  span holding the whole word.
- **Formulas:** each formula is wrapped in an ActualText span holding its linear
  text. Inside that span, every glyph is marked content of its MathML element.
- **Pages:** filled top to bottom. Headings, display introductions, the first
  and last two lines of paragraphs, and table rows keep with their neighbours.
- **Numbering:** a numbering pass precedes layout, so references may point
  forward. Destinations and page numbers are resolved after pagination.

## The device back end

`pdf` writes PDF 2.0:

- **Fonts:** subset as CID-keyed CFF (name-keyed CFF is converted, with glyph
  ids preserved) or TrueType, each with a ToUnicode map.
- **Structure:** the tree in the PDF 2.0, PDF 1.7, MathML and tekx namespaces,
  with a ParentTree, structure destinations, `/Ref` from contents entries,
  and an IDTree.
- **Formulas:** each Formula carries its MathML as an associated file; the
  MathML's annotation holds the tekx source, so the source is not attached twice.
- **Metadata:** XMP declaring PDF/UA-2, WTPDF accessibility and reuse, and
  PDF/A-4. A document that carries files is PDF/A-4f; those files are also
  listed in the catalog's EmbeddedFiles name tree, so viewers show each
  formula's MathML as an attachment.
- **Colour:** an sRGB output intent. The ICC profile (version 2.1, 2.5 KB) is
  written out by `srgb_profile()` from the sRGB primaries and tone curve,
  not shipped as a binary blob. It is the colour space every viewer already
  assumes for DeviceRGB, and declaring it is what PDF/A asks.
- **Safety net:** any content outside marked content is wrapped as an artifact.

Every object that is not itself a stream (structure elements, dictionaries)
is packed into compressed object streams, and indexed by a compressed
cross-reference stream. A 59-page, 1,080-formula document comes to 1.7 MB.
Streams are compressed by tekx's own deflate, and output is reproducible under
`SOURCE_DATE_EPOCH`.

## The web back end

`tekx html` reads the tree after `eqn`, the same input as `set`, and writes
HTML5:
- sections, theorems, proofs, lists, tables, figures and references become the
  matching semantic elements;
- every formula becomes the MathML that `eqn` produced;
- a display whose lines share an alignment point becomes one MathML table
  built from the `mleft` and `mright` halves `eqn` emits;
- footnotes become linked endnotes (`doc-noteref`, `doc-endnote`);
- references link to their targets with the same text as in the PDF, and with
  the same spoken names ("Equation 1").

Numbers come from the same numbering pass, `number.c`, that `set` uses, so the
PDF and the page cannot disagree. The driver chooses this back end when the
output file ends in `.html`.

## LaTeX, both ways

`tekx tex` (texout.c) is a third back end. It reads the same tree as `html`
and writes amsart LaTeX for journals and arXiv:
- each formula is parsed again from its source and written by `eqn_latex()`
  (eqnsem.c), which serializes the meaning, not the tekx spelling;
- fractions keep tekx's choice, a/b on the line outside display style;
- `\left` and `\right` appear only around contents taller than a line;
- display lines become `equation`, or `align` split by `eqn_latex_split()` at
  the same alignment point the PDF uses;
- the definitions are replayed with `eqn_reset_declarations()` first, so that
  the stage runs after `eqn` in one process.

The driver chooses this back end for an output name ending in `.tex`.

`tekx import` (latex.c) goes the other way. It is not a stage, because its
input is not tekx, and it works in steps:
1. comments are blanked, keeping lines, so that warnings point at the right
   place;
2. `\newcommand`, `\def`, `\DeclareMathOperator`, `\newtheorem` and
   `\numberwithin` are read and blanked;
3. macros are expanded, at most twenty rounds, and a macro that names itself
   is left alone;
4. the body is converted block by block.

Labels and references travel as placeholders until the end, when every target
is known. A reference to something tekx cannot number (a table) becomes `??`
with a warning, and a label that clashes with a citation key is renamed. Every
formula is parsed with eqn as it is written, with diagnostics collected in
`diag_sink` (util.c), so that one that tekx would not read is reported in the
import's own words.

## Validation

The PDFs are checked with veraPDF against the PDF/UA-2, WTPDF accessibility,
WTPDF reuse and PDF/A-4 (or 4f) profiles. Passing a validator shows that a tag
tree is well formed, not that it means the right thing, so the suite also pins
the tree itself: `test/structure.awk` turns the page description's structure
into an outline, and golden files hold it for every test document and for
`examples/tour.tekx`. In that outline you can see that an equation's number
is a label inside its Formula, that a figure's caption sits beside the figure
in one Aside, and that every link has a spoken name and the right target:

```
pdf2:Link link="Equation 1" to=Formula
pdf2:Link link="Footnote 1" to=FENote
```

With a `verapdf` at hand, run it from the suite:

```bash
VERAPDF=/path/to/verapdf make test
```

### Copy and paste

`test/paste.sh` (step 9 of the suite) checks what copy and paste gives in the
viewers people use. It extracts the text of test PDFs with each engine at hand:

| engine | viewers | how it is enabled |
|---|---|---|
| poppler | Evince, Okular, `pdftotext`, most tools | `pdftotext` on the PATH |
| pdf.js | Firefox | `PDFJS=/path/to/pdfjs-dist` |
| PDFium | Chrome, Edge | `PDFIUM=/path/to/dir` holding `pypdfium2` |

For each engine it checks three things:
- every formula in `test/formulas.txt`, inline and displayed, pastes back with
  the same MathML as the original;
- `test/paste/prose.tekx` pastes as written: words whole across hyphenated line
  breaks, ligatures as letters, real spaces;
- the text of `examples/tour.tekx` and `test/golden/journal.tekx` matches
  `test/paste/*.ENGINE.txt`.

Known engine limits are listed in `test/paste/limits` with their reasons. They
are reported rather than failed, and the script says when one starts passing.

The three engines read nested marked content differently, so the content
stream is written for all of them at once:
- **Poppler** takes ActualText only from a `Span`, and lets it replace
  everything inside, nested marks included.
- **PDFium** takes ActualText from any tag, but only from the innermost
  dictionary. Around the per-glyph MathML marks of a formula, that dictionary is
  the MCID's, so before this design PDFium copied formulas and hyphenated words
  as nothing at all. It also skips a text object whose innermost dictionary is
  the same object as the previous one's.
- **pdf.js** ignores ActualText.

So each ActualText span is written twice:

```
/Span <</ActualText (φ = (1 + √5)/2)>> BDC      poppler and Acrobat read this
  /mi <</MCID 2>> BDC
    /Copy /AT0 BDC  BT … ET  EMC               PDFium reads /AT0 once, skips the rest
  EMC
  …
EMC
```

`/AT0` is a named property in the page's `/Properties` holding the same text,
and every text object of the span shares it. `/Copy` is not a `Span`, so poppler
ignores it.

pdf.js reads the glyphs, plus the invisible helpers that layout places for it:
- the `(`, `)` and `/` of a fraction;
- `^` and `_` before scripts, and a blank after an unbracketed script;
- `choose` in a binomial, `quad` in a wide space.

The helpers are drawn in render mode 3 at 2% width, so none overlaps the next
word or widens a formula, in any engine.

The result:

| | poppler | PDFium | pdf.js |
|---|---|---|---|
| formulas, inline (48) | 48 | 48 | 45 |
| formulas, displayed (48) | 48 | 48 | 45 |
| prose (hyphenated words whole) | yes | yes | split: `uncharacter- istically` |

The pdf.js exceptions are drawn glyphs that no helper can undo:
- the brace of `cases`;
- the radical of `root 3 of x`;
- one product whose copy text depends on a blank that the layout does not show.

Before this design, PDFium copied 0 of 48 formulas, and poppler lost the
blank after a radical.
