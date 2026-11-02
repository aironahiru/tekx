//! PDF 2.0 writer producing tagged, PDF/UA-2 and WTPDF conformant files.
//!
//! Everything is generated here: object graph, compressed content streams,
//! subset CID fonts with ToUnicode maps, the logical structure tree with
//! PDF 2.0 / MathML / tekx namespaces, links with structure destinations,
//! bookmarks, associated MathML files and XMP metadata.

pub mod deflate;
pub mod md5;
mod xmp;

use crate::font::{Font, ProgramKind};
use crate::frame::{ArtifactKind, AttrValue, Document, ElemId, FontId, Item, LinkTarget, Ns};
use std::collections::{BTreeMap, BTreeSet, HashMap};
use std::fmt::Write as _;

pub const TEKX_NS: &str = "https://github.com/aironahiru/tekx/ns/1";

/// Map a tekx structure role to a standard type (role mapping target).
pub fn tekx_role(tag: &str) -> (&'static str, Ns) {
    match tag {
        "title" => ("H1", Ns::Pdf2),
        "subtitle" | "author" | "date" | "affiliation" | "text-block" | "code-block"
        | "bib-entry" => ("P", Ns::Pdf2),
        "title-block" | "equation" | "proof" | "theorem-like" | "block" | "figure-block" => {
            ("Div", Ns::Pdf2)
        }
        "abstract" | "section" | "subsection" | "subsubsection" | "paragraph-section"
        | "bibliography" | "contents" => ("Sect", Ns::Pdf2),
        "section-heading" => ("H2", Ns::Pdf2),
        "subsection-heading" => ("H3", Ns::Pdf2),
        "subsubsection-heading" => ("H4", Ns::Pdf2),
        "paragraph-heading" | "abstract-heading" => ("H5", Ns::Pdf2),
        "heading-number" | "item-label" | "equation-number" | "footnote-mark" | "bib-label" => {
            ("Lbl", Ns::Pdf2)
        }
        "theorem-head" | "strong" => ("Strong", Ns::Pdf2),
        "proof-head" | "emphasis" => ("Em", Ns::Pdf2),
        "qed" | "text-fragment" => ("Span", Ns::Pdf2),
        "quote" => ("BlockQuote", Ns::Pdf),
        "code" => ("Code", Ns::Pdf),
        "contents-entry" => ("P", Ns::Pdf2),
        _ => ("Div", Ns::Pdf2),
    }
}

/// PDF 1.7 equivalents for PDF 2.0-only standard types (fallback RoleMap).
fn pdf17_fallback(t: &str) -> Option<&'static str> {
    match t {
        "Title" => Some("P"),
        "FENote" => Some("Note"),
        "Aside" => Some("Note"),
        "Em" | "Strong" | "Sub" => Some("Span"),
        "DocumentFragment" => Some("Art"),
        "H7" | "H8" | "H9" | "H10" => Some("H6"),
        _ => None,
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
struct Ref(usize);

impl std::fmt::Display for Ref {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{} 0 R", self.0)
    }
}

struct Writer {
    objects: Vec<Option<Vec<u8>>>,
    compress: bool,
}

impl Writer {
    fn new(compress: bool) -> Self {
        Writer {
            objects: Vec::new(),
            compress,
        }
    }

    fn alloc(&mut self) -> Ref {
        self.objects.push(None);
        Ref(self.objects.len())
    }

    fn set(&mut self, r: Ref, body: impl Into<Vec<u8>>) {
        self.objects[r.0 - 1] = Some(body.into());
    }

    fn stream(&mut self, r: Ref, dict_entries: &str, data: &[u8], compress: bool) {
        let (bytes, filter) = if compress && self.compress {
            (deflate::zlib_compress(data), " /Filter /FlateDecode")
        } else {
            (data.to_vec(), "")
        };
        let mut body = format!(
            "<< {dict_entries}{filter} /Length {} >>\nstream\n",
            bytes.len()
        )
        .into_bytes();
        body.extend_from_slice(&bytes);
        body.extend_from_slice(b"\nendstream");
        self.set(r, body);
    }

