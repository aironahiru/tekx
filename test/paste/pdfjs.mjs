// The text pdf.js (Firefox's viewer) gives for each page: text items in
// content order, a newline where pdf.js ends a line, pages separated by
// form feeds.  This is what Firefox's text layer offers for selection.
//
//	node test/paste/pdfjs.mjs PDFJS_DIST FILE.pdf
import fs from "fs";
import { pathToFileURL } from "url";

const dist = process.argv[2];
const pdfjs = await import(pathToFileURL(dist + "/legacy/build/pdf.mjs").href);
const doc = await pdfjs.getDocument({ data: new Uint8Array(fs.readFileSync(process.argv[3])), verbosity: 0 }).promise;
const pages = [];
for (let p = 1; p <= doc.numPages; p++) {
	const tc = await (await doc.getPage(p)).getTextContent();
	let s = "";
	for (const it of tc.items) {
		s += it.str;
		if (it.hasEOL)
			s += "\n";
	}
	pages.push(s);
}
process.stdout.write(pages.join("\f") + "\n");
