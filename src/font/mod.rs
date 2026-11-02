//! OpenType font engine: everything tekx needs from a font file.
//!
//! Supports CFF (`OTTO`) and TrueType (`glyf`) outlines, `cmap` formats 4 and
//! 12, horizontal metrics, GSUB/GPOS basics and the `MATH` table.

pub mod cff;
pub mod math;
pub mod otl;
pub mod reader;
pub mod truetype;

use cff::{BBox, Cff};
use math::MathTable;
use otl::{Gpos, Gsub};
use reader::{err, FontResult, Reader};
use std::cell::RefCell;
use std::collections::{BTreeSet, HashMap};
use truetype::TrueType;

pub use reader::FontError;

/// The outline technology of a font.
#[derive(Clone, Debug)]
pub enum Outlines {
    Cff(Box<Cff>),
    TrueType(TrueType),
}

/// Kind of embedded font program produced for PDF.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ProgramKind {
    /// Bare CID-keyed CFF (`FontFile3` / `CIDFontType0C`).
    CidCff,
    /// TrueType sfnt (`FontFile2`).
    TrueType,
}

#[derive(Debug)]
pub struct Font {
    data: Vec<u8>,
    tables: HashMap<[u8; 4], (usize, usize)>,
    pub units_per_em: f64,
    pub ascender: f64,
    pub descender: f64,
    pub line_gap: f64,
    pub cap_height: f64,
    pub x_height: f64,
    pub italic_angle: f64,
    pub bbox: [f64; 4],
    pub weight: u16,
    pub is_fixed_pitch: bool,
    pub is_italic: bool,
    pub ps_name: String,
    pub family: String,
    cmap: HashMap<u32, u16>,
    advances: Vec<u16>,
    pub num_glyphs: usize,
    pub gsub: Gsub,
    pub gpos: Gpos,
    pub math: Option<MathTable>,
    pub outlines: Outlines,
    bbox_cache: RefCell<HashMap<u16, BBox>>,
}

fn name_string(data: &[u8], off: usize, len: usize, wanted: u16) -> FontResult<Option<String>> {
    let table = &data[off..off + len];
    let mut r = Reader::new(table);
    let _fmt = r.u16()?;
    let count = r.u16()? as usize;
    let storage = r.u16()? as usize;
    let mut best: Option<String> = None;
    for _ in 0..count {
        let platform = r.u16()?;
        let _enc = r.u16()?;
        let _lang = r.u16()?;
        let id = r.u16()?;
        let length = r.u16()? as usize;
        let offset = r.u16()? as usize;
        if id != wanted {
            continue;
        }
        let start = storage + offset;
        if start + length > table.len() {
            continue;
        }
        let bytes = &table[start..start + length];
        let s = if platform == 3 || platform == 0 {
            let units: Vec<u16> = bytes
                .chunks(2)
                .filter(|c| c.len() == 2)
                .map(|c| u16::from_be_bytes([c[0], c[1]]))
                .collect();
            String::from_utf16_lossy(&units)
        } else {
            bytes.iter().map(|&b| b as char).collect()
        };
        if platform == 3 || best.is_none() {
            best = Some(s);
        }
    }
    Ok(best)
}