    fn finish(self, root: Ref, id: [u8; 16]) -> Vec<u8> {
        let mut out = b"%PDF-2.0\n%\xE2\xE3\xCF\xD3\n".to_vec();
        let mut offsets = Vec::with_capacity(self.objects.len());
        for (i, obj) in self.objects.iter().enumerate() {
            offsets.push(out.len());
            out.extend_from_slice(format!("{} 0 obj\n", i + 1).as_bytes());
            out.extend_from_slice(obj.as_deref().unwrap_or(b"null"));
            out.extend_from_slice(b"\nendobj\n");
        }
        let xref = out.len();
        let mut x = format!("xref\n0 {}\n0000000000 65535 f\r\n", self.objects.len() + 1);
        for o in offsets {
            let _ = write!(x, "{o:010} 00000 n\r\n");
        }
        let hex: String = id.iter().map(|b| format!("{b:02X}")).collect();
        let _ = write!(
            x,
            "trailer\n<< /Size {} /Root {} /ID [<{hex}> <{hex}>] >>\nstartxref\n{xref}\n%%EOF\n",
            self.objects.len() + 1,
            root
        );
        out.extend_from_slice(x.as_bytes());
        out
    }
}

/// Encode a text string: literal for printable ASCII, UTF-16BE hex otherwise.
pub fn text_string(s: &str) -> String {
    if s.chars().all(|c| (' '..='~').contains(&c)) {
        let mut out = String::with_capacity(s.len() + 2);
        out.push('(');
        for c in s.chars() {
            match c {
                '(' | ')' | '\\' => {
                    out.push('\\');
                    out.push(c);
                }
                _ => out.push(c),
            }
        }
        out.push(')');
        out
    } else {
        let mut out = String::from("<FEFF");
        for u in s.encode_utf16() {
            let _ = write!(out, "{u:04X}");
        }
        out.push('>');
        out
    }
}

/// Encode a PDF name (without the leading slash handled by caller).
pub fn name(s: &str) -> String {
    let mut out = String::from("/");
    for b in s.bytes() {
        if b.is_ascii_alphanumeric() || b"-_.+".contains(&b) {
            out.push(b as char);
        } else {
            let _ = write!(out, "#{b:02X}");
        }
    }
    out
}

fn num(v: f64) -> String {
    if (v - v.round()).abs() < 1e-6 {
        format!("{}", v.round() as i64)
    } else {
        let s = format!("{v:.4}");
        let s = s.trim_end_matches('0').trim_end_matches('.');
        if s == "-0" {
            "0".into()
        } else {
            s.to_string()
        }
    }
}

fn utf16_hex(s: &str) -> String {
    let mut out = String::new();
    for u in s.encode_utf16() {
        let _ = write!(out, "{u:04X}");
    }
    out
}

/// Deterministic six-letter subset tag.
fn subset_tag(font: &Font, used: &BTreeSet<u16>) -> String {
    let mut seed = font.ps_name.clone().into_bytes();
    for g in used {
        seed.extend_from_slice(&g.to_be_bytes());
    }
    let h = md5::md5(&seed);
    h.iter().take(6).map(|b| (b'A' + b % 26) as char).collect()
}

#[derive(Default)]
struct FontUsage {
    glyphs: BTreeMap<u16, String>,
}

enum Kid {
    Elem(ElemId),
    Mc { page: usize, mcid: usize },
    Annot { page: usize, annot: Ref },
}

/// Options influencing PDF output.
#[derive(Clone, Debug)]
pub struct PdfOptions {
    pub compress: bool,
}

impl Default for PdfOptions {
    fn default() -> Self {
        PdfOptions { compress: true }
    }
}

