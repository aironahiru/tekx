//! XMP metadata packet (PDF/UA-2 identification, WTPDF declarations).

use crate::frame::Metadata;

/// Civil date from Unix seconds (proleptic Gregorian, UTC).
pub fn civil(ts: i64) -> (i64, u32, u32, u32, u32, u32) {
    let days = ts.div_euclid(86400);
    let secs = ts.rem_euclid(86400);
    let z = days + 719468;
    let era = z.div_euclid(146097);
    let doe = z.rem_euclid(146097);
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = (doy - (153 * mp + 2) / 5 + 1) as u32;
    let m = if mp < 10 { mp + 3 } else { mp - 9 } as u32;
    let y = if m <= 2 { y + 1 } else { y };
    (
        y,
        m,
        d,
        (secs / 3600) as u32,
        (secs / 60 % 60) as u32,
        (secs % 60) as u32,
    )
}

pub fn iso_date(ts: i64) -> String {
    let (y, m, d, hh, mm, ss) = civil(ts);
    format!("{y:04}-{m:02}-{d:02}T{hh:02}:{mm:02}:{ss:02}Z")
}

pub fn pdf_date(ts: i64) -> String {
    let (y, m, d, hh, mm, ss) = civil(ts);
    format!("D:{y:04}{m:02}{d:02}{hh:02}{mm:02}{ss:02}Z")
}

fn esc(s: &str) -> String {
    s.replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('"', "&quot;")
}

pub fn build(meta: &Metadata) -> String {
    let now = iso_date(meta.timestamp);
    let day = &now[..10];
    let version = env!("CARGO_PKG_VERSION");
    let lang = if meta.language.is_empty() {
        "en"
    } else {
        &meta.language
    };
    let mut creators = String::new();
    for a in &meta.authors {
        creators.push_str(&format!("      <rdf:li>{}</rdf:li>\n", esc(a)));
    }
    let creator_block = if creators.is_empty() {
        String::new()
    } else {
        format!("   <dc:creator>\n    <rdf:Seq>\n{creators}    </rdf:Seq>\n   </dc:creator>\n")
    };
    let subject = match &meta.subject {
        Some(s) => format!(
            "   <dc:description>\n    <rdf:Alt>\n     <rdf:li xml:lang=\"x-default\">{}</rdf:li>\n    </rdf:Alt>\n   </dc:description>\n",
            esc(s)
        ),
        None => String::new(),
    };
    let keywords = if meta.keywords.is_empty() {
        String::new()
    } else {
        format!(
            "   <pdf:Keywords>{}</pdf:Keywords>\n",
            esc(&meta.keywords.join(", "))
        )
    };
    let declaration = |kind: &str| {
        format!(
            "     <rdf:li rdf:parseType=\"Resource\">\n      <pdfd:conformsTo>http://pdfa.org/declarations/wtpdf#{kind}1.0</pdfd:conformsTo>\n      <pdfd:claimData>\n       <rdf:Bag>\n        <rdf:li rdf:parseType=\"Resource\">\n         <pdfd:claimBy>tekx {version}</pdfd:claimBy>\n         <pdfd:claimDate>{day}</pdfd:claimDate>\n        </rdf:li>\n       </rdf:Bag>\n      </pdfd:claimData>\n     </rdf:li>\n"
        )
    };
    format!(
        "<?xpacket begin=\"\u{FEFF}\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>
<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">
 <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">
  <rdf:Description rdf:about=\"\"
    xmlns:dc=\"http://purl.org/dc/elements/1.1/\"
    xmlns:pdf=\"http://ns.adobe.com/pdf/1.3/\"
    xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"
    xmlns:pdfuaid=\"http://www.aiim.org/pdfua/ns/id/\"
    xmlns:pdfd=\"http://pdfa.org/declarations/\">
   <dc:format>application/pdf</dc:format>
   <dc:title>
    <rdf:Alt>
     <rdf:li xml:lang=\"x-default\">{title}</rdf:li>
    </rdf:Alt>
   </dc:title>
{creator_block}{subject}   <dc:language>
    <rdf:Bag>
     <rdf:li>{lang}</rdf:li>
    </rdf:Bag>
   </dc:language>
   <pdf:Producer>tekx {version}</pdf:Producer>
{keywords}   <xmp:CreatorTool>tekx {version}</xmp:CreatorTool>
   <xmp:CreateDate>{now}</xmp:CreateDate>
   <xmp:ModifyDate>{now}</xmp:ModifyDate>
   <xmp:MetadataDate>{now}</xmp:MetadataDate>
   <pdfuaid:part>2</pdfuaid:part>
   <pdfuaid:rev>2024</pdfuaid:rev>
   <pdfd:declarations>
    <rdf:Bag>
{acc}{reuse}    </rdf:Bag>
   </pdfd:declarations>
  </rdf:Description>
 </rdf:RDF>
</x:xmpmeta>
<?xpacket end=\"w\"?>",
        title = esc(&meta.title),
        lang = esc(lang),
        acc = declaration("accessibility"),
        reuse = declaration("reuse"),
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn civil_dates() {
        assert_eq!(iso_date(0), "1970-01-01T00:00:00Z");
        assert_eq!(iso_date(1_793_616_300), "2026-11-02T10:45:00Z");
        assert_eq!(pdf_date(951_782_400), "D:20000229000000Z");
    }
}
