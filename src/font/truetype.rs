//! TrueType (`glyf`/`loca`) outlines: bounding boxes and subsetting.
//!
//! Subsetting keeps glyph ids: unused glyphs get empty `loca` ranges, and the
//! font program is rebuilt with only the tables a PDF consumer needs.

use super::cff::BBox;
use super::reader::{err, push_u16, push_u32, FontResult, Reader};
use std::collections::BTreeSet;

#[derive(Clone, Debug)]
pub struct TrueType {
    /// Glyph data ranges inside the `glyf` table (relative to its start).
    pub loca: Vec<(usize, usize)>,
    pub glyf: Vec<u8>,
}

impl TrueType {
    pub fn parse(
        loca: &[u8],
        glyf: &[u8],
        num_glyphs: usize,
        long_format: bool,
    ) -> FontResult<TrueType> {
        let mut r = Reader::new(loca);
        let mut offsets = Vec::with_capacity(num_glyphs + 1);
        for _ in 0..=num_glyphs {
            let off = if long_format {
                r.u32()? as usize
            } else {
                r.u16()? as usize * 2
            };
            offsets.push(off.min(glyf.len()));
        }
        let loca = offsets.windows(2).map(|w| (w[0], w[1].max(w[0]))).collect();
        Ok(TrueType {
            loca,
            glyf: glyf.to_vec(),
        })
    }

    pub fn glyph_bbox(&self, gid: u16) -> BBox {
        let Some(&(a, b)) = self.loca.get(gid as usize) else {
            return BBox::empty();
        };
        if b - a < 10 {
            return BBox::empty();
        }
        let mut r = Reader::new(&self.glyf[a..b]);
        let read = || -> FontResult<BBox> {
            let mut r2 = r;
            let _n = r2.i16()?;
            Ok(BBox {
                x_min: r2.i16()? as f64,
                y_min: r2.i16()? as f64,
                x_max: r2.i16()? as f64,
                y_max: r2.i16()? as f64,
            })
        };
        let bb = read().unwrap_or_default();
        let _ = r.skip(0);
        bb
    }

    /// Component glyph ids of a composite glyph.
    fn components(&self, gid: u16) -> Vec<u16> {
        let mut out = Vec::new();
        let Some(&(a, b)) = self.loca.get(gid as usize) else {
            return out;
        };
        if b - a < 10 {
            return out;
        }
        let data = &self.glyf[a..b];
        let mut r = Reader::new(data);
        let Ok(n) = r.i16() else { return out };
        if n >= 0 {
            return out;
        }
        if r.skip(8).is_err() {
            return out;
        }
        loop {
            let (Ok(flags), Ok(glyph)) = (r.u16(), r.u16()) else {
                break;
            };
            out.push(glyph);
            let mut skip = if flags & 0x0001 != 0 { 4 } else { 2 };
            if flags & 0x0008 != 0 {
                skip += 2;
            } else if flags & 0x0040 != 0 {
                skip += 4;
            } else if flags & 0x0080 != 0 {
                skip += 8;
            }
            if r.skip(skip).is_err() || flags & 0x0020 == 0 {
                break;
            }
        }
        out
    }

    /// Close a glyph set over composite references.
    pub fn closure(&self, used: &BTreeSet<u16>) -> BTreeSet<u16> {
        let mut keep: BTreeSet<u16> = used.clone();
        keep.insert(0);
        let mut stack: Vec<u16> = keep.iter().copied().collect();
        while let Some(g) = stack.pop() {
            for c in self.components(g) {
                if (c as usize) < self.loca.len() && keep.insert(c) {
                    stack.push(c);
                }
            }
        }
        keep
    }

    /// Build a subset sfnt. `tables` provides the original raw tables by tag.
    pub fn subset(&self, used: &BTreeSet<u16>, tables: &[([u8; 4], &[u8])]) -> FontResult<Vec<u8>> {
        let keep = self.closure(used);
        let mut glyf = Vec::new();
        let mut loca = Vec::with_capacity((self.loca.len() + 1) * 4);
        for (g, &(a, b)) in self.loca.iter().enumerate() {
            push_u32(&mut loca, glyf.len() as u32);
            if keep.contains(&(g as u16)) {
                glyf.extend_from_slice(&self.glyf[a..b]);
                while glyf.len() % 4 != 0 {
                    glyf.push(0);
                }
            }
        }
        push_u32(&mut loca, glyf.len() as u32);

        let mut out_tables: Vec<([u8; 4], Vec<u8>)> = Vec::new();
        for &(tag, data) in tables {
            match &tag {
                b"head" => {
                    if data.len() < 54 {
                        return err("head table too short");
                    }
                    let mut h = data.to_vec();
                    h[8..12].copy_from_slice(&[0, 0, 0, 0]); // checkSumAdjustment
                    h[50..52].copy_from_slice(&1u16.to_be_bytes()); // long loca
                    out_tables.push((tag, h));
                }
                b"hhea" | b"hmtx" | b"maxp" | b"cvt " | b"fpgm" | b"prep" | b"OS/2" | b"name"
                | b"post" => {
                    out_tables.push((tag, data.to_vec()));
                }
                _ => {}
            }
        }
        out_tables.push((*b"loca", loca));
        out_tables.push((*b"glyf", glyf));
        out_tables.sort_by(|a, b| a.0.cmp(&b.0));
        let mut font = write_sfnt(0x0001_0000, &out_tables);
        // checkSumAdjustment
        let total = checksum(&font);
        let adj = 0xB1B0_AFBAu32.wrapping_sub(total);
        if let Some(pos) = find_table_offset(&font, b"head") {
            font[pos + 8..pos + 12].copy_from_slice(&adj.to_be_bytes());
        }
        Ok(font)
    }
}

pub fn checksum(data: &[u8]) -> u32 {
    let mut sum = 0u32;
    for chunk in data.chunks(4) {
        let mut b = [0u8; 4];
        b[..chunk.len()].copy_from_slice(chunk);
        sum = sum.wrapping_add(u32::from_be_bytes(b));
    }
    sum
}

fn find_table_offset(font: &[u8], tag: &[u8; 4]) -> Option<usize> {
    let n = u16::from_be_bytes([font[4], font[5]]) as usize;
    for i in 0..n {
        let rec = 12 + i * 16;
        if &font[rec..rec + 4] == tag {
            return Some(u32::from_be_bytes([
                font[rec + 8],
                font[rec + 9],
                font[rec + 10],
                font[rec + 11],
            ]) as usize);
        }
    }
    None
}

/// Serialise an sfnt container from (tag, data) pairs (already sorted by tag).
pub fn write_sfnt(version: u32, tables: &[([u8; 4], Vec<u8>)]) -> Vec<u8> {
    let n = tables.len() as u16;
    let mut es = 0u16;
    while (1u16 << (es + 1)) <= n {
        es += 1;
    }
    let search_range = (1u16 << es) * 16;
    let mut out = Vec::new();
    push_u32(&mut out, version);
    push_u16(&mut out, n);
    push_u16(&mut out, search_range);
    push_u16(&mut out, es);
    push_u16(&mut out, n * 16 - search_range);
    let mut offset = 12 + 16 * tables.len();
    let mut body = Vec::new();
    for (tag, data) in tables {
        out.extend_from_slice(tag);
        push_u32(&mut out, checksum(data));
        push_u32(&mut out, offset as u32);
        push_u32(&mut out, data.len() as u32);
        body.extend_from_slice(data);
        while body.len() % 4 != 0 {
            body.push(0);
        }
        offset = 12 + 16 * tables.len() + body.len();
    }
    out.extend_from_slice(&body);
    out
}