/// Serialise a laid-out document to PDF bytes.
pub fn write_pdf(doc: &Document, opts: &PdfOptions) -> Vec<u8> {
    let mut w = Writer::new(opts.compress);
    let catalog = w.alloc();
    let pages_ref = w.alloc();
    let struct_root = w.alloc();
    let page_refs: Vec<Ref> = doc.pages.iter().map(|_| w.alloc()).collect();
    let font_refs: Vec<Ref> = doc.fonts.iter().map(|_| w.alloc()).collect();
    let elem_refs: Vec<Ref> = doc.structure.elems.iter().map(|_| w.alloc()).collect();

    let mut usage: Vec<FontUsage> = doc.fonts.iter().map(|_| FontUsage::default()).collect();
    let mut kids: Vec<Vec<Kid>> = doc.structure.elems.iter().map(|_| Vec::new()).collect();
    let mut attached = vec![false; doc.structure.elems.len()];
    let mut parent_tree: BTreeMap<usize, Vec<ElemId>> = BTreeMap::new();
    let mut annot_parents: BTreeMap<usize, ElemId> = BTreeMap::new();
    let n_pages = doc.pages.len();
    let mut next_annot_key = n_pages;
    let mut page_annots: Vec<Vec<Ref>> = vec![Vec::new(); n_pages];

    fn attach(e: ElemId, doc: &Document, kids: &mut [Vec<Kid>], attached: &mut [bool]) {
        if attached[e.0] {
            return;
        }
        attached[e.0] = true;
        if let Some(p) = doc.structure.get(e).parent {
            attach(p, doc, kids, attached);
            kids[p.0].push(Kid::Elem(e));
        }
    }
    attached[doc.root.0] = true;

    // ---- content streams -------------------------------------------------
    let mut contents = Vec::with_capacity(n_pages);
    for (pi, page) in doc.pages.iter().enumerate() {
        let mut c = String::new();
        let mut mcid = 0usize;
        let mut tag_stack: Vec<Option<ElemId>> = Vec::new();
        let mut mcs: Vec<ElemId> = Vec::new();
        let h = page.height;
        for item in &page.items {
            match item {
                Item::Text {
                    font,
                    size,
                    x,
                    y,
                    glyphs,
                    invisible,
                } => {
                    if glyphs.is_empty() {
                        continue;
                    }
                    // Safety net (ISO 14289-2 8.2.2): content outside any marked
                    // content sequence is emitted as an artifact.
                    let wrap = tag_stack.is_empty();
                    if wrap {
                        c.push_str("/Artifact BMC\n");
                    }
                    let f = &doc.fonts[font.0];
                    let scale = size / f.units_per_em;
                    let _ = write!(c, "BT\n/F{} {} Tf\n", font.0, num(*size));
                    if *invisible {
                        c.push_str("3 Tr\n");
                    }
                    let _ = writeln!(c, "1 0 0 1 {} {} Tm", num(*x), num(h - y));
                    c.push('[');
                    let mut pending_adj = 0.0f64;
                    for g in glyphs {
                        if pending_adj.abs() > 0.0005 {
                            let _ = write!(c, "{}", num(pending_adj));
                        }
                        let _ = write!(c, "<{:04X}>", g.gid);
                        let natural = f.advance(g.gid) * scale;
                        // TJ adjustment is in thousandths of text space, positive moves left.
                        pending_adj = -(g.advance - natural) * 1000.0 / size;
                        let u = &mut usage[font.0].glyphs;
                        let entry = u.entry(g.gid).or_default();
                        if entry.is_empty() && !g.text.is_empty() {
                            *entry = g.text.clone();
                        }
                    }
                    c.push_str("] TJ\n");
                    if *invisible {
                        c.push_str("0 Tr\n");
                    }
                    c.push_str("ET\n");
                    if wrap {
                        c.push_str("EMC\n");
                    }
                }
                Item::Rule {
                    x,
                    y,
                    width,
                    height,
                } => {
                    let wrap = tag_stack.is_empty();
                    if wrap {
                        c.push_str("/Artifact BMC\n");
                    }
                    let _ = writeln!(
                        c,
                        "{} {} {} {} re f",
                        num(*x),
                        num(h - y - height),
                        num(*width),
                        num(*height)
                    );
                    if wrap {
                        c.push_str("EMC\n");
                    }
                }
                Item::BeginTag(e) => {
                    let elem = doc.structure.get(*e);
                    let _ = writeln!(c, "{} <</MCID {}>> BDC", name(&elem.tag), mcid);
                    attach(*e, doc, &mut kids, &mut attached);
                    kids[e.0].push(Kid::Mc { page: pi, mcid });
                    mcs.push(*e);
                    mcid += 1;
                    tag_stack.push(Some(*e));
                }
                Item::EndTag | Item::EndArtifact | Item::EndActualText => {
                    c.push_str("EMC\n");
                    tag_stack.pop();
                }
                Item::BeginArtifact(kind) => {
                    match kind {
                        ArtifactKind::Pagination => {
                            c.push_str("/Artifact <</Type /Pagination>> BDC\n")
                        }
                        ArtifactKind::Layout => c.push_str("/Artifact <</Type /Layout>> BDC\n"),
                        ArtifactKind::Other => c.push_str("/Artifact BMC\n"),
                    }
                    tag_stack.push(None);
                }
                Item::BeginActualText(t) => {
                    let _ = writeln!(c, "/Span <</ActualText <FEFF{}>>> BDC", utf16_hex(t));
                    tag_stack.push(None);
                }
                Item::Link {
                    x,
                    y,
                    width,
                    height,
                    target,
                    elem,
                    contents,
                } => {
                    let annot = w.alloc();
                    let key = next_annot_key;
                    next_annot_key += 1;
                    let rect = format!(
                        "[{} {} {} {}]",
                        num(*x),
                        num(h - y - height),
                        num(x + width),
                        num(h - y)
                    );
                    let action = match target {
                        LinkTarget::Url(u) => format!("<< /S /URI /URI {} >>", text_string(u)),
                        LinkTarget::Internal { elem: te, page, y } => {
                            let pr = page_refs[(*page).min(n_pages - 1)];
                            let top = doc.pages[(*page).min(n_pages - 1)].height - y;
                            format!(
                                "<< /S /GoTo /D [{pr} /XYZ null {} null] /SD [{} /XYZ null {} null] >>",
                                num(top),
                                elem_refs[te.0],
                                num(top)
                            )
                        }
                    };
                    w.set(
                        annot,
                        format!(
                            "<< /Type /Annot /Subtype /Link /Rect {rect} /Border [0 0 0] /F 4 /StructParent {key} /Contents {} /A {action} >>",
                            text_string(contents)
                        ),
                    );
                    attach(*elem, doc, &mut kids, &mut attached);
                    kids[elem.0].push(Kid::Annot { page: pi, annot });
                    annot_parents.insert(key, *elem);
                    page_annots[pi].push(annot);
                }
            }
        }
        // Close any unbalanced marked content defensively.
        for _ in tag_stack {
            c.push_str("EMC\n");
        }
        parent_tree.insert(pi, mcs);
        contents.push(c);
    }

    // ---- fonts -----------------------------------------------------------
    for (fi, font) in doc.fonts.iter().enumerate() {
        let used: BTreeSet<u16> = usage[fi].glyphs.keys().copied().collect();
        write_font(&mut w, font_refs[fi], font, &used, &usage[fi].glyphs);
    }

    // ---- pages -----------------------------------------------------------
    let font_res: String = doc
        .fonts
        .iter()
        .enumerate()
        .map(|(i, _)| format!("/F{} {} ", i, font_refs[i]))
        .collect();
    for (pi, page) in doc.pages.iter().enumerate() {
        let cref = w.alloc();
        w.stream(cref, "", contents[pi].as_bytes(), true);
        let annots = if page_annots[pi].is_empty() {
            String::new()
        } else {
            format!(
                " /Annots [{}]",
                page_annots[pi]
                    .iter()
                    .map(|r| r.to_string())
                    .collect::<Vec<_>>()
                    .join(" ")
            )
        };
        w.set(
            page_refs[pi],
            format!(
                "<< /Type /Page /Parent {pages_ref} /MediaBox [0 0 {} {}] /Resources << /Font << {font_res}>> >> /Contents {cref} /StructParents {pi} /Tabs /S{annots} >>",
                num(page.width),
                num(page.height)
            ),
        );
    }
    w.set(
        pages_ref,
        format!(
            "<< /Type /Pages /Kids [{}] /Count {} >>",
            page_refs
                .iter()
                .map(|r| r.to_string())
                .collect::<Vec<_>>()
                .join(" "),
            n_pages
        ),
    );

    // ---- structure tree --------------------------------------------------
    let ns_pdf2 = w.alloc();
    let ns_pdf = w.alloc();
    let ns_mathml = w.alloc();
    let ns_tekx = w.alloc();
    w.set(
        ns_pdf2,
        "<< /Type /Namespace /NS (http://iso.org/pdf2/ssn) >>",
    );
    w.set(
        ns_pdf,
        "<< /Type /Namespace /NS (http://iso.org/pdf/ssn) >>",
    );
    w.set(
        ns_mathml,
        "<< /Type /Namespace /NS (http://www.w3.org/1998/Math/MathML) >>",
    );
    let ns_ref = |ns: Ns| match ns {
        Ns::Pdf2 => ns_pdf2,
        Ns::Pdf => ns_pdf,
        Ns::MathMl => ns_mathml,
        Ns::Tekx => ns_tekx,
    };

    // Elements with no content anywhere below them are dropped.
    let mut has_content = vec![false; doc.structure.elems.len()];
    fn mark(e: usize, kids: &[Vec<Kid>], has: &mut [bool]) -> bool {
        let mut any = false;
        for k in &kids[e] {
            any |= match k {
                Kid::Elem(c) => mark(c.0, kids, has),
                _ => true,
            };
        }
        has[e] = any;
        any
    }
    mark(doc.root.0, &kids, &mut has_content);

    let mut tekx_roles: BTreeSet<String> = BTreeSet::new();
    let mut used_tags: BTreeSet<String> = BTreeSet::new();
    let mut af_counter = 0usize;
    let ts = xmp::pdf_date(doc.metadata.timestamp);
    for (i, elem) in doc.structure.elems.iter().enumerate() {
        if !has_content[i] && i != doc.root.0 {
            continue;
        }
        if elem.ns == Ns::Tekx {
            tekx_roles.insert(elem.tag.clone());
        } else {
            used_tags.insert(elem.tag.clone());
        }
        let mut d = format!(
            "<< /Type /StructElem /S {} /NS {}",
            name(&elem.tag),
            ns_ref(elem.ns)
        );
        let parent = match elem.parent {
            Some(p) => elem_refs[p.0].to_string(),
            None => struct_root.to_string(),
        };
        let _ = write!(d, " /P {parent}");
        let mut k = String::new();
        for kid in &kids[i] {
            match kid {
                Kid::Elem(c) => {
                    if has_content[c.0] {
                        let _ = write!(k, "{} ", elem_refs[c.0]);
                    }
                }
                Kid::Mc { page, mcid } => {
                    let _ = write!(
                        k,
                        "<< /Type /MCR /Pg {} /MCID {} >> ",
                        page_refs[*page], mcid
                    );
                }
                Kid::Annot { page, annot } => {
                    let _ = write!(
                        k,
                        "<< /Type /OBJR /Pg {} /Obj {} >> ",
                        page_refs[*page], annot
                    );
                }
            }
        }
        let _ = write!(d, " /K [{}]", k.trim_end());
        if let Some(a) = &elem.alt {
            let _ = write!(d, " /Alt {}", text_string(a));
        }
        if let Some(a) = &elem.actual_text {
            let _ = write!(d, " /ActualText {}", text_string(a));
        }
        if let Some(l) = &elem.lang {
            let _ = write!(d, " /Lang {}", text_string(l));
        }
        if let Some(t) = &elem.title {
            let _ = write!(d, " /T {}", text_string(t));
        }
        if let Some(id) = &elem.id {
            let _ = write!(d, " /ID {}", text_string(id));
        }
        if !elem.attributes.is_empty() {
            let mut by_owner: BTreeMap<&str, Vec<(&str, &AttrValue)>> = BTreeMap::new();
            for a in &elem.attributes {
                by_owner
                    .entry(a.owner.as_str())
                    .or_default()
                    .push((a.key.as_str(), &a.value));
            }
            d.push_str(" /A [");
            for (owner, entries) in by_owner {
                let _ = write!(d, "<< /O {}", name(owner));
                for (key, value) in entries {
                    let v = match value {
                        AttrValue::Name(n) => name(n),
                        AttrValue::Int(i) => i.to_string(),
                        AttrValue::Num(f) => num(*f),
                        AttrValue::Text(t) => text_string(t),
                    };
                    let _ = write!(d, " {} {}", name(key), v);
                }
                d.push_str(" >> ");
            }
            d.push(']');
        }
        let mut afs = Vec::new();
        if let Some(m) = &elem.mathml {
            af_counter += 1;
            afs.push(embed_file(
                &mut w,
                &format!("mathml-{af_counter}.xml"),
                "application/mathml+xml",
                "Supplement",
                &format!("MathML of formula {af_counter}"),
                m.as_bytes(),
                &ts,
            ));
        }
        if let Some(s) = &elem.source {
            afs.push(embed_file(
                &mut w,
                &format!("formula-{af_counter}.tekx"),
                "text/plain",
                "Source",
                &format!("tekx source of formula {af_counter}"),
                s.as_bytes(),
                &ts,
            ));
        }
        if !afs.is_empty() {
            let _ = write!(
                d,
                " /AF [{}]",
                afs.iter()
                    .map(|r| r.to_string())
                    .collect::<Vec<_>>()
                    .join(" ")
            );
        }
        d.push_str(" >>");
        w.set(elem_refs[i], d);
    }
    // Unused element objects become null (already allocated); fill them.
    for (i, r) in elem_refs.iter().enumerate() {
        if !has_content[i] && i != doc.root.0 {
            w.set(*r, "null");
        }
    }

    let mut rolemap_ns = String::new();
    let mut rolemap_fallback = String::new();
    for role in &tekx_roles {
        let (target, ns) = tekx_role(role);
        let _ = write!(
            rolemap_ns,
            " {} [{} {}]",
            name(role),
            name(target),
            ns_ref(ns)
        );
        let fb = pdf17_fallback(target).unwrap_or(target);
        let _ = write!(rolemap_fallback, " {} {}", name(role), name(fb));
    }
    for t in &used_tags {
        if let Some(fb) = pdf17_fallback(t) {
            let _ = write!(rolemap_fallback, " {} {}", name(t), name(fb));
        }
    }
    w.set(
        ns_tekx,
        format!(
            "<< /Type /Namespace /NS {} /RoleMapNS <<{} >> >>",
            text_string(TEKX_NS),
            rolemap_ns
        ),
    );

    let parent_tree_ref = w.alloc();
    let mut nums = String::new();
    for (pi, mcs) in &parent_tree {
        let _ = write!(
            nums,
            "{} [{}] ",
            pi,
            mcs.iter()
                .map(|e| elem_refs[e.0].to_string())
                .collect::<Vec<_>>()
                .join(" ")
        );
    }
    for (key, e) in &annot_parents {
        let _ = write!(nums, "{} {} ", key, elem_refs[e.0]);
    }
    w.set(
        parent_tree_ref,
        format!("<< /Nums [{}] >>", nums.trim_end()),
    );
    w.set(
        struct_root,
        format!(
            "<< /Type /StructTreeRoot /K [{}] /ParentTree {parent_tree_ref} /ParentTreeNextKey {next_annot_key} /Namespaces [{ns_pdf2} {ns_pdf} {ns_mathml} {ns_tekx}] /RoleMap <<{} >> >>",
            elem_refs[doc.root.0], rolemap_fallback
        ),
    );

    // ---- outlines ----------------------------------------------------------
    let outlines_ref = write_outlines(&mut w, doc, &page_refs, &elem_refs);

    // ---- metadata & catalog ------------------------------------------------
    let meta_ref = w.alloc();
    let xmp = xmp::build(&doc.metadata);
    w.stream(
        meta_ref,
        "/Type /Metadata /Subtype /XML",
        xmp.as_bytes(),
        false,
    );
    let lang = if doc.metadata.language.is_empty() {
        "en".to_string()
    } else {
        doc.metadata.language.clone()
    };
    let mut cat = format!(
        "<< /Type /Catalog /Pages {pages_ref} /StructTreeRoot {struct_root} /MarkInfo << /Marked true >> /Lang {} /Metadata {meta_ref} /ViewerPreferences << /DisplayDocTitle true >>",
        text_string(&lang)
    );
    if let Some(o) = outlines_ref {
        let _ = write!(cat, " /Outlines {o} /PageMode /UseOutlines");
    }
    cat.push_str(" >>");
    w.set(catalog, cat);

    let mut id_seed = xmp.into_bytes();
    for c in &contents {
        id_seed.extend_from_slice(&md5::md5(c.as_bytes()));
    }
    let id = md5::md5(&id_seed);
    w.finish(catalog, id)
}

