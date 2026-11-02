#!/usr/bin/env python3
"""Generate the deterministic benchmark corpus used in docs/08-end-to-end-pipeline.md.

Writes body.tex (40 sections x 4 paragraphs with inline math, one numbered
equation, one itemize and one tabular per section; ~49 pages in article
class) plus five driver files into the directory given as argv[1].
"""
import os
import random
import sys

out_dir = sys.argv[1]
os.makedirs(out_dir, exist_ok=True)
random.seed(7)
words = ("tagging structure element marked content namespace mapping formula artifact "
         "annotation stream parser object dictionary reference compliance viewer engine "
         "kernel package hook socket").split()
body = []
for s in range(40):
    body.append(r"\section{Section %d}" % (s + 1))
    for p in range(4):
        body.append(" ".join(random.choice(words) for _ in range(90))
                    + r" with $x_{%d}^2 + y = \frac{a}{b}$ inline." % p)
        body.append("")
    body.append(r"\begin{equation} \sum_{k=1}^{n} k^2 = \frac{n(n+1)(2n+1)}{6} \end{equation}")
    body.append(r"\begin{itemize}\item alpha item\item beta item\end{itemize}")
    body.append(r"\begin{tabular}{ll} Key & Value \\ A & 1 \\ B & 2 \end{tabular}")
    body.append("")
with open(os.path.join(out_dir, "body.tex"), "w") as f:
    f.write("\n".join(body))

META = r"\DocumentMetadata{lang=en,pdfstandard=UA-2,tagging=on%s}" + "\n"
drivers = {
    "b1-pdftex-legacy": ("", "amsmath"),
    "b2-pdftex-tagged": (META % "", "amsmath"),
    "b3-lua-legacy": ("", "unicode-math"),
    "b4-lua-tagged-af": (META % "", "unicode-math"),
    "b5-lua-tagged-se": (META % ",tagging-setup={math/setup=mathml-SE}", "unicode-math"),
}
for name, (meta, pkg) in drivers.items():
    with open(os.path.join(out_dir, name + ".tex"), "w") as f:
        f.write(meta + "\\documentclass{article}\n\\usepackage{%s}\n\\title{Benchmark}\n"
                "\\begin{document}\n\\input{body}\n\\end{document}\n" % pkg)
