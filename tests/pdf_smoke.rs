use std::rc::Rc;
use tekx::font::Font;
use tekx::frame::*;
use tekx::pdf::{write_pdf, PdfOptions};

fn run(font: &Font, fid: usize, size: f64, s: &str) -> Vec<Glyph> {
    s.chars()
        .map(|c| {
            let gid = font.glyph(c).unwrap_or(0);
            Glyph {
                gid,
                advance: font.advance(gid) * size / font.units_per_em,
                text: c.to_string(),
            }
        })
        .collect::<Vec<_>>()
        .into_iter()
        .map(|g| {
            let _ = fid;
            g
        })
        .collect()
}

#[test]
fn smoke_pdf_is_written() {
    let text = Rc::new(
        Font::parse(std::fs::read("assets/fonts/STIXTwoText-Regular.otf").unwrap()).unwrap(),
    );
    let math = Rc::new(
        Font::parse(std::fs::read("assets/fonts/STIXTwoMath-Regular.otf").unwrap()).unwrap(),
    );
    let mut st = StructTree::default();
    let root = st.add("Document", Ns::Pdf2, None);
    let sect = st.add("section", Ns::Tekx, Some(root));
    let h = st.add("section-heading", Ns::Tekx, Some(sect));
    let p = st.add("P", Ns::Pdf2, Some(sect));
    let link = st.add("Link", Ns::Pdf2, Some(p));
    let formula = st.add("Formula", Ns::Pdf2, Some(p));
    st.get_mut(formula).alt = Some("x squared".into());
    st.get_mut(formula).mathml = Some("<math xmlns=\"http://www.w3.org/1998/Math/MathML\"><msup><mi>x</mi><mn>2</mn></msup></math>".into());
    let m = st.add("math", Ns::MathMl, Some(formula));
    let msup = st.add("msup", Ns::MathMl, Some(m));
    let mi = st.add("mi", Ns::MathMl, Some(msup));
    let mn = st.add("mn", Ns::MathMl, Some(msup));
    let p2 = st.add("P", Ns::Pdf2, Some(sect));

    let mut items = vec![
        Item::BeginTag(h),
        Item::Text {
            font: FontId(0),
            size: 16.0,
            x: 72.0,
            y: 100.0,
            glyphs: run(&text, 0, 16.0, "Introduction"),
            invisible: false,
        },
        Item::EndTag,
        Item::BeginTag(p),
        Item::Text {
            font: FontId(0),
            size: 11.0,
            x: 72.0,
            y: 130.0,
            glyphs: run(&text, 0, 11.0, "Office affine flow, see "),
            invisible: false,
        },
        Item::EndTag,
        Item::BeginTag(link),
        Item::Text {
            font: FontId(0),
            size: 11.0,
            x: 190.0,
            y: 130.0,
            glyphs: run(&text, 0, 11.0, "Section 1"),
            invisible: false,
        },
        Item::EndTag,
        Item::Link {
            x: 190.0,
            y: 120.0,
            width: 45.0,
            height: 13.0,
            target: LinkTarget::Internal {
                elem: sect,
                page: 0,
                y: 90.0,
            },
            elem: link,
            contents: "Section 1".into(),
        },
        Item::BeginTag(p),
        Item::Text {
            font: FontId(0),
            size: 11.0,
            x: 236.0,
            y: 130.0,
            glyphs: run(&text, 0, 11.0, " and "),
            invisible: false,
        },
        Item::EndTag,
        Item::BeginActualText("x^2".into()),
        Item::BeginTag(mi),
        Item::Text {
            font: FontId(1),
            size: 11.0,
            x: 262.0,
            y: 130.0,
            glyphs: run(&math, 1, 11.0, "𝑥"),
            invisible: false,
        },
        Item::EndTag,
        Item::BeginTag(mn),
        Item::Text {
            font: FontId(1),
            size: 8.0,
            x: 268.0,
            y: 126.0,
            glyphs: run(&math, 1, 8.0, "2"),
            invisible: false,
        },
        Item::EndTag,
        Item::EndActualText,
        Item::BeginTag(p),
        Item::Text {
            font: FontId(0),
            size: 11.0,
            x: 274.0,
            y: 130.0,
            glyphs: run(&text, 0, 11.0, "."),
            invisible: false,
        },
        Item::EndTag,
        Item::BeginTag(p2),
        Item::Text {
            font: FontId(0),
            size: 11.0,
            x: 72.0,
            y: 150.0,
            glyphs: run(&text, 0, 11.0, "Second paragraph."),
            invisible: false,
        },
        Item::EndTag,
        Item::BeginArtifact(ArtifactKind::Pagination),
        Item::Text {
            font: FontId(0),
            size: 10.0,
            x: 300.0,
            y: 800.0,
            glyphs: run(&text, 0, 10.0, "1"),
            invisible: false,
        },
        Item::EndArtifact,
    ];
    items.push(Item::Rule {
        x: 72.0,
        y: 160.0,
        width: 0.0,
        height: 0.0,
    });
    let doc = Document {
        fonts: vec![text, math],
        pages: vec![Page {
            width: 595.0,
            height: 842.0,
            items,
        }],
        structure: st,
        root,
        outlines: vec![Outline {
            title: "Introduction".into(),
            level: 1,
            page: 0,
            y: 90.0,
            elem: sect,
        }],
        metadata: Metadata {
            title: "Smoke".into(),
            authors: vec!["Ada".into()],
            language: "en-US".into(),
            timestamp: 1_793_616_300,
            ..Default::default()
        },
    };
    let _ = m;
    let bytes = write_pdf(&doc, &PdfOptions::default());
    std::fs::create_dir_all("target/tmp").unwrap();
    std::fs::write("target/tmp/smoke.pdf", &bytes).unwrap();
    assert!(bytes.starts_with(b"%PDF-2.0"));
}