fn embed_file(
    w: &mut Writer,
    file: &str,
    mime: &str,
    rel: &str,
    desc: &str,
    data: &[u8],
    date: &str,
) -> Ref {
    let stream = w.alloc();
    w.stream(
        stream,
        &format!(
            "/Type /EmbeddedFile /Subtype {} /Params << /ModDate {} /Size {} >>",
            name(mime),
            text_string(date),
            data.len()
        ),
        data,
        true,
    );
    let spec = w.alloc();
    w.set(
        spec,
        format!(
            "<< /Type /Filespec /F {} /UF {} /Desc {} /AFRelationship /{rel} /EF << /F {stream} /UF {stream} >> >>",
            text_string(file),
            text_string(file),
            text_string(desc)
        ),
    );
    spec
}

fn write_outlines(
    w: &mut Writer,
    doc: &Document,
    page_refs: &[Ref],
    elem_refs: &[Ref],
) -> Option<Ref> {
    if doc.outlines.is_empty() || doc.pages.is_empty() {
        return None;
    }
    let root = w.alloc();
    let refs: Vec<Ref> = doc.outlines.iter().map(|_| w.alloc()).collect();
    let n = doc.outlines.len();
    // parent index for each entry (None = root)
    let mut parent: Vec<Option<usize>> = vec![None; n];
    let mut stack: Vec<usize> = Vec::new();
    for i in 0..n {
        while let Some(&top) = stack.last() {
            if doc.outlines[top].level >= doc.outlines[i].level {
                stack.pop();
            } else {
                break;
            }
        }
        parent[i] = stack.last().copied();
        stack.push(i);
    }
    let children =
        |p: Option<usize>| -> Vec<usize> { (0..n).filter(|&i| parent[i] == p).collect() };
    let count_desc = |p: usize| -> usize {
        (0..n)
            .filter(|&i| {
                let mut q = parent[i];
                while let Some(x) = q {
                    if x == p {
                        return true;
                    }
                    q = parent[x];
                }
                false
            })
            .count()
    };
    for i in 0..n {
        let o = &doc.outlines[i];
        let siblings = children(parent[i]);
        let pos = siblings.iter().position(|&s| s == i).unwrap();
        let page = o.page.min(doc.pages.len() - 1);
        let top = doc.pages[page].height - o.y;
        let mut d = format!(
            "<< /Title {} /Parent {} /A << /S /GoTo /D [{} /XYZ null {} null] /SD [{} /XYZ null {} null] >>",
            text_string(&o.title),
            parent[i].map(|p| refs[p]).unwrap_or(root),
            page_refs[page],
            num(top),
            elem_refs[o.elem.0],
            num(top)
        );
        if pos > 0 {
            let _ = write!(d, " /Prev {}", refs[siblings[pos - 1]]);
        }
        if pos + 1 < siblings.len() {
            let _ = write!(d, " /Next {}", refs[siblings[pos + 1]]);
        }
        let ch = children(Some(i));
        if !ch.is_empty() {
            let _ = write!(
                d,
                " /First {} /Last {} /Count {}",
                refs[ch[0]],
                refs[*ch.last().unwrap()],
                count_desc(i)
            );
        }
        d.push_str(" >>");
        w.set(refs[i], d);
    }
    let top = children(None);
    w.set(
        root,
        format!(
            "<< /Type /Outlines /First {} /Last {} /Count {} >>",
            refs[top[0]],
            refs[*top.last().unwrap()],
            n
        ),
    );
    Some(root)
}

