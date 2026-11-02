// Inspect what pdf.js (the PDF engine of Firefox) extracts from a tagged PDF.
//
// Usage (from the repository root, after `npm install pdfjs-dist@6.5.438`
// in a directory of your choice):
//
//   PDFJS=/path/to/node_modules/pdfjs-dist node tools/pdfjs_probe.mjs FILE.pdf
//
// Prints: the text-content stream (what a copy operation in the pdf.js text
// layer is built from), the marked-content tags pdf.js sees, the structure
// tree as serialised by page.getStructTree() (roles after RoleMap
// resolution, alt, mathML presence) and document language.
import path from "node:path";
import { pathToFileURL } from "node:url";

const base = process.env.PDFJS;
if (!base) {
  console.error("set PDFJS to the pdfjs-dist package directory");
  process.exit(2);
}
const { getDocument } = await import(
  pathToFileURL(path.join(base, "legacy/build/pdf.mjs")).href
);

const file = process.argv[2];
const doc = await getDocument({ url: file, verbosity: 0 }).promise;
const page = await doc.getPage(1);

const tc = await page.getTextContent({ includeMarkedContent: true });
let text = "";
const tags = [];
for (const item of tc.items) {
  if (item.type === "beginMarkedContentProps" || item.type === "beginMarkedContent") {
    tags.push(item.tag);
  } else if (item.str !== undefined) {
    text += item.str + (item.hasEOL ? "\n" : "");
  }
}
console.log("--- getTextContent() ---");
console.log(text.trimEnd());
console.log("--- marked-content tags (first 12) ---");
console.log(tags.slice(0, 12).join(" "));

const tree = await page.getStructTree();
const lines = [];
const walk = (node, depth) => {
  if (!node || !node.role) return;
  let line = "  ".repeat(depth) + node.role;
  if (node.alt) line += ` alt=${JSON.stringify(node.alt)}`;
  if (node.lang) line += ` lang=${node.lang}`;
  if (node.mathML) line += ` mathML=${JSON.stringify(node.mathML.slice(0, 60))}...`;
  for (const key of ["scope", "rowSpan", "colSpan", "headers", "summary"]) {
    if (node[key] !== undefined) line += ` ${key}=${JSON.stringify(node[key])}`;
  }
  lines.push(line);
  for (const child of node.children || []) walk(child, depth + 1);
};
walk(tree, 0);
console.log("--- getStructTree() ---");
console.log(lines.slice(0, 60).join("\n"));

const meta = await doc.getMetadata();
console.log("--- metadata ---");
console.log(`Language: ${meta.info.Language ?? "(none)"} | PDFFormatVersion: ${meta.info.PDFFormatVersion}`);
