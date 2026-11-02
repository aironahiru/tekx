//! Laid-out document: pages of positioned items plus the logical structure
//! tree. This is the boundary between layout and the output backends.
//!
//! Coordinates are in PostScript points with the origin at the top-left of
//! the page and y growing downwards; the PDF backend flips them.

/// Index of a font in [`Document::fonts`].
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct FontId(pub usize);

/// Index of a structure element in [`StructTree`].
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct ElemId(pub usize);

/// One glyph with its intended advance and the text it represents.
#[derive(Clone, Debug)]
pub struct Glyph {
    pub gid: u16,
    /// Horizontal advance in points (including kerning / justification).
    pub advance: f64,
    /// Unicode text this glyph stands for (may be empty for parts of a cluster).
    pub text: String,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ArtifactKind {
    /// Page furniture: page numbers, running heads.
    Pagination,
    /// Decorative layout elements: rules, ornaments.
    Layout,
    /// Unspecified artifact (e.g. a soft hyphen at a line break).
    Other,
}

/// A target of a link annotation.
#[derive(Clone, Debug)]
pub enum LinkTarget {
    Url(String),
    /// Jump to a structure element located on `page` at vertical position `y`.
    Internal {
        elem: ElemId,
        page: usize,
        y: f64,
    },
}

#[derive(Clone, Debug)]
pub enum Item {
    /// A run of glyphs from one font, starting at (x, y) on the baseline.
    Text {
        font: FontId,
        size: f64,
        x: f64,
        y: f64,
        glyphs: Vec<Glyph>,
        invisible: bool,
    },
    /// A filled rectangle (fraction bars, radical overbars, table rules).
    Rule {
        x: f64,
        y: f64,
        width: f64,
        height: f64,
    },
    /// Start of real content belonging to a structure element.
    BeginTag(ElemId),
    EndTag,
    BeginArtifact(ArtifactKind),
    EndArtifact,
    /// Replacement text for the enclosed content (copy/paste, search).
    BeginActualText(String),
    EndActualText,
    /// A clickable area, owned by a `Link` structure element.
    Link {
        x: f64,
        y: f64,
        width: f64,
        height: f64,
        target: LinkTarget,
        elem: ElemId,
        contents: String,
    },
}

#[derive(Clone, Debug)]
pub struct Page {
    pub width: f64,
    pub height: f64,
    pub items: Vec<Item>,
}

/// Structure namespaces.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Ns {
    /// PDF 2.0 standard structure namespace.
    Pdf2,
    /// PDF 1.7 standard structure namespace (types absent from PDF 2.0).
    Pdf,
    /// MathML 3.0.
    MathMl,
    /// The tekx namespace (role mapped onto PDF 2.0 types).
    Tekx,
}

/// An attribute value written into a structure element's `/A` dictionary.
#[derive(Clone, Debug)]
pub enum AttrValue {
    Name(String),
    Int(i64),
    Num(f64),
    Text(String),
}

#[derive(Clone, Debug)]
pub struct Attribute {
    /// Attribute owner, e.g. `Layout`, `Table`, `List`, or the MathML namespace.
    pub owner: String,
    pub key: String,
    pub value: AttrValue,
}

#[derive(Clone, Debug)]
pub struct Elem {
    pub tag: String,
    pub ns: Ns,
    pub parent: Option<ElemId>,
    pub alt: Option<String>,
    pub actual_text: Option<String>,
    pub lang: Option<String>,
    pub title: Option<String>,
    pub id: Option<String>,
    pub attributes: Vec<Attribute>,
    /// MathML document attached as an associated file (Formula only).
    pub mathml: Option<String>,
    /// Linear source attached as an associated file (Formula only).
    pub source: Option<String>,
}

/// The logical structure tree. Children are attached in content order by the
/// backend (an element joins its parent when its first content appears).
#[derive(Clone, Debug, Default)]
pub struct StructTree {
    pub elems: Vec<Elem>,
}

impl StructTree {
    pub fn add(&mut self, tag: &str, ns: Ns, parent: Option<ElemId>) -> ElemId {
        self.elems.push(Elem {
            tag: tag.to_string(),
            ns,
            parent,
            alt: None,
            actual_text: None,
            lang: None,
            title: None,
            id: None,
            attributes: Vec::new(),
            mathml: None,
            source: None,
        });
        ElemId(self.elems.len() - 1)
    }

    pub fn get(&self, id: ElemId) -> &Elem {
        &self.elems[id.0]
    }

    pub fn get_mut(&mut self, id: ElemId) -> &mut Elem {
        &mut self.elems[id.0]
    }

    pub fn attr(&mut self, id: ElemId, owner: &str, key: &str, value: AttrValue) {
        self.elems[id.0].attributes.push(Attribute {
            owner: owner.into(),
            key: key.into(),
            value,
        });
    }
}

/// A bookmark entry.
#[derive(Clone, Debug)]
pub struct Outline {
    pub title: String,
    pub level: usize,
    pub page: usize,
    pub y: f64,
    pub elem: ElemId,
}

/// Document-level metadata.
#[derive(Clone, Debug, Default)]
pub struct Metadata {
    pub title: String,
    pub authors: Vec<String>,
    pub language: String,
    pub subject: Option<String>,
    pub keywords: Vec<String>,
    /// Seconds since the Unix epoch used for all timestamps.
    pub timestamp: i64,
}

/// A fully laid-out document, ready for a backend.
pub struct Document {
    pub fonts: Vec<std::rc::Rc<crate::font::Font>>,
    pub pages: Vec<Page>,
    pub structure: StructTree,
    pub root: ElemId,
    pub outlines: Vec<Outline>,
    pub metadata: Metadata,
}