impl Font {
    /// Parse an OpenType/TrueType font file.
    pub fn parse(data: Vec<u8>) -> FontResult<Font> {
        let mut r = Reader::new(&data);
        let version = r.u32()?;
        if version != 0x0001_0000 && version != 0x4F54_544F && version != 0x7472_7565 {
            return err("not an OpenType or TrueType font (collections are not supported)");
        }
        let num_tables = r.u16()?;
        r.skip(6)?;
        let mut tables = HashMap::new();
        for _ in 0..num_tables {
            let tag = r.tag()?;
            let _sum = r.u32()?;
            let off = r.u32()? as usize;
            let len = r.u32()? as usize;
            if off.checked_add(len).is_none_or(|e| e > data.len()) {
                return err(format!(
                    "table {} out of range",
                    String::from_utf8_lossy(&tag)
                ));
            }
            tables.insert(tag, (off, len));
        }
        let table = |tag: &[u8; 4]| -> FontResult<&[u8]> {
            match tables.get(tag) {
                Some(&(o, l)) => Ok(&data[o..o + l]),
                None => err(format!(
                    "missing required table '{}'",
                    String::from_utf8_lossy(tag)
                )),
            }
        };

        let head = table(b"head")?;
        let mut hr = Reader::at(head, 18)?;
        let units_per_em = hr.u16()? as f64;
        let mut br = Reader::at(head, 36)?;
        let bbox = [
            br.i16()? as f64,
            br.i16()? as f64,
            br.i16()? as f64,
            br.i16()? as f64,
        ];
        let mac_style = br.u16()?;
        let long_loca = Reader::at(head, 50)?.i16()? == 1;

        let hhea = table(b"hhea")?;
        let mut hh = Reader::at(hhea, 4)?;
        let mut ascender = hh.i16()? as f64;
        let mut descender = hh.i16()? as f64;
        let mut line_gap = hh.i16()? as f64;
        let n_hmetrics = Reader::at(hhea, 34)?.u16()? as usize;

        let num_glyphs = Reader::at(table(b"maxp")?, 4)?.u16()? as usize;

        let hmtx = table(b"hmtx")?;
        let mut advances = Vec::with_capacity(num_glyphs);
        let mut mr = Reader::new(hmtx);
        let mut last = 0u16;
        for i in 0..num_glyphs {
            if i < n_hmetrics {
                last = mr.u16()?;
                mr.skip(2)?;
            }
            advances.push(last);
        }

        let mut cap_height = 0.7 * units_per_em;
        let mut x_height = 0.45 * units_per_em;
        let mut weight = 400;
        let mut is_italic = mac_style & 2 != 0;
        if let Ok(os2) = table(b"OS/2") {
            let mut o = Reader::new(os2);
            let os2_version = o.u16()?;
            o.skip(2)?;
            weight = o.u16()?;
            o.skip(2)?;
            let fs_type = o.u16()?;
            if fs_type & 0x000F == 0x0002 {
                return err(
                    "the font's license does not permit embedding (OS/2 fsType = restricted)",
                );
            }
            if os2.len() >= 78 {
                let mut t = Reader::at(os2, 62)?;
                let fs_selection = t.u16()?;
                is_italic |= fs_selection & 1 != 0;
                let mut m = Reader::at(os2, 68)?;
                let typo_asc = m.i16()? as f64;
                let typo_desc = m.i16()? as f64;
                let typo_gap = m.i16()? as f64;
                if fs_selection & (1 << 7) != 0 || (ascender == 0.0 && descender == 0.0) {
                    ascender = typo_asc;
                    descender = typo_desc;
                    line_gap = typo_gap;
                }
            }
            if os2_version >= 2 && os2.len() >= 90 {
                let mut m = Reader::at(os2, 86)?;
                x_height = m.i16()? as f64;
                cap_height = m.i16()? as f64;
            }
        }

        let (mut italic_angle, mut is_fixed_pitch) = (0.0, false);
        if let Ok(post) = table(b"post") {
            let mut p = Reader::at(post, 4)?;
            italic_angle = p.fixed()?;
            p.skip(4)?;
            is_fixed_pitch = p.u32()? != 0;
        }

        let (ps_name, family) = match tables.get(b"name") {
            Some(&(o, l)) => (
                name_string(&data, o, l, 6)?.unwrap_or_else(|| "Unnamed".into()),
                name_string(&data, o, l, 1)?.unwrap_or_else(|| "Unnamed".into()),
            ),
            None => ("Unnamed".into(), "Unnamed".into()),
        };

        let cmap = Self::parse_cmap(table(b"cmap")?)?;

        let outlines = if let Ok(cff) = table(b"CFF ") {
            Outlines::Cff(Box::new(Cff::parse(cff)?))
        } else {
            let loca = table(b"loca")?;
            let glyf = table(b"glyf")?;
            Outlines::TrueType(TrueType::parse(loca, glyf, num_glyphs, long_loca)?)
        };

        let gsub = match tables.get(b"GSUB") {
            Some(&(o, l)) => Gsub::parse(&data[o..o + l]).unwrap_or_default(),
            None => Gsub::default(),
        };
        let gpos = match tables.get(b"GPOS") {
            Some(&(o, l)) => Gpos::parse(&data[o..o + l]).unwrap_or_default(),
            None => Gpos::default(),
        };
        let math = match tables.get(b"MATH") {
            Some(&(o, l)) => Some(MathTable::parse(&data[o..o + l])?),
            None => None,
        };

        Ok(Font {
            data,
            tables,
            units_per_em,
            ascender,
            descender,
            line_gap,
            cap_height,
            x_height,
            italic_angle,
            bbox,
            weight,
            is_fixed_pitch,
            is_italic,
            ps_name,
            family,
            cmap,
            advances,
            num_glyphs,
            gsub,
            gpos,
            math,
            outlines,
            bbox_cache: RefCell::new(HashMap::new()),
        })
    }