fn write_font(
    w: &mut Writer,
    type0: Ref,
    font: &Font,
    used: &BTreeSet<u16>,
    texts: &BTreeMap<u16, String>,
) {
    let tag = subset_tag(font, used);
    let base = format!("{tag}+{}", font.ps_name);
    let k = 1000.0 / font.units_per_em;
    let (program, kind) = match font.subset_program(used, &base) {
        Ok(p) => p,
        Err(e) => panic!("cannot subset font {}: {e}", font.ps_name),
    };
    let file_ref = w.alloc();
    match kind {
        ProgramKind::CidCff => w.stream(file_ref, "/Subtype /CIDFontType0C", &program, true),
        ProgramKind::TrueType => w.stream(
            file_ref,
            &format!("/Length1 {}", program.len()),
            &program,
            true,
        ),
    }
    let desc_ref = w.alloc();
    let file_key = if kind == ProgramKind::CidCff {
        "FontFile3"
    } else {
        "FontFile2"
    };
    let stem_v = 50.0 + (font.weight as f64 / 65.0).powi(2);
    w.set(
        desc_ref,
        format!(
            "<< /Type /FontDescriptor /FontName {} /Flags {} /FontBBox [{} {} {} {}] /ItalicAngle {} /Ascent {} /Descent {} /CapHeight {} /XHeight {} /StemV {} /{file_key} {file_ref} >>",
            name(&base),
            font.pdf_flags(),
            num(font.bbox[0] * k),
            num(font.bbox[1] * k),
            num(font.bbox[2] * k),
            num(font.bbox[3] * k),
            num(font.italic_angle),
            num(font.ascender * k),
            num(font.descender * k),
            num(font.cap_height * k),
            num(font.x_height * k),
            num(stem_v.round())
        ),
    );
    // Widths: group consecutive glyph ids.
    let mut widths = String::new();
    let ids: Vec<u16> = used.iter().copied().collect();
    let mut i = 0;
    while i < ids.len() {
        let start = ids[i];
        let mut run = vec![num(font.advance(start) * k)];
        let mut j = i + 1;
        while j < ids.len() && ids[j] == ids[j - 1] + 1 {
            run.push(num(font.advance(ids[j]) * k));
            j += 1;
        }
        let _ = write!(widths, "{} [{}] ", start, run.join(" "));
        i = j;
    }
    let cid_ref = w.alloc();
    let subtype = if kind == ProgramKind::CidCff {
        "CIDFontType0"
    } else {
        "CIDFontType2"
    };
    let cid_map = if kind == ProgramKind::TrueType {
        " /CIDToGIDMap /Identity"
    } else {
        ""
    };
    w.set(
        cid_ref,
        format!(
            "<< /Type /Font /Subtype /{subtype} /BaseFont {} /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /FontDescriptor {desc_ref} /DW 0 /W [{}]{cid_map} >>",
            name(&base),
            widths.trim_end()
        ),
    );
    let tu_ref = w.alloc();
    w.stream(tu_ref, "", to_unicode_cmap(texts).as_bytes(), true);
    w.set(
        type0,
        format!(
            "<< /Type /Font /Subtype /Type0 /BaseFont {} /Encoding /Identity-H /DescendantFonts [{cid_ref}] /ToUnicode {tu_ref} >>",
            name(&base)
        ),
    );
}

fn to_unicode_cmap(texts: &BTreeMap<u16, String>) -> String {
    let mut s = String::from(
        "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n",
    );
    let entries: Vec<(&u16, &String)> = texts.iter().filter(|(_, t)| !t.is_empty()).collect();
    for chunk in entries.chunks(100) {
        let _ = writeln!(s, "{} beginbfchar", chunk.len());
        for (g, t) in chunk {
            let _ = writeln!(s, "<{:04X}> <{}>", g, utf16_hex(t));
        }
        s.push_str("endbfchar\n");
    }
    s.push_str("endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n");
    s
}

/// Convenience: which fonts does a document reference (for diagnostics).
pub fn font_names(doc: &Document) -> HashMap<FontId, String> {
    doc.fonts
        .iter()
        .enumerate()
        .map(|(i, f)| (FontId(i), f.ps_name.clone()))
        .collect()
}
