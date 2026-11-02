# The text PDFium (the viewer in Chrome and Edge) gives for each page:
# FPDFText_GetText over the whole page, which is what a select-all and
# copy returns.  Pages are separated by form feeds.
#
#	python3 -I test/paste/pdfium.py PYPDFIUM2_DIR FILE.pdf
import sys

sys.path.insert(0, sys.argv[1])
import pypdfium2 as pdfium  # noqa: E402

doc = pdfium.PdfDocument(sys.argv[2])
pages = []
for i in range(len(doc)):
    pages.append(doc[i].get_textpage().get_text_range())
sys.stdout.write("\f".join(pages).replace("\r\n", "\n") + "\n")
