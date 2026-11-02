#!/usr/bin/env python3
"""Dump the Tagged-PDF-relevant layers of a PDF file.

Usage:
    python3 -I tools/inspect_pdf.py FILE.pdf MODE [DEPTH]

MODE is one of:
    catalog   PDF version, catalog keys, MarkInfo, Lang, StructTreeRoot keys,
              namespaces with RoleMapNS sizes, fallback RoleMap size
    tree      structure tree (role [namespace] + Alt/ActualText/AF/attributes)
    rolemap   RoleMapNS content of every namespace + fallback /RoleMap + ClassMap
    xmp       pdfuaid / pdfaid / dc:title / dc:language / WTPDF declarations
    stream    marked-content operators (BDC/BMC/EMC) of page 1
    fonts     font resources of all pages with ToUnicode presence
    af        embedded file specifications (associated files)
    table     Table/TR/TH/TD elements with resolved attribute classes

Requires pikepdf (pip install pikepdf).  Read-only.
"""
import re
import sys

import pikepdf
from pikepdf import Array, Dictionary, Name

NS_SHORT = {
    "http://iso.org/pdf/ssn": "pdf",
    "http://iso.org/pdf2/ssn": "pdf2",
    "http://www.w3.org/1998/Math/MathML": "mathml",
    "https://www.latex-project.org/ns/dflt": "latex",
    "https://www.latex-project.org/ns/book": "latex-book",
}


def ns_name(ns_dict):
    if ns_dict is None:
        return "-"
    uri = str(ns_dict.NS)
    return NS_SHORT.get(uri, uri)


def kids_of(elem):
    k = elem.get("/K")
    if k is None:
        return []
    return list(k) if isinstance(k, Array) else [k]


def resolved_attributes(elem, classmap):
    out = []
    for key in ("/A", "/C"):
        val = elem.get(key)
        if val is None:
            continue
        for item in (val if isinstance(val, Array) else [val]):
            if isinstance(item, Dictionary):
                out.append({k: str(v) for k, v in item.items()})
            elif isinstance(item, Name) and classmap is not None and item in classmap:
                cls = classmap[item]
                for c in (cls if isinstance(cls, Array) else [cls]):
                    out.append({"class": str(item), **{k: str(v) for k, v in c.items()}})
    return out


def mode_catalog(pdf):
    root = pdf.Root
    print("PDF version:", pdf.pdf_version)
    print("Catalog keys:", " ".join(sorted(root.keys())))
    for key in ("/MarkInfo", "/Lang", "/ViewerPreferences"):
        if key in root:
            val = root[key]
            if isinstance(val, Dictionary):
                val = {k: str(v) for k, v in val.items()}
            print(f"  {key} = {val}")
    print("Trailer /Info present:", "/Info" in pdf.trailer)
    str_root = root.get("/StructTreeRoot")
    if str_root is None:
        print("StructTreeRoot: absent")
        return
    print("StructTreeRoot keys:", " ".join(sorted(str_root.keys())))
    for ns in str_root.get("/Namespaces", []):
        rm = ns.get("/RoleMapNS")
        print(f"  Namespace {str(ns.NS)}  RoleMapNS entries={len(rm.keys()) if rm is not None else 0}")
    if "/RoleMap" in str_root:
        print("  Fallback /RoleMap entries:", len(str_root.RoleMap.keys()))


def mode_tree(pdf, max_depth):
    str_root = pdf.Root.get("/StructTreeRoot")
    if str_root is None:
        print("no structure tree")
        return
    classmap = str_root.get("/ClassMap")

    def walk(elem, depth):
        if depth > max_depth or not isinstance(elem, Dictionary) or "/S" not in elem:
            return
        extra = []
        for key in ("/Alt", "/ActualText", "/Lang"):
            if key in elem:
                extra.append(f"{key}={str(elem[key])[:40]!r}")
        for af in (elem.get("/AF") or []):
            ef = af.get("/EF")
            sub = ef.F.get("/Subtype") if ef is not None else None
            extra.append(f"AF[{af.get('/AFRelationship')} {sub}]")
        attrs = resolved_attributes(elem, classmap)
        if attrs:
            extra.append("attrs=" + ";".join(",".join(f"{k}:{v}" for k, v in a.items()) for a in attrs))
        mcs = sum(1 for k in kids_of(elem) if isinstance(k, int)
                  or (isinstance(k, Dictionary) and k.get("/Type") == Name.MCR))
        line = "  " * depth + f"{str(elem.S)[1:]} [{ns_name(elem.get('/NS'))}]"
        if mcs:
            line += f" (+{mcs} MC)"
        if extra:
            line += "  " + " ".join(extra)
        print(line)
        for kid in kids_of(elem):
            walk(kid, depth + 1)

    for kid in kids_of(str_root):
        walk(kid, 0)


