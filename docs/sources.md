# Consolidated source registry

Status legend: **fetched** = HTTP 200 on 2026-11-02 (PDFs checked for a `%PDF-` header); **repo** = verified with `git ls-remote` or cloned (commit given); **cited** = URL taken from a LaTeX Project primary source but the host refuses automated fetch (HTTP 403), so not independently verified.

## LaTeX Project: kernel, latex-lab, release notes

| Resource | URL | Status |
|---|---|---|
| LaTeX Project home | https://www.latex-project.org/ | fetched |
| LaTeX News index | https://www.latex-project.org/news/latex2e-news/ | fetched |
| LaTeX News (all issues, PDF) | https://www.latex-project.org/news/latex2e-news/ltnews.pdf | fetched |
| LaTeX News 44 (2026-11) | https://www.latex-project.org/news/latex2e-news/ltnews44.pdf | fetched |
| source2e (kernel source) | https://www.latex-project.org/help/documentation/source2e.pdf | fetched |
| cmdguide | https://texdoc.org/serve/cmdguide/0 | fetched |
| lttagging | https://texdoc.org/serve/lttagging/0 | fetched |
| ltsockets | https://texdoc.org/serve/ltsockets-doc/0 | fetched |
| interface3 | https://texdoc.org/serve/interface3/0 | fetched |
| documentmetadata-support | https://texdoc.org/serve/documentmetadata-support/0 | fetched |
| latex-lab-math | https://texdoc.org/serve/latex-lab-math/0 | fetched |
| latex-lab-table | https://texdoc.org/serve/latex-lab-table/0 | fetched |
| latex-lab-graphic | https://texdoc.org/serve/latex-lab-graphic/0 | fetched |
| latex-lab-title | https://texdoc.org/serve/latex-lab-title/0 | fetched |
| latex-lab-namespace | https://texdoc.org/serve/latex-lab-namespace/0 | fetched |
| Kernel repository | https://github.com/latex3/latex2e | repo @ `2ec6765` |
| L3 repository | https://github.com/latex3/latex3 | repo @ `65b3640` |
| U. Fischer, "News from the LaTeX Tagged PDF project: 2026" (TUGboat 47:2) | https://www.latex-project.org/publications/2026-UFi-TUG-tb146fischer-tagging26.pdf | fetched |
| F. Mittelbach, "Towards accessible document classes" (TUGboat 47:2) | https://www.latex-project.org/publications/2026-FMi-TUG-tb146mittelbach-accessibleclasses.pdf | fetched |
| Blueprint (Mittelbach & Rowley 2020), publications index | https://www.latex-project.org/publications/indexbyyear/2020/ | fetched |
| TUGboat 47:2 (TUG 2026 proceedings) | https://tug.org/TUGboat/tb47-2/ | fetched |
| TUG accessibility working group | https://tug.org/twg/accessibility/ | fetched |

## Tagging project

| Resource | URL | Status |
|---|---|---|
| Project site | https://latex3.github.io/tagging-project/ | fetched |
| Usage instructions | https://latex3.github.io/tagging-project/documentation/usage-instructions | fetched |
| Tagging status table | https://latex3.github.io/tagging-project/tagging-status/ | fetched |
| WTPDF/STEM examples | https://latex3.github.io/tagging-project/documentation/wtpdf/ | fetched |
| Repository (status YAML, docs, issues) | https://github.com/latex3/tagging-project | repo @ `d0001d5` |
| Structure tests (pdf_structure) | https://github.com/latex3/pdf_structure | repo |

## Packages (CTAN, texdoc, repositories)