    fn parse_cmap(data: &[u8]) -> FontResult<HashMap<u32, u16>> {
        let mut r = Reader::new(data);
        let _v = r.u16()?;
        let n = r.u16()?;
        let mut candidates: Vec<(u8, usize)> = Vec::new();
        for _ in 0..n {
            let platform = r.u16()?;
            let encoding = r.u16()?;
            let off = r.u32()? as usize;
            let priority = match (platform, encoding) {
                (3, 10) | (0, 4) | (0, 6) => 3,
                (3, 1) | (0, 3) | (0, 1) | (0, 0) => 2,
                (3, 0) => 1,
                _ => 0,
            };
            if priority > 0 {
                candidates.push((priority, off));
            }
        }
        candidates.sort_by(|a, b| a.0.cmp(&b.0));
        let mut map = HashMap::new();
        for (_, off) in candidates {
            let format = Reader::at(data, off)?.u16()?;
            match format {
                4 => Self::cmap4(data, off, &mut map)?,
                12 => Self::cmap12(data, off, &mut map)?,
                _ => {}
            }
        }
        if map.is_empty() {
            return err("no usable Unicode cmap subtable");
        }
        Ok(map)
    }

    fn cmap4(data: &[u8], off: usize, map: &mut HashMap<u32, u16>) -> FontResult<()> {
        let mut r = Reader::at(data, off + 6)?;
        let seg_count = r.u16()? as usize / 2;
        let ends_off = off + 14;
        let starts_off = ends_off + seg_count * 2 + 2;
        let deltas_off = starts_off + seg_count * 2;
        let ranges_off = deltas_off + seg_count * 2;
        for s in 0..seg_count {
            let end = reader::u16_at(data, ends_off + 2 * s)? as u32;
            let start = reader::u16_at(data, starts_off + 2 * s)? as u32;
            let delta = reader::i16_at(data, deltas_off + 2 * s)? as i32;
            let range = reader::u16_at(data, ranges_off + 2 * s)? as usize;
            if start > end || start == 0xFFFF {
                continue;
            }
            for c in start..=end {
                let g = if range == 0 {
                    ((c as i32 + delta) & 0xFFFF) as u16
                } else {
                    let addr = ranges_off + 2 * s + range + 2 * (c - start) as usize;
                    let raw = reader::u16_at(data, addr).unwrap_or(0);
                    if raw == 0 {
                        0
                    } else {
                        ((raw as i32 + delta) & 0xFFFF) as u16
                    }
                };
                if g != 0 {
                    map.insert(c, g);
                }
            }
        }
        Ok(())
    }

    fn cmap12(data: &[u8], off: usize, map: &mut HashMap<u32, u16>) -> FontResult<()> {
        let mut r = Reader::at(data, off + 12)?;
        let groups = r.u32()?;
        for _ in 0..groups {
            let start = r.u32()?;
            let end = r.u32()?;
            let gid = r.u32()?;
            if end < start || end - start > 0x10FFFF {
                continue;
            }
            for c in start..=end {
                let g = gid + (c - start);
                if g != 0 && g < 0x10000 {
                    map.insert(c, g as u16);
                }
            }
        }
        Ok(())
    }

    /// Glyph id for a character.
    pub fn glyph(&self, c: char) -> Option<u16> {
        self.cmap.get(&(c as u32)).copied()
    }

    pub fn has_char(&self, c: char) -> bool {
        self.cmap.contains_key(&(c as u32))
    }

    /// Advance width in font units.
    pub fn advance(&self, gid: u16) -> f64 {
        *self.advances.get(gid as usize).unwrap_or(&0) as f64
    }

    /// Ink bounding box (cached).
    pub fn glyph_bbox(&self, gid: u16) -> BBox {
        if let Some(b) = self.bbox_cache.borrow().get(&gid) {
            return *b;
        }
        let b = match &self.outlines {
            Outlines::Cff(c) => c.glyph_bbox(gid),
            Outlines::TrueType(t) => t.glyph_bbox(gid),
        };
        self.bbox_cache.borrow_mut().insert(gid, b);
        b
    }