def mode_rolemap(pdf):
    str_root = pdf.Root.get("/StructTreeRoot")
    for ns in str_root.get("/Namespaces", []):
        rm = ns.get("/RoleMapNS")
        if rm is None:
            continue
        items = []
        for k, v in rm.items():
            if isinstance(v, Array):
                items.append(f"{k[1:]}->{ns_name(v[1])}:{str(v[0])[1:]}")
            else:
                items.append(f"{k[1:]}->{v}")
        print(f"{str(ns.NS)} ({len(items)} entries)")
        print("  " + ", ".join(sorted(items)))
    if "/RoleMap" in str_root:
        print("Fallback /RoleMap:", ", ".join(f"{k[1:]}->{str(v)[1:]}" for k, v in sorted(str_root.RoleMap.items())))
    if "/ClassMap" in str_root:
        print("ClassMap:", ", ".join(str(k) for k in str_root.ClassMap.keys()))


def mode_xmp(pdf):
    meta = pdf.Root.get("/Metadata")
    if meta is None:
        print("no XMP metadata stream")
        return
    xml = meta.read_bytes().decode("utf-8", "replace")
    for tag in ("pdfuaid:part", "pdfuaid:rev", "pdfaid:part", "pdfaid:conformance", "pdfaid:rev",
                "dc:title", "dc:language", "pdfd:conformsTo", "pdf:Producer", "xmp:CreatorTool"):
        vals = re.findall(r"<%s[^>]*>(.*?)</%s>" % (tag, tag), xml, re.S)
        if vals:
            clean = [" ".join(re.sub(r"<[^>]+>", " ", v).split()) for v in vals]
            print(f"{tag}: {clean}")


def mode_stream(pdf, limit):
    page = pdf.pages[0]
    shown = 0
    counts = {}
    for operands, op in pikepdf.parse_content_stream(page):
        o = str(op)
        counts[o] = counts.get(o, 0) + 1
        if o in ("BDC", "BMC", "EMC") and shown < limit:
            parts = []
            for x in operands:
                if isinstance(x, Dictionary):
                    parts.append("<<" + " ".join(f"{k} {v}" for k, v in x.items()) + ">>")
                else:
                    parts.append(str(x))
            print("  " + " ".join(parts + [o]))
            shown += 1
    print("Marked-content operator counts:", {k: counts.get(k, 0) for k in ("BDC", "BMC", "EMC")})
    print("Page /StructParents:", page.obj.get("/StructParents"), "| /Tabs:", page.obj.get("/Tabs"))


def mode_fonts(pdf):
    seen = set()
    for page in pdf.pages:
        for name, font in (page.obj.Resources.get("/Font") or {}).items():
            if font.objgen in seen:
                continue
            seen.add(font.objgen)
            print(f"  {name}: {font.get('/BaseFont')} Subtype={font.get('/Subtype')} ToUnicode={'/ToUnicode' in font}")


def mode_af(pdf):
    n = 0
    for obj in pdf.objects:
        if isinstance(obj, Dictionary) and obj.get("/Type") == Name.Filespec:
            n += 1
            ef = obj.get("/EF")
            sub = ef.F.get("/Subtype") if ef is not None else None
            print(f"  Filespec {obj.get('/F')} AFRelationship={obj.get('/AFRelationship')} Subtype={sub} Desc={obj.get('/Desc')}")
    print("Total file specifications:", n)


def mode_table(pdf):
    str_root = pdf.Root.StructTreeRoot
    classmap = str_root.get("/ClassMap")

    def walk(elem, depth):
        if not isinstance(elem, Dictionary) or "/S" not in elem:
            return
        role = str(elem.S)[1:]
        if role in ("Table", "TR", "TH", "TD"):
            attrs = resolved_attributes(elem, classmap)
            print("  " * depth + role + ("  " + str(attrs) if attrs else ""))
        for kid in kids_of(elem):
            walk(kid, depth + 1)

    for kid in kids_of(str_root):
        walk(kid, 0)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    pdf = pikepdf.open(sys.argv[1])
    mode = sys.argv[2]
    depth = int(sys.argv[3]) if len(sys.argv) > 3 else 12
    {
        "catalog": lambda: mode_catalog(pdf),
        "tree": lambda: mode_tree(pdf, depth),
        "rolemap": lambda: mode_rolemap(pdf),
        "xmp": lambda: mode_xmp(pdf),
        "stream": lambda: mode_stream(pdf, depth),
        "fonts": lambda: mode_fonts(pdf),
        "af": lambda: mode_af(pdf),
        "table": lambda: mode_table(pdf),
    }[mode]()


if __name__ == "__main__":
    main()
