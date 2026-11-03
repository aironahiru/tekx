# Changelog

Changes that alter output are listed under each version. See
[doc/compatibility.md](doc/compatibility.md).

## 1.0.1, 2026-11-03

First release.

| area | |
|---|---|
| Language | Settings, sections and appendices, theorem-like blocks and proofs, lists, tables, quotations, code, figures with required alt text, footnotes, contents, macros, notation, environments. |
| Mathematics | tekx notation, read as spoken; LaTeX notation, including `\left`, `\big`, `\overbrace`, `\overset`, `\xrightarrow`, `\displaystyle`, `\limits` and the class commands. Layout follows TeX with the OpenType MATH table; 91 of 104 test formulas match LuaTeX within 0.02 pt. Long displays break automatically. |
| Text | Knuth-Plass line breaking with TeX's passes and hyphen costs, Liang hyphenation, compound-word breaks, character protrusion, stretchable math spacing. |
| Papers | Journal front matter, numbering by section, cross-references, BibTeX with amsplain order and AMS style, numeric, ordered or alphabetic citations. |
| Output | Tagged PDF 2.0 valid as PDF/UA-2, WTPDF and PDF/A-4; MathML, speech and copy text for every formula; HTML with MathML; amsart LaTeX. Reproducible bytes. |
| Tools | `tekx explain`, `tekx -w`, `tekx new`, `tekx import`, the pipeline stages `pp`, `parse`, `eqn`, `set`, `pdf`, `html`. |
| Packages | `use`, `tekx pkg`, registries in directories or git, `tekx.lock` with SHA-256 content hashes, filters and custom blocks. |