    pub fn kern(&self, left: u16, right: u16) -> f64 {
        self.gpos.kern(left, right) as f64
    }

    pub fn is_cff(&self) -> bool {
        matches!(self.outlines, Outlines::Cff(_))
    }

    /// PDF FontDescriptor flags.
    pub fn pdf_flags(&self) -> u32 {
        let mut f = 0u32;
        if self.is_fixed_pitch {
            f |= 1;
        }
        if self.math.is_none() && !self.ps_name.to_lowercase().contains("sans") {
            f |= 2; // serif
        }
        f |= 4; // symbolic: glyphs outside the standard Latin set are used
        if self.is_italic || self.italic_angle != 0.0 {
            f |= 64;
        }
        f
    }

    /// Build the embeddable font program for the given used glyph ids.
    pub fn subset_program(
        &self,
        used: &BTreeSet<u16>,
        subset_name: &str,
    ) -> FontResult<(Vec<u8>, ProgramKind)> {
        match &self.outlines {
            Outlines::Cff(c) => Ok((c.subset(used, subset_name)?, ProgramKind::CidCff)),
            Outlines::TrueType(t) => {
                let mut raw = Vec::new();
                for (tag, &(o, l)) in &self.tables {
                    raw.push((*tag, &self.data[o..o + l]));
                }
                raw.sort_by(|a, b| a.0.cmp(&b.0));
                Ok((t.subset(used, &raw)?, ProgramKind::TrueType))
            }
        }
    }

    /// Characters mapped by the cmap (for diagnostics/tests).
    pub fn cmap_len(&self) -> usize {
        self.cmap.len()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn stix_math() -> Font {
        Font::parse(include_bytes!("../../assets/fonts/STIXTwoMath-Regular.otf").to_vec()).unwrap()
    }

    #[test]
    fn parses_stix_math() {
        let f = stix_math();
        assert_eq!(f.units_per_em, 1000.0);
        assert!(f.is_cff());
        let m = f.math.as_ref().expect("MATH table");
        assert_eq!(m.constants.script_percent_scale_down, 70);
        assert!(m.constants.axis_height > 0);
        let paren = f.glyph('(').unwrap();
        assert!(
            m.vertical.contains_key(&paren),
            "parenthesis must be stretchable"
        );
        let bb = f.glyph_bbox(f.glyph('x').unwrap());
        assert!(bb.y_max > 300.0 && bb.y_max < 600.0, "x height bbox {bb:?}");
    }

    #[test]
    fn text_font_has_ligatures_and_kerning() {
        let f = Font::parse(include_bytes!("../../assets/fonts/STIXTwoText-Regular.otf").to_vec())
            .unwrap();
        let (fi, i) = (f.glyph('f').unwrap(), f.glyph('i').unwrap());
        let mut glyphs = vec![fi, i];
        let mut clusters = vec![1, 1];
        f.gsub.apply(b"liga", &mut glyphs, &mut clusters);
        // STIX Two joins f+i with contextual alternates (f._ij + i.f_), keeping
        // one glyph per character, which is ideal for copy/paste.
        assert_eq!(glyphs.len(), 2);
        assert_ne!(glyphs, vec![fi, i], "contextual f-i join applied");
        assert_eq!(clusters, vec![1, 1]);
        let (a, v) = (f.glyph('A').unwrap(), f.glyph('V').unwrap());
        assert!(f.kern(a, v) < 0.0, "AV kerning");
    }

    #[test]
    fn cff_subset_reparses() {
        let f = stix_math();
        let used: BTreeSet<u16> = ['x', 'y', '(', ')', '∑']
            .iter()
            .filter_map(|&c| f.glyph(c))
            .collect();
        let (prog, kind) = f.subset_program(&used, "ABCDEF+STIXTwoMath").unwrap();
        assert_eq!(kind, ProgramKind::CidCff);
        let sub = Cff::parse(&prog).unwrap();
        assert!(sub.is_cid_keyed());
        assert_eq!(sub.num_glyphs(), f.num_glyphs);
        let x = f.glyph('x').unwrap();
        let orig = f.glyph_bbox(x);
        assert_eq!(sub.glyph_bbox(x), orig);
        assert!(prog.len() < 200_000, "subset size {}", prog.len());
    }
}