| Package | CTAN | Documentation | Repository |
|---|---|---|---|
| tagpdf | https://ctan.org/pkg/tagpdf (fetched) | https://texdoc.org/serve/tagpdf/0 (fetched) | https://github.com/latex3/tagpdf (repo @ `c669286`) |
| latex-lab | https://ctan.org/pkg/latex-lab (fetched) | per-module texdoc above | in latex2e repo `required/latex-lab` |
| pdfmanagement | https://ctan.org/pkg/pdfmanagement (fetched); https://ctan.org/pkg/pdfmanagement-testphase (fetched) | https://texdoc.org/serve/l3pdfmeta/0 (fetched) | https://github.com/latex3/pdfresources (repo @ `0fd0d9e`) |
| luamml | https://ctan.org/pkg/luamml (fetched) | https://texdoc.org/serve/luamml/0 (fetched) | https://github.com/latex3/luamml (repo @ `c5b9bb0`) |
| latex-tagging-status | https://ctan.org/pkg/latex-tagging-status (fetched) | – | in tagging-project repo |
| unicode-math | https://ctan.org/pkg/unicode-math (fetched) | – | – |
| lua-unicode-math | https://ctan.org/pkg/lua-unicode-math (fetched) | – | – |
| luaotfload | https://ctan.org/pkg/luaotfload (fetched) | https://texdoc.org/serve/luaotfload/0 (fetched) | https://github.com/latex3/luaotfload (repo) |
| l3kernel / l3backend | https://ctan.org/pkg/l3kernel ; https://ctan.org/pkg/l3backend (fetched) | https://texdoc.org/serve/interface3/0 | https://github.com/latex3/latex3 |
| hyperref | https://ctan.org/pkg/hyperref (fetched) | – | – |
| ltx-talk | https://ctan.org/pkg/ltx-talk (fetched) | – | https://github.com/josephwright/ltx-talk (repo) |
| lualineno | https://ctan.org/pkg/lualineno (fetched) | – | – |
| latex-base | https://ctan.org/pkg/latex-base (fetched) | – | https://github.com/latex3/latex2e |
| l3build (test system) | – | – | https://github.com/latex3/l3build (repo) |

## Engines

| Resource | URL | Status |
|---|---|---|
| LuaTeX | https://www.luatex.org/ | fetched |
| LuaTeX source | https://gitlab.lisn.upsaclay.fr/texlive/luatex | fetched |
| LuaTeX manual | https://texdoc.org/serve/luatex/0 | fetched |
| CTAN luatex / pdftex | https://ctan.org/pkg/luatex ; https://ctan.org/pkg/pdftex | fetched |
| pdfTeX | https://www.tug.org/applications/pdftex/ | fetched |
| pdfTeX manual | https://texdoc.org/serve/pdftex/0 | fetched |
| TeX Live | https://www.tug.org/texlive/ | fetched |

## Standards and validation

| Resource | URL | Status |
|---|---|---|
| ISO 32000-2:2020 (OBP), cited in tagpdf bibliography | https://www.iso.org/obp/ui/#iso:std:iso:32000:-2:ed-2:v1:en | cited |
| PDF 2.0 sponsored access (PDF Association) | https://pdfa.org/resource/iso-32000-2/ | cited (403) |
| ISO 32000-1:2008 (Adobe-hosted) | https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf | fetched |
| Deriving HTML from PDF v1.2 (PDF Association), cited by LaTeX News 44 | https://pdfa.org/deriving-pdf-to-html-version-1-2/ | cited (403) |
| veraPDF | https://verapdf.org/ ; https://docs.verapdf.org/validation/ | fetched |
| veraPDF validation profiles | https://github.com/veraPDF/veraPDF-validation-profiles | repo |
| veraPDF library | https://github.com/veraPDF/veraPDF-library | repo |
| W3C MathML Core | https://www.w3.org/TR/mathml-core/ | fetched |
| W3C MathML 4 | https://www.w3.org/TR/mathml4/ | fetched |
| W3C WAI-ARIA 1.2 | https://www.w3.org/TR/wai-aria-1.2/ | fetched |

## Consumers and assistive technology

| Resource | URL | Status |
|---|---|---|
| pdf.js | https://github.com/mozilla/pdf.js ; https://mozilla.github.io/pdf.js/ | repo; fetched |
| PDFium structure-tree header | https://pdfium.googlesource.com/pdfium/+/refs/heads/main/public/fpdf_structtree.h | fetched |
| PDFium mirror | https://github.com/chromium/pdfium | repo |
| Foxit PDF/UA-2 PoC | https://github.com/foxitsoftware/PDF_UA-2 | repo; README fetched (raw) |
| MathCAT | https://github.com/NSoiffer/MathCAT | repo |
| NVDA | https://www.nvaccess.org/ ; https://github.com/nvaccess/nvda | fetched; repo |
| ngPDF | https://ngpdf.com/ | fetched |
| LaTeX showtags | https://texlive.net/showtags | fetched |
| PAC | https://pac.pdf-accessibility.org/ | fetched |
