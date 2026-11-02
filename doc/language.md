# The tekx language

A tekx file is plain UTF-8 text. Prose is written as prose; structure comes from
a keyword at the start of a line and from indentation; mathematics is written
the way it is spoken. This document describes the whole language.

- [Documents](#documents)
- [Blocks](#blocks)
- [Inline text](#inline-text)
- [Mathematics](#mathematics)
- [Cross-references](#cross-references)
- [Citations and bibliography](#citations-and-bibliography)
- [Macros and definitions](#macros-and-definitions)
- [Errors](#errors)

## Documents

```
title "A Title"
subtitle "An optional subtitle"
author "Ada Lovelace and Charles Babbage"
date today
```

Metadata lines may appear anywhere at the top level. They set the title block
and the document's metadata (PDF title, authors, language, XMP).

| line | effect |
|---|---|
| `tekx 0.2` | the tekx version the document is written for (see [compatibility.md](compatibility.md)) |
| `title TEXT` | the document title; also the PDF title shown in the window bar |
| `subtitle TEXT` | under the title; also the PDF subject |
| `author TEXT` | one or more authors, separated by ` and ` |
| `author "Name", affiliation: "…", email: "…"` | one author with details (also `orcid:` and `url:`; `affiliation:` may repeat) |
| `date TEXT` | a date as written, or `today` (honours `SOURCE_DATE_EPOCH`) |
| `language TAG` | a BCP 47 language tag, default `en` (also `lang`) |
| `keywords TEXT` | key words: printed after the abstract, and the PDF's keywords |
| `msc TEXT` | the 2020 Mathematics Subject Classification, printed after the abstract |
| `numbering by section` | theorems and equations numbered within sections: Theorem 2.3, (2.3); the default is `numbering continuous` |
| `bibliography "refs.bib"` | read a BibTeX database (several may be listed, separated by commas) |
| `paper a4` | `a4` (default), `a5` or `letter` |
| `size 11` | the body type size in points, 6 to 24 (default 11) |
| `font ROLE FILE` | replace a built-in font; ROLE is `text`, `italic`, `bold`, `bolditalic`, `math` or `mono` |

Quotes around values are optional, and protect commas: `"London, UK"` is one
value. The Ruby-flavoured one-liner `document title: "A Title", author: "Ada
Lovelace"` sets several fields at once.

A journal paper's front matter:

```
title "Gaps Between Sums of Two Squares"
author "Ada Lovelace", affiliation: "University of London", email: "ada@example.org"
author "Charles Babbage", affiliation: "Cambridge", orcid: "0000-0002-1825-0097"
msc "11N36 (primary); 11E25 (secondary)"
keywords "sums of two squares, short intervals"
numbering by section
bibliography "refs.bib"
```

Each author's affiliations sit under the name, and the email address, ORCID
and web page are links. A line that looks like a setting but isn't one
(`affilation "London"`) is an error with a suggestion, not a paragraph.

A `math` font must have an OpenType MATH table. Fonts must permit embedding.

## Blocks

Blocks are separated by blank lines. A block that has a body takes the lines
indented below it; `do … end` is an equivalent spelling that needs no
indentation:

```
theorem "Pythagoras"
  The body, indented.

theorem "Pythagoras" do
The body, delimited.
end
```

### Paragraphs

Consecutive lines form a paragraph; a line break is a space. The first line of a
paragraph is indented, except after headings, displays and the like.

A paragraph that continues a sentence after a display formula is not indented:
in

```
The identity

    e^(i pi) + 1 = 0

where e is Euler's number, ...
```

the text after the display goes on with the same paragraph, because it starts
with a lowercase letter. A paragraph that starts with a capital letter after a
display is a new paragraph.

### Headings

```
section Introduction #intro
subsection Notation
subsubsection Conventions
section* Acknowledgements        // unnumbered
# Introduction                   // the same as section
## Notation
### Conventions
```

Sections are numbered 1, 1.1, 1.1.1 and nest: a section contains everything
up to the next heading of the same or a higher level. A trailing `#label`
makes the section a target for `@label`.

A line holding only `appendix` turns the sections after it into appendices:
"Appendix A", with subsections A.1 and, under `numbering by section`, Lemma A.1
and equation (A.1).

### Theorems and proofs

```
theorem "Fermat" #fermat
  There are no positive integers with $a^n + b^n = c^n$ for $n > 2$.

proof
  The margin is too small.
```

The theorem-like blocks are `theorem`, `lemma`, `proposition`, `corollary`,
`conjecture`, `claim`, `axiom`, `hypothesis`, `fact` (italic body), `definition`,
`example`, `exercise`, `problem`, `solution`, `notation`, `assumption` (upright
body), and `remark`, `note`, `observation` (italic head, upright body). They
share one counter and take an optional quoted title and `#label`.

`proof` sets "Proof." and ends with ∎ at the right margin; `proof "Proof of
@fermat"` replaces the head. Define new kinds with
`define axiom2 like theorem titled "Postulate"`.

### Lists

```
- an item
- another, with
  a continuation line
  1. nested and numbered
  2. like this
```

Bullets are `-`, `+` or `*`; numbers are `1.` or `1)` (the first number is the
starting value). An item's body may hold any blocks, indented to its text.

### Tables

```
| Symbol | Meaning |
|:------:|---------|
| $NN$   | natural numbers |
| $RR$   | real numbers |
```

Rows above the separator are header cells (tagged `TH` with column scope).
Colons in the separator align columns: `:--` left, `:-:` centre, `--:` right.
Cells hold inline text, including mathematics.

### Displays

Any line indented further than the paragraph around it is display mathematics:

```
The Gaussian integral

    integral of e^(-x^2) dx from -infinity to infinity = sqrt pi   #gauss

underlies the normal distribution.
```

A fraction written `a/b` is stacked in a display and set on the line, a/b,
everywhere else: in running text, in scripts (`x^(1/4)` is x^{1/4}) and inside
the numerator and denominator of a stacked fraction. Compound parts get
parentheses: `(1 + sqrt 5)/2` reads (1 + √5)/2. `frac(a, b)`, `a over b` and
`\frac{a}{b}` always stack. This is how print sets fractions, and what LaTeX's
`/` and `\frac` mean.

Consecutive display lines form one display. They are aligned at their first
relation, or at `&` if you place one. A line that starts with a relation
continues the line above. A line that starts with `+` or `−` is indented under
the right-hand side:

```
    (a + b)^2 = (a + b)(a + b)
              = a^2 + 2 a b + b^2
```

A trailing `#label` numbers the line, as in (1), and makes it a target. Only
labelled lines are numbered.

A line that ends with `cases` or a matrix keyword (`matrix`, `pmatrix`,
`bmatrix`, `vmatrix`, `Vmatrix`, `Bmatrix`) takes the lines indented below it as
rows:

```
    abs(x) = cases
      x    if x >= 0
      -x   otherwise

    R = matrix
      cos theta   -sin theta
      sin theta   cos theta
```

The cells of a row are separated by `&`, by commas, by two or more blanks, or,
failing those, by single blanks.

Displays may also be written `$$ formula $$` on one line, or between lines that
hold only `$$`.

### Figures

```
figure "plot.png" width 60% #plot
  alt "The parabola y = x squared, opening upwards through the origin."
  The graph of $y = x^2$.
```

A figure places a PNG or JPEG image, centred, with a numbered caption: "Figure
1. The graph of y = x²." The file name is relative to the source file.

- **width:** a percentage of the text width, or `cm`, `mm`, `in` or `pt`. The
  default is the image's own size at 96 pixels per inch, capped at the text width.
- **alt:** required. It is what a reader who cannot see the figure is told, and
  tekx refuses a figure without it. The lines after it form the caption.
- **Structure:** the image is a `Figure` with its alternative text, and the
  caption is a `Caption`; both sit inside an `Aside`.

PNG images may be gray, gray and alpha, RGB, RGBA or palette, of any bit depth;
transparency becomes a soft mask. Interlaced PNG is refused. JPEG (gray or RGB)
is embedded as it is.

### Other blocks

| block | meaning |
|---|---|
| `abstract` + body | the abstract, set narrower and smaller under a heading |
| `quote` + body, or lines starting with `> ` | a block quotation |
| ```` ``` ```` … ```` ``` ```` | verbatim code (an optional language name after the fence) |
| `let MATH [be PROSE]` | "Let 𝑓 : ℝ → ℝ be continuous." — also declares `f` a function |
| `contents` | a table of contents with page numbers and links |
| `references` + entries | the bibliography; entries are `key: text`, cited as `@key` |
| `newpage` | start a new page |

Comments run from `//` (at the start of a line or after a blank) to the end of
the line.

## Inline text

| write | get |
|---|---|
| `*emphasis*` | *emphasis* (italic; upright inside italic) |
| `**strong**` | **strong** (bold) |
| `` `code` `` | `code` in the monospaced font |
| `$x^2$` | inline mathematics |
| `[text](https://…)` | a link |
| `<https://…>` or a bare `https://…` | a link that shows its address |
| `@label` | a cross-reference |
| `$` | a dollar sign when no formula can close it: `$5 and $10` stays text |
| `^[text]` | a footnote, numbered and set at the foot of the page |
| `{name}` | a text macro |
| `--`, `---`, `...` | – — … |
| `"quotes"`, `'quotes'` | “quotes”, ‘quotes’ |
| `~` | a non-breaking space |
| `\*` | a literal character (`\` escapes the next one) |

A footnote's text may hold any inline markup, mathematics included. Its mark is
a link to the note, and the note is tagged as an `FENote` that follows its mark
in the reading order.

Long words are hyphenated (US English patterns). Lines are justified by the
Knuth–Plass algorithm.

## Mathematics

The aim is that a formula reads aloud the way it is written, and that it is
written the way a mathematician says it.

### Atoms

- **Letters:** a single letter is an italic variable, `x`.
- **Greek:** spelled out, `alpha`, `beta` … `omega`, `Gamma` … `Omega`, plus
  `varepsilon`, `vartheta`, `varphi`, `varrho`, `varsigma`, `varpi`.
- **Letters with digits:** `x1` means x₁ and `alpha2` means α₂.
- **Sets:** `RR`, `NN`, `ZZ`, `QQ`, `CC`, `HH`, `PP`, `FF`, `EE` give ℝ, ℕ, ℤ, ℚ,
  ℂ, ℍ, ℙ, 𝔽, 𝔼.
- **Symbols:** `infinity` (or `infty`, `oo`), `partial`, `nabla`, `emptyset`,
  `aleph`, `hbar`, `ell`, `degree`, `cdots`, `ldots`, `vdots`, `ddots`; `...`
  chooses … or ⋯ from context.
- **Numbers:** `3.14`.
- **Words:** in quotes are set as text, `"speed"`. The words `if`, `otherwise`,
  `and`, `or`, `when`, `where`, `unless`, `for all`, `prime`, `odd`, `even` are
  text without quotes.
- **Unknown words:** a word of four or more letters is one upright name, so
  `velocity = distance/time` reads as you would expect. Shorter unknown words
  are products of letters, so `abc` is a·b·c.
- **Unicode:** any symbol can be typed directly, `∑ ≤ ∀ ∈ ℝ`, as can
  superscripts and subscripts, `x²`, `a₁`.

### Operators, loosest first

| binding | operators |
|---|---|
| 1 | `&` alignment |
| 2, 3 | `;` `,` |
| 5 | `if` `when` `where` `for` `otherwise` |
| 8 | `:` (as in `f: A -> B`, `{x : x > 0}`) |
| 10, 12 | `<=>` `iff` · `=>` `implies` |
| 14, 16 | `or` · `and` (as words) |
| 20 | relations: `=` `!=` `<` `>` `<=` `>=` `~` `~=` `approx` `equiv` `propto` `in` `notin` `not in` `subset` `subseteq` `supset` `->` `to` `|->` `mapsto` `:=` `divides` `perp` `parallel` … |
| 30 | `over` (a fraction of everything around it) |
| 40 | `+` `-` `+-` `-+` `union` `setminus` `oplus` |
| 42 | `/` with blanks around it |
| 45 | juxtaposition with a blank: `2 x`, `1/2 x` |
| 50 | `*` (·) `times` `cdot` `div` `intersect` `circ` `otimes` `mod`, and `/` without blanks |
| 55 | unary `-` |
| 60 | juxtaposition without a blank: `2x`, `n(n + 1)` |
| 70 | `^` `_` `!` `'` `squared` `cubed` `inverse` `transpose` |

`/` makes a fraction and drops the parentheses that only grouped:
`(a + b)/(c - d)` is a fraction of a + b over c − d.

Blanks group. `1/2 x` is ½·x while `1/2x` is 1/(2x); `h/2pi` is h/(2π);
`sin x / x` is (sin x)/x.

### Scripts

`x^2`, `x_i`, `x_i^2`, `x^(n+1)` (parentheses group and disappear), `x_max`
(words of three or more letters are upright), `f'`, `f''`, `e^-x`, `A^T`,
`x squared`, `f inverse`.

### Functions

`sin`, `cos`, `tan`, `log`, `ln`, `exp`, `det`, `dim`, `ker`, `gcd`, … and
anything declared with `define operator` are upright and take an argument
without parentheses: `sin 2 pi x`. An argument runs until `+`, a relation, or
another function name. So `sin x cos y` means (sin x)(cos y).

`f`, `g`, `h`, `F`, `G`, `H`, names declared with `define function`, and names
introduced by `f(x) = …`, `f: A -> B` or `let` apply to a parenthesized argument:
`f(x)`, `g(x, y)`, `f^(-1)(y)`, `f'(x)`. Other letters followed by parentheses
multiply: `x(y + 1)`.

### Big operators and limits

```
sum k^2 for k from 1 to n           sum_(k=1)^n k^2        ∑_(k=1)^n k²
sum from k = 1 to n of k^2          sum over i in I of x_i
prod_(p prime) 1/(1 - p^(-s))
integral of f(x) dx from 0 to 1     integral_0^1 f(x) dx   int_0^1 f
integral of e^(-x^2) dx from -infinity to infinity
limit of f(x) as x approaches 0     lim_(x -> 0) f(x)      limit of g(t) as t goes to 0 from the right
max_(i) a_i     min(a, b)     sup     inf     liminf     limsup
union_(i=1)^n A_i     intersection_i B_i
```

A big operator's body extends over products but stops at `+`, `−` and
relations; use parentheses for more. Integrals end at their differentials
(`dx`, `dt`, `dtheta`), which are set with an upright d. In display style, limits
of sums, products, unions and `lim` go under and over the operator, while those of
integrals go beside it.

### Roots, fences and arrays

| write | get |
|---|---|
| `sqrt x`, `sqrt(x + 1)`, `square root of x` | √x |
| `root 3 of x`, `root(3, x)`, `cube root of x` | ∛x |
| `abs(x)`, `|x|`, `absolute value of x` | \|x\| |
| `norm(v)`, `||v||` | ‖v‖ |
| `floor(x)`, `ceil(x)` | ⌊x⌋, ⌈x⌉ |
| `binom(n, k)`, `n choose k` | binomial coefficient |
| `{x in RR : x > 0}`, `{1, 2, 3}` | sets |
| `[0, 1)` | intervals: brackets need not match |
| `[1, 2; 3, 4]`, `(1, 2; 3, 4)`, `matrix(1, 2; 3, 4)` | matrices (`;` separates rows) |
| `cases(x if x >= 0; -x otherwise)` | cases |

Parentheses, brackets and bars grow with their contents.

### Accents and styles

`hat x`, `bar x` (an overline on longer arguments), `vec v`, `tilde x`,
`dot x`, `ddot x`, `check x`, `breve x`, `overline(z + w)`. Unicode combining
marks also work: `x̂`.

`bb R`, `cal A`, `frak g`, `bold v`, `bolditalic v`, `sans x`, `mono x`,
`upright d`; `text "words"`.

### Quantifiers and logic

`for all epsilon > 0, there exists delta > 0` is set as ∀ε > 0, ∃δ > 0. Also
`forall`, `exists`, `exists unique`, `not`, `land`, `lor`, `implies`, `iff`,
`therefore`, `because`.

### LaTeX notation

A formula may be written in LaTeX's notation instead, and it means the same:

```
    \sum_{k=1}^{n} k = \frac{n(n+1)}{2}
    f(x) = \begin{cases} x & \text{if } x \ge 0 \\ -x & \text{otherwise} \end{cases}
```

The result is the same formula, with the same layout, MathML, speech and copy
text. The copy text is written in tekx's own notation.

As soon as a formula uses one TeX command, TeX's reading rules apply to that
formula:
- braces group without being drawn, and `\{ \}` are set braces;
- a run of letters is a product, as in TeX (`\text{…}` or `\mathrm{…}` makes a
  word).

What is understood:
- **Commands with arguments:** `\frac`, `\dfrac`, `\tfrac`, `\sqrt` and
  `\sqrt[n]`, `\binom`, `\text` and `\mbox`, `\mathrm`, `\operatorname`,
  `\pmod`.
- **Styles:** `\mathbb`, `\mathcal`, `\mathfrak`, `\mathbf`, `\boldsymbol`,
  `\mathsf`, `\mathtt`.
- **Names:** every Greek letter, operator, relation, arrow, delimiter and
  function name under its LaTeX name.
- **Spacing:** `\,` `\:` `\;` `\!` `\quad` `\qquad`.
- **Environments:** `pmatrix`, `bmatrix`, `Bmatrix`, `vmatrix`, `Vmatrix`,
  `matrix`, `cases`, `aligned`, `gathered`, `split` and `array`.
- **Dropped,** because tekx sizes delimiters and chooses styles itself:
  `\left`, `\right`, `\big` and its relatives, `\displaystyle`, `\limits`.

An unknown command is an error with a suggestion: *unknown TeX command \fracc;
did you mean \frac?*

### Spacing

`quad` and `qquad` insert one and two ems of space, as in
`f(x) = x^2 quad x > 0`. Screen readers hear a pause there.

### Seeing how a formula was read

`tekx explain` shows the grouping tekx chose, with every implicit
parenthesis written out:

```
$ tekx explain 'sin x/y + 1/2 x'
formula    sin x/y + 1/2 x
reads as   sin(x/y) + (1/2)·x
says       sine the fraction x over y end fraction plus one half x
copies as  sin x/y + 1/2 x
MathML     <math …>
```

Give formulas as arguments, or one per line on the standard input.

### What each formula produces

For every formula, tekx produces:

- **the typeset formula,** laid out by the rules of TeX's Appendix G with the
  font's OpenType MATH constants;
- **MathML,** both as PDF structure elements in the MathML namespace and as an
  associated file of each `Formula`;
- **speech,** which becomes the formula's alternative text, for example "the sum
  from k equals 1 to n of k squared";
- **copy text,** a linear Unicode form, for example `∑_(k=1)^n k²`. It is what
  you get when you copy the formula out of the PDF, and tekx reads it back as the
  same formula.

## Cross-references

Mark a target with `#label` at the end of a section heading, theorem line or
display line, or with a reference key. Refer to it with `@label`:

| target | reads |
|---|---|
| section | Section 2.1 |
| theorem-like block | Theorem 3, Lemma 4 … |
| display line | (5) |
| figure | Figure 2 |
| appendix | Appendix A |

References may point forward. Every reference is a link to a structure
destination of its target, and says its full name to a screen reader
("Equation 5"). An unknown label is an error with a suggestion.

Habits from LaTeX are understood. `Theorem @main` reads "Theorem 3", not
"Theorem Theorem 3", and `(@gauss)` reads "(5)". After a plural, references give
their numbers alone: `Lemmas @a and @b` reads "Lemmas 2 and 4".

## Citations and bibliography

```
bibliography "refs.bib"

Landau [@landau] improved [@hooley, Thm. 2; @bambah], as did @hooley.
```

| write | get |
|---|---|
| `[@key]` | [3] |
| `[@a; @b; @c]` | [1–3]: sorted, and runs of three or more become ranges |
| `[@key, Thm. 2]` | [3, Thm. 2]: a note after the key, which may hold mathematics |
| `[@a, p. 4; @b]` | [1, p. 4; 2] |
| `@key` | [3], when no `#key` target exists |

The `.bib` file is read as BibTeX reads it: `@string` macros, `#`
concatenation, month names, braces, accents (`{\"U}`, `\'e`, `\ss`),
mathematics in titles, and names written "First von Last", "von Last, First" or
"von Last, Jr, First". Only the cited entries are printed, sorted by author,
year and title, in the style of the AMS:

> Christopher Hooley, *On the intervals between numbers that are sums of two
> squares*, Acta Math. **127** (1971), 279–297, doi:10.1007/BF02392056.

Articles, books, collections, proceedings, theses, reports and miscellaneous
entries are supported. DOIs and arXiv identifiers become links. The list is set
where a `references` line stands, or at the end of the document. A `references`
block may also give entries by hand (`key: text`), cited the same way.

A citation key and a `#label` live apart: `[@landau]` is always the paper, even
when an equation is labelled `#landau`. A bare `@landau` that could mean either
is an error that says which to write. An unknown key is an error with a
suggestion.

## Macros and definitions

All definitions are hoisted: a macro may be used before the line that defines
it.

### Text macros

```
define tekx2 as "tekx 2"
define cite(who, year = "2026") as "{who} ({year})"

Built with {tekx2}; see {cite(Knuth)} or {cite Lamport, 1994}.
```

Arguments are positional or named (`{cite year: 1984, who: Knuth}`), and a
parameter may have a default. Macros may use macros. `{today}` and `{tekx}` are
built in.

### Block macros

```
define block aside(title = "Aside")
  quote
    **{title}.** {body}

aside "Careful"
  The body of the call replaces {body}.
```

The indented lines under the definition are the template. A call is the
macro's name, its arguments, and an indented body. A template line that
consists of `{body}` alone receives the body's lines, keeping their structure;
`{body}` inside a line receives the body joined into one line.

### Mathematical notation

```
define function phi, psi                 // phi(x) is an application
define operator Var, Tr                  // upright, prefix: Tr A
define norm2(v) as ||v||_2 read as "the two-norm of {v}"
define E(x) as EE[x] read as "the expected value of {x}"
```

A notation macro is expanded in the formula, so the layout, MathML and copy text
show the expansion. Its `read as` text replaces the speech. `{v}` in the
`read as` text is the speech of the argument.

### Environments

```
define axiom2 like theorem titled "Postulate"
```

The new block `axiom2` is numbered and styled like a theorem, under the given
name.

### Packages

```
use house-notation 1.2
```

takes in a package's definitions: notation, environments and macros shared by
a group's documents. A package may also bring blocks of its own, implemented
by programs that rewrite the document. Packages are pinned by content hash in
`tekx.lock` and managed with `tekx pkg`; see [packages.md](packages.md).

## Errors

Errors name the file, line and column, and say how to fix what they can:

```
paper.tekx:5:16: error: unknown macro {golden} (define it with: define golden as "...")
paper.tekx:7:1: error: unknown block 'theorm'; did you mean 'theorem'?
paper.tekx:9:12: error: this '(' is never closed
paper.tekx:12:5: error: unknown label @strat; did you mean @start?
```

When there are errors, nothing is written: tekx never leaves a broken or stale
PDF behind.
