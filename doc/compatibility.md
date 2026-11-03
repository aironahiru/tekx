# Compatibility

These rules hold from tekx 1.0.1 on. The test suite enforces them.

| rule | |
|---|---|
| Documents keep building | A document that builds with 1.0.1 builds with every later 1.x release with the same structure, numbering, references, citations, and the same MathML, speech and copy text for every formula. Line and page breaks may move as typography improves. |
| A document names its version | `tekx 1.0` at the top declares it. An older tekx refuses a document written for a newer one, with a message. |
| Nothing disappears without warning | A feature is deprecated with a warning naming its replacement for at least one minor release before it is removed in a major release. |
| Same inputs, same bytes | The same release, the same sources (including `tekx.lock` and the packages it pins) and the same `SOURCE_DATE_EPOCH` give an identical PDF. |
| Formats are interfaces | The page description declares its version (`x tekx-pdl 1`). The document tree that package filters read changes in minor releases only by addition; a filter passes unknown nodes through. |
| Packages stay pinned | The `tekx.lock` format is stable. A package's `requires` names the oldest tekx it supports. A build uses only what the lock pins. |
| Output meets its standards | PDF/UA-2, WTPDF 1.0 (accessibility and reuse) and PDF/A-4, checked with veraPDF. |

## How the rules are kept

The golden files are the contract:

| file | holds |
|---|---|
| `test/golden/*.out` | the document tree, each formula's MathML, speech and copy text, the tag tree |
| `test/tour.structure` | the tag tree of `examples/tour.tekx` |
| `test/explain.out` | how formulas are read |
| `test/errors/*.err` | diagnostics |
| `test/luatex/layout.out` | the position of every glyph in 104 formulas |

A change to an expected output is deliberate and is listed in
[CHANGELOG.md](../CHANGELOG.md). Every example is built twice and must give the
same bytes, and the shell pipeline must equal the driver.
