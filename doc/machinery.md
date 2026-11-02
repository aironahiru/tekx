# The hidden machinery

> Write programs that do one thing and do it well. Write programs to work
> together. Write programs to handle text streams, because that is a
> universal interface. — Doug McIlroy

tekx is one binary, but inside it is a Unix pipeline. This document describes
the design, the five stages and the four text formats that join them. Everything
here can be seen from the shell.

## Principles, and where they come from

| principle | from | in tekx |
|---|---|---|
| filters joined by text streams | McIlroy, the Unix pipeline | five stages; each reads text and writes text |
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
misspelled `{name}`. `#line N "FILE"` gives the origin of the next line.
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
| `meta` | KEY VALUE |
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
| `refs` | children `entry` KEY with inline children |
| `contents`, `newpage` | none |
| inline | `text`, `emph`, `strong`, `code`, `math` SOURCE, `link` URL, `url`, `ref` LABEL, `note` (a footnote, with inline children) |

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
| `a ID KEY TEXT` | a property: `alt`, `actual`, `lang`, `title`, `ident`, `mathml`, `source`, `ref` |
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
- **Formulas:** MathML and source attached to each Formula as associated files.
- **Metadata:** XMP declaring PDF/UA-2 and WTPDF accessibility and reuse.
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

## Validation

The PDFs are checked with veraPDF against the PDF/UA-2, WTPDF accessibility and
WTPDF reuse profiles. Copy and paste is checked with poppler (`pdftotext`, which
honours ActualText) and pdf.js (which does not). With a `verapdf` at hand, run
it from the suite:

```bash
VERAPDF=/path/to/verapdf make test
```
