#!/bin/sh
# doc/img/make.sh -- render the pictures in the documentation from their
# sources: each NAME.tekx becomes NAME.png, cropped to its ink.
# Needs pdftoppm (poppler) and Python with Pillow.  Run from the top directory.
set -eu
T=${TEKX:-./tekx}
for src in doc/img/*.tekx; do
	name=${src%.tekx}
	"$T" -o "$name.pdf" "$src"
	pdftoppm -r 144 -png -f 1 -l 1 "$name.pdf" "$name.page"
	python3 - "$name.page-1.png" "$name.png" <<'PY'
import sys
from PIL import Image, ImageChops
im = Image.open(sys.argv[1]).convert('L')
w, h = im.size
body = im.crop((0, 0, w, int(h * 0.88)))          # leave out the page number
box = ImageChops.difference(body, Image.new('L', body.size, 255)).getbbox()
m = 16
box = (max(0, box[0] - m), max(0, box[1] - m), min(w, box[2] + m), min(body.height, box[3] + m))
body.crop(box).quantize(16).save(sys.argv[2], optimize=True)
PY
	rm -f "$name.pdf" "$name.page-1.png"
done
