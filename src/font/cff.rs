//! Compact Font Format (CFF, version 1) support.
//!
//! * parses INDEX/DICT structures of name-keyed and CID-keyed fonts,
//! * interprets Type 2 charstrings to compute exact glyph ink bounding boxes
//!   and to discover which subroutines a glyph uses,
//! * writes a CID-keyed subset (`CIDFontType0C` for PDF) in which glyph ids are
//!   preserved (CID == GID). Unused charstrings become a bare `endchar` and
//!   unused subroutines a bare `return`, so no renumbering is required.

use super::reader::{err, push_u16, FontResult, Reader};
use std::collections::BTreeSet;

/// An absolute byte range inside the CFF data.
type Range = (usize, usize);

/// One DICT entry, keeping the original encoding for verbatim copies.
#[derive(Clone, Debug)]
struct DictEntry {
    op: u16,
    operands: Vec<f64>,
    raw: Vec<u8>,
}

#[derive(Clone, Debug, Default)]
struct Dict {
    entries: Vec<DictEntry>,
}

impl Dict {
    fn parse(data: &[u8]) -> FontResult<Dict> {
        let mut entries = Vec::new();
        let mut operands = Vec::new();
        let mut start = 0usize;
        let mut i = 0usize;
        while i < data.len() {
            let b0 = data[i];
            match b0 {
                0..=21 => {
                    let op = if b0 == 12 {
                        i += 1;
                        if i >= data.len() {
                            return err("truncated DICT escape");
                        }
                        1200 + data[i] as u16
                    } else {
                        b0 as u16
                    };
                    i += 1;
                    entries.push(DictEntry {
                        op,
                        operands: std::mem::take(&mut operands),
                        raw: data[start..i].to_vec(),
                    });
                    start = i;
                }
                28 => {
                    if i + 2 >= data.len() {
                        return err("truncated DICT int");
                    }
                    operands.push(i16::from_be_bytes([data[i + 1], data[i + 2]]) as f64);
                    i += 3;
                }
                29 => {
                    if i + 4 >= data.len() {
                        return err("truncated DICT int");
                    }
                    operands.push(i32::from_be_bytes([
                        data[i + 1],
                        data[i + 2],
                        data[i + 3],
                        data[i + 4],
                    ]) as f64);
                    i += 5;
                }
                30 => {
                    let (v, len) = parse_real(&data[i + 1..])?;
                    operands.push(v);
                    i += 1 + len;
                }
                32..=246 => {
                    operands.push(b0 as f64 - 139.0);
                    i += 1;
                }
                247..=250 => {
                    let b1 = *data
                        .get(i + 1)
                        .ok_or_else(|| super::reader::FontError("truncated".into()))?;
                    operands.push((b0 as f64 - 247.0) * 256.0 + b1 as f64 + 108.0);
                    i += 2;
                }
                251..=254 => {
                    let b1 = *data
                        .get(i + 1)
                        .ok_or_else(|| super::reader::FontError("truncated".into()))?;
                    operands.push(-(b0 as f64 - 251.0) * 256.0 - b1 as f64 - 108.0);
                    i += 2;
                }
                _ => return err(format!("invalid DICT byte {b0}")),
            }
        }
        Ok(Dict { entries })
    }

    fn get(&self, op: u16) -> Option<&[f64]> {
        self.entries
            .iter()
            .find(|e| e.op == op)
            .map(|e| e.operands.as_slice())
    }

    fn get_int(&self, op: u16) -> Option<i64> {
        self.get(op).and_then(|v| v.first()).map(|v| *v as i64)
    }
}

fn parse_real(data: &[u8]) -> FontResult<(f64, usize)> {
    let mut s = String::new();
    let mut len = 0;
    'outer: for &b in data {
        len += 1;
        for nib in [b >> 4, b & 0xF] {
            match nib {
                0..=9 => s.push((b'0' + nib) as char),
                0xA => s.push('.'),
                0xB => s.push('E'),
                0xC => s.push_str("E-"),
                0xE => s.push('-'),
                0xF => break 'outer,
                _ => {}
            }
        }
    }
    let v = s.parse::<f64>().unwrap_or(0.0);
    Ok((v, len))
}

fn parse_index(data: &[u8], offset: usize) -> FontResult<(Vec<Range>, usize)> {
    let mut r = Reader::at(data, offset)?;
    let count = r.u16()? as usize;
    if count == 0 {
        return Ok((Vec::new(), offset + 2));
    }
    let off_size = r.u8()?;
    let mut offsets = Vec::with_capacity(count + 1);
    for _ in 0..=count {
        offsets.push(r.uint_n(off_size)? as usize);
    }
    let base = r.pos() - 1;
    let mut ranges = Vec::with_capacity(count);
    for w in offsets.windows(2) {
        let (a, b) = (base + w[0], base + w[1]);
        if b < a || b > data.len() {
            return err("corrupt CFF INDEX offsets");
        }
        ranges.push((a, b));
    }
    let end = base + offsets[count];
    Ok((ranges, end))
}

/// Private data of one (FD) font: private DICT and local subroutines.
#[derive(Clone, Debug)]
struct PrivateFont {
    private: Dict,
    local_subrs: Vec<Range>,
}

/// A parsed CFF font program.
#[derive(Clone, Debug)]
pub struct Cff {
    data: Vec<u8>,
    name: Vec<u8>,
    top: Dict,
    strings: Vec<Range>,
    global_subrs: Vec<Range>,
    charstrings: Vec<Range>,
    fonts: Vec<PrivateFont>,
    /// FD index per glyph (CID-keyed fonts only).
    fd_select: Vec<u8>,
    is_cid: bool,
}

/// Ink bounding box of a glyph in font units.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct BBox {
    pub x_min: f64,
    pub y_min: f64,
    pub x_max: f64,
    pub y_max: f64,
}

impl BBox {
    pub fn empty() -> Self {
        BBox {
            x_min: 0.0,
            y_min: 0.0,
            x_max: 0.0,
            y_max: 0.0,
        }
    }
}

const OP_CHARSTRINGS: u16 = 17;
const OP_PRIVATE: u16 = 18;
const OP_SUBRS: u16 = 19;
const OP_ROS: u16 = 1230;
const OP_FDARRAY: u16 = 1236;
const OP_FDSELECT: u16 = 1237;
const OP_FONTMATRIX: u16 = 1207;
const OP_FONTBBOX: u16 = 5;

impl Cff {
    pub fn parse(data: &[u8]) -> FontResult<Cff> {
        let data = data.to_vec();
        let mut r = Reader::new(&data);
        let major = r.u8()?;
        if major != 1 {
            return err(format!("unsupported CFF major version {major}"));
        }
        let _minor = r.u8()?;
        let hdr_size = r.u8()? as usize;
        let (names, after_names) = parse_index(&data, hdr_size)?;
        let (tops, after_tops) = parse_index(&data, after_names)?;
        let (strings, after_strings) = parse_index(&data, after_tops)?;
        let (global_subrs, _) = parse_index(&data, after_strings)?;
        let name = names
            .first()
            .map(|&(a, b)| data[a..b].to_vec())
            .unwrap_or_default();
        let top_range = *tops
            .first()
            .ok_or_else(|| super::reader::FontError("CFF without Top DICT".into()))?;
        let top = Dict::parse(&data[top_range.0..top_range.1])?;
        let cs_off = top
            .get_int(OP_CHARSTRINGS)
            .ok_or_else(|| super::reader::FontError("CFF without CharStrings".into()))?
            as usize;
        let (charstrings, _) = parse_index(&data, cs_off)?;
        let is_cid = top.get(OP_ROS).is_some();
        let mut fonts = Vec::new();
        let mut fd_select = Vec::new();
        if is_cid {
            let fda = top
                .get_int(OP_FDARRAY)
                .ok_or_else(|| super::reader::FontError("CID CFF without FDArray".into()))?
                as usize;
            let (fd_dicts, _) = parse_index(&data, fda)?;
            for &(a, b) in &fd_dicts {
                let fd = Dict::parse(&data[a..b])?;
                fonts.push(Self::read_private(&data, &fd)?);
            }
            let fds = top
                .get_int(OP_FDSELECT)
                .ok_or_else(|| super::reader::FontError("CID CFF without FDSelect".into()))?
                as usize;
            fd_select = Self::read_fd_select(&data, fds, charstrings.len())?;
        } else {
            fonts.push(Self::read_private(&data, &top)?);
        }
        Ok(Cff {
            data,
            name,
            top,
            strings,
            global_subrs,
            charstrings,
            fonts,
            fd_select,
            is_cid,
        })
    }

    fn read_private(data: &[u8], dict: &Dict) -> FontResult<PrivateFont> {
        if let Some(p) = dict.get(OP_PRIVATE) {
            if p.len() >= 2 {
                let (size, off) = (p[0] as usize, p[1] as usize);
                if off + size > data.len() {
                    return err("Private DICT out of range");
                }
                let private = Dict::parse(&data[off..off + size])?;
                let local_subrs = match private.get_int(OP_SUBRS) {
                    Some(rel) => parse_index(data, off + rel as usize)?.0,
                    None => Vec::new(),
                };
                return Ok(PrivateFont {
                    private,
                    local_subrs,
                });
            }
        }
        Ok(PrivateFont {
            private: Dict::default(),
            local_subrs: Vec::new(),
        })
    }

    fn read_fd_select(data: &[u8], off: usize, n_glyphs: usize) -> FontResult<Vec<u8>> {
        let mut r = Reader::at(data, off)?;
        let format = r.u8()?;
        let mut out = vec![0u8; n_glyphs];
        match format {
            0 => {
                for slot in out.iter_mut() {
                    *slot = r.u8()?;
                }
            }
            3 => {
                let n = r.u16()? as usize;
                let mut first = r.u16()? as usize;
                for _ in 0..n {
                    let fd = r.u8()?;
                    let next = r.u16()? as usize;
                    for g in first..next.min(n_glyphs) {
                        out[g] = fd;
                    }
                    first = next;
                }
            }
            _ => return err(format!("unsupported FDSelect format {format}")),
        }
        Ok(out)
    }

    pub fn num_glyphs(&self) -> usize {
        self.charstrings.len()
    }

    pub fn is_cid_keyed(&self) -> bool {
        self.is_cid
    }

    pub fn font_name(&self) -> String {
        String::from_utf8_lossy(&self.name).into_owned()
    }

    /// FontMatrix scale (units per em equivalent), defaulting to 1000.
    pub fn units_per_em(&self) -> f64 {
        match self.top.get(OP_FONTMATRIX) {
            Some(m) if !m.is_empty() && m[0] != 0.0 => 1.0 / m[0],
            _ => 1000.0,
        }
    }

    fn font_for(&self, gid: u16) -> &PrivateFont {
        let fd = if self.is_cid {
            *self.fd_select.get(gid as usize).unwrap_or(&0) as usize
        } else {
            0
        };
        self.fonts.get(fd).unwrap_or(&self.fonts[0])
    }

    fn fd_index(&self, gid: u16) -> usize {
        if self.is_cid {
            *self.fd_select.get(gid as usize).unwrap_or(&0) as usize
        } else {
            0
        }
    }

    /// Interpret a glyph's charstring and return its ink bounding box.
    pub fn glyph_bbox(&self, gid: u16) -> BBox {
        let mut interp = Interp::new(self, gid);
        match interp.run_glyph() {
            Ok(()) => interp.bbox(),
            Err(_) => BBox::empty(),
        }
    }

    /// Write a CID-keyed CFF that keeps glyph ids, containing only `used`
    /// glyphs (others replaced by `endchar`).
    pub fn subset(&self, used: &BTreeSet<u16>, font_name: &str) -> FontResult<Vec<u8>> {
        let n = self.charstrings.len();
        let mut keep: BTreeSet<u16> = used.iter().copied().filter(|&g| (g as usize) < n).collect();
        keep.insert(0);

        // Discover subroutine usage.
        let mut used_gsubrs = BTreeSet::new();
        let mut used_lsubrs: Vec<BTreeSet<usize>> = vec![BTreeSet::new(); self.fonts.len()];
        for &g in &keep {
            let mut interp = Interp::new(self, g);
            interp.track_subrs = true;
            let _ = interp.run_glyph();
            used_gsubrs.extend(interp.used_gsubrs.iter().copied());
            let fd = self.fd_index(g);
            used_lsubrs[fd].extend(interp.used_lsubrs.iter().copied());
        }

        // Build pieces.
        let charstrings: Vec<Vec<u8>> = (0..n)
            .map(|g| {
                if keep.contains(&(g as u16)) {
                    let (a, b) = self.charstrings[g];
                    self.data[a..b].to_vec()
                } else {
                    vec![14] // endchar
                }
            })
            .collect();
        let gsubrs: Vec<Vec<u8>> = self
            .global_subrs
            .iter()
            .enumerate()
            .map(|(i, &(a, b))| {
                if used_gsubrs.contains(&i) {
                    self.data[a..b].to_vec()
                } else {
                    vec![11]
                }
            })
            .collect();

        let strings: Vec<Vec<u8>> = vec![b"Adobe".to_vec(), b"Identity".to_vec()];
        let name_index = write_index(&[font_name.as_bytes().to_vec()]);
        let string_index = write_index(&strings);
        let gsubr_index = write_index(&gsubrs);
        let charset = {
            let mut c = vec![2u8];
            if n > 1 {
                push_u16(&mut c, 1);
                push_u16(&mut c, (n - 2) as u16);
            }
            c
        };
        let fd_select = {
            let mut ranges: Vec<(u16, u8)> = Vec::new();
            for g in 0..n {
                let fd = self.fd_index(g as u16) as u8;
                if ranges.last().map(|r| r.1) != Some(fd) {
                    ranges.push((g as u16, fd));
                }
            }
            let mut f = vec![3u8];
            push_u16(&mut f, ranges.len() as u16);
            for (first, fd) in &ranges {
                push_u16(&mut f, *first);
                f.push(*fd);
            }
            push_u16(&mut f, n as u16);
            f
        };
        let cs_index = write_index(&charstrings);

        // Private dicts with rewritten Subrs offsets + zeroed local subrs.
        let mut privates: Vec<(Vec<u8>, Vec<u8>)> = Vec::new();
        for (fd, font) in self.fonts.iter().enumerate() {
            let has_subrs = !font.local_subrs.is_empty();
            let mut body = Vec::new();
            for e in &font.private.entries {
                if e.op != OP_SUBRS {
                    body.extend_from_slice(&e.raw);
                }
            }
            let subrs_bytes = if has_subrs {
                let subrs: Vec<Vec<u8>> = font
                    .local_subrs
                    .iter()
                    .enumerate()
                    .map(|(i, &(a, b))| {
                        if used_lsubrs[fd].contains(&i) {
                            self.data[a..b].to_vec()
                        } else {
                            vec![11]
                        }
                    })
                    .collect();
                write_index(&subrs)
            } else {
                Vec::new()
            };
            if has_subrs {
                let size = body.len() + 6; // 5-byte int + op
                push_int5(&mut body, size as i32);
                body.push(OP_SUBRS as u8);
            }
            privates.push((body, subrs_bytes));
        }

        // Top DICT with fixed-size offsets.
        let top_len = {
            let probe = self.build_top_dict(0, 0, 0, 0, n);
            probe.len()
        };
        let top_index_len = index_len(&[top_len]);
        let header_len = 4;
        let mut pos =
            header_len + name_index.len() + top_index_len + string_index.len() + gsubr_index.len();
        let charset_off = pos;
        pos += charset.len();
        let fdselect_off = pos;
        pos += fd_select.len();
        let cs_off = pos;
        pos += cs_index.len();
        let fdarray_off = pos;
        // FD font dicts: Private [size offset] with 5-byte ints.
        let fd_dict_len = 11; // 5 + 5 + 1
        let fd_index_len = index_len(&vec![fd_dict_len; privates.len()]);
        pos += fd_index_len;
        let mut fd_dicts = Vec::new();
        let mut private_blobs = Vec::new();
        for (body, subrs) in &privates {
            let mut d = Vec::new();
            push_int5(&mut d, body.len() as i32);
            push_int5(&mut d, pos as i32);
            d.push(OP_PRIVATE as u8);
            fd_dicts.push(d);
            private_blobs.extend_from_slice(body);
            private_blobs.extend_from_slice(subrs);
            pos += body.len() + subrs.len();
        }
        let fd_index = write_index(&fd_dicts);
        debug_assert_eq!(fd_index.len(), fd_index_len);
        let top = self.build_top_dict(charset_off, fdselect_off, cs_off, fdarray_off, n);
        debug_assert_eq!(top.len(), top_len);
        let top_index = write_index(&[top]);

        let mut out = vec![1u8, 0, 4, 4];
        out.extend_from_slice(&name_index);
        out.extend_from_slice(&top_index);
        out.extend_from_slice(&string_index);
        out.extend_from_slice(&gsubr_index);
        out.extend_from_slice(&charset);
        out.extend_from_slice(&fd_select);
        out.extend_from_slice(&cs_index);
        out.extend_from_slice(&fd_index);
        out.extend_from_slice(&private_blobs);
        Ok(out)
    }

    fn build_top_dict(
        &self,
        charset: usize,
        fdselect: usize,
        cs: usize,
        fdarray: usize,
        n: usize,
    ) -> Vec<u8> {
        let mut d = Vec::new();
        // ROS must come first: Adobe (SID 391), Identity (SID 392), supplement 0.
        push_int5(&mut d, 391);
        push_int5(&mut d, 392);
        push_int5(&mut d, 0);
        d.extend_from_slice(&[12, 30]);
        // Copy FontMatrix and FontBBox verbatim when present.
        for e in &self.top.entries {
            if e.op == OP_FONTMATRIX || e.op == OP_FONTBBOX {
                d.extend_from_slice(&e.raw);
            }
        }
        push_int5(&mut d, n as i32);
        d.extend_from_slice(&[12, 34]); // CIDCount
        push_int5(&mut d, charset as i32);
        d.push(15);
        push_int5(&mut d, cs as i32);
        d.push(17);
        push_int5(&mut d, fdarray as i32);
        d.extend_from_slice(&[12, 36]);
        push_int5(&mut d, fdselect as i32);
        d.extend_from_slice(&[12, 37]);
        let _ = &self.strings;
        d
    }
}

fn push_int5(out: &mut Vec<u8>, v: i32) {
    out.push(29);
    out.extend_from_slice(&v.to_be_bytes());
}

fn off_size_for(max: usize) -> u8 {
    if max < 0x100 {
        1
    } else if max < 0x10000 {
        2
    } else if max < 0x1000000 {
        3
    } else {
        4
    }
}

fn index_len(item_lens: &[usize]) -> usize {
    if item_lens.is_empty() {
        return 2;
    }
    let total: usize = item_lens.iter().sum();
    let os = off_size_for(total + 1) as usize;
    2 + 1 + os * (item_lens.len() + 1) + total
}

fn write_index(items: &[Vec<u8>]) -> Vec<u8> {
    let mut out = Vec::new();
    push_u16(&mut out, items.len() as u16);
    if items.is_empty() {
        return out;
    }
    let total: usize = items.iter().map(|i| i.len()).sum();
    let os = off_size_for(total + 1);
    out.push(os);
    let mut off = 1usize;
    let write_off = |out: &mut Vec<u8>, v: usize| {
        let b = (v as u32).to_be_bytes();
        out.extend_from_slice(&b[4 - os as usize..]);
    };
    write_off(&mut out, off);
    for it in items {
        off += it.len();
        write_off(&mut out, off);
    }
    for it in items {
        out.extend_from_slice(it);
    }
    out
}

fn subr_bias(count: usize) -> i64 {
    if count < 1240 {
        107
    } else if count < 33900 {
        1131
    } else {
        32768
    }
}

/// Type 2 charstring interpreter (bounding boxes and subroutine tracking).
struct Interp<'a> {
    cff: &'a Cff,
    font: &'a PrivateFont,
    stack: Vec<f64>,
    x: f64,
    y: f64,
    n_stems: usize,
    width_done: bool,
    min_x: f64,
    min_y: f64,
    max_x: f64,
    max_y: f64,
    has_points: bool,
    depth: usize,
    track_subrs: bool,
    used_gsubrs: BTreeSet<usize>,
    used_lsubrs: BTreeSet<usize>,
    gid: u16,
    ended: bool,
}

impl<'a> Interp<'a> {
    fn new(cff: &'a Cff, gid: u16) -> Self {
        Interp {
            cff,
            font: cff.font_for(gid),
            stack: Vec::with_capacity(48),
            x: 0.0,
            y: 0.0,
            n_stems: 0,
            width_done: false,
            min_x: f64::MAX,
            min_y: f64::MAX,
            max_x: f64::MIN,
            max_y: f64::MIN,
            has_points: false,
            depth: 0,
            track_subrs: false,
            used_gsubrs: BTreeSet::new(),
            used_lsubrs: BTreeSet::new(),
            gid,
            ended: false,
        }
    }

    fn bbox(&self) -> BBox {
        if !self.has_points {
            return BBox::empty();
        }
        BBox {
            x_min: self.min_x,
            y_min: self.min_y,
            x_max: self.max_x,
            y_max: self.max_y,
        }
    }

    fn point(&mut self, x: f64, y: f64) {
        self.has_points = true;
        self.min_x = self.min_x.min(x);
        self.max_x = self.max_x.max(x);
        self.min_y = self.min_y.min(y);
        self.max_y = self.max_y.max(y);
    }

    fn line_to(&mut self, dx: f64, dy: f64) {
        self.x += dx;
        self.y += dy;
        self.point(self.x, self.y);
    }

    fn curve_to(&mut self, d: [f64; 6]) {
        let p0 = (self.x, self.y);
        let p1 = (p0.0 + d[0], p0.1 + d[1]);
        let p2 = (p1.0 + d[2], p1.1 + d[3]);
        let p3 = (p2.0 + d[4], p2.1 + d[5]);
        self.point(p3.0, p3.1);
        for axis in 0..2 {
            let (a, b, c, e) = if axis == 0 {
                (p0.0, p1.0, p2.0, p3.0)
            } else {
                (p0.1, p1.1, p2.1, p3.1)
            };
            // derivative coefficients of cubic bezier
            let qa = -a + 3.0 * b - 3.0 * c + e;
            let qb = 2.0 * (a - 2.0 * b + c);
            let qc = b - a;
            let mut roots = Vec::new();
            if qa.abs() < 1e-12 {
                if qb.abs() > 1e-12 {
                    roots.push(-qc / qb);
                }
            } else {
                let disc = qb * qb - 4.0 * qa * qc;
                if disc >= 0.0 {
                    let s = disc.sqrt();
                    roots.push((-qb + s) / (2.0 * qa));
                    roots.push((-qb - s) / (2.0 * qa));
                }
            }
            for t in roots {
                if t > 0.0 && t < 1.0 {
                    let mt = 1.0 - t;
                    let x = mt * mt * mt * p0.0
                        + 3.0 * mt * mt * t * p1.0
                        + 3.0 * mt * t * t * p2.0
                        + t * t * t * p3.0;
                    let y = mt * mt * mt * p0.1
                        + 3.0 * mt * mt * t * p1.1
                        + 3.0 * mt * t * t * p2.1
                        + t * t * t * p3.1;
                    self.point(x, y);
                }
            }
        }
        self.x = p3.0;
        self.y = p3.1;
    }

    fn move_to(&mut self, dx: f64, dy: f64) {
        self.x += dx;
        self.y += dy;
    }

    fn take_width(&mut self, expected_even: bool) {
        if !self.width_done {
            self.width_done = true;
            let odd = self.stack.len() % 2 == 1;
            if odd == expected_even && !self.stack.is_empty() {
                self.stack.remove(0);
            }
        }
    }

    fn run_glyph(&mut self) -> FontResult<()> {
        let (a, b) = *self
            .cff
            .charstrings
            .get(self.gid as usize)
            .ok_or_else(|| super::reader::FontError("gid out of range".into()))?;
        let data = &self.cff.data[a..b];
        self.exec(data)
    }

    fn exec(&mut self, code: &[u8]) -> FontResult<()> {
        if self.depth > 10 {
            return err("charstring subroutine nesting too deep");
        }
        let mut i = 0usize;
        while i < code.len() && !self.ended {
            let b0 = code[i];
            match b0 {
                32..=246 => {
                    self.stack.push(b0 as f64 - 139.0);
                    i += 1;
                }
                247..=250 => {
                    let b1 = *code.get(i + 1).unwrap_or(&0) as f64;
                    self.stack.push((b0 as f64 - 247.0) * 256.0 + b1 + 108.0);
                    i += 2;
                }
                251..=254 => {
                    let b1 = *code.get(i + 1).unwrap_or(&0) as f64;
                    self.stack.push(-(b0 as f64 - 251.0) * 256.0 - b1 - 108.0);
                    i += 2;
                }
                28 => {
                    if i + 2 >= code.len() {
                        return err("truncated shortint");
                    }
                    self.stack
                        .push(i16::from_be_bytes([code[i + 1], code[i + 2]]) as f64);
                    i += 3;
                }
                255 => {
                    if i + 4 >= code.len() {
                        return err("truncated fixed");
                    }
                    let v =
                        i32::from_be_bytes([code[i + 1], code[i + 2], code[i + 3], code[i + 4]]);
                    self.stack.push(v as f64 / 65536.0);
                    i += 5;
                }
                1 | 3 | 18 | 23 => {
                    // hstem, vstem, hstemhm, vstemhm
                    self.take_width(true);
                    self.n_stems += self.stack.len() / 2;
                    self.stack.clear();
                    i += 1;
                }
                19 | 20 => {
                    // hintmask, cntrmask (implicit vstem)
                    self.take_width(true);
                    self.n_stems += self.stack.len() / 2;
                    self.stack.clear();
                    i += 1 + self.n_stems.div_ceil(8);
                }
                21 => {
                    self.take_width(true);
                    let dy = self.stack.pop().unwrap_or(0.0);
                    let dx = self.stack.pop().unwrap_or(0.0);
                    self.move_to(dx, dy);
                    self.stack.clear();
                    i += 1;
                }
                22 => {
                    self.take_width(false);
                    let dx = self.stack.pop().unwrap_or(0.0);
                    self.move_to(dx, 0.0);
                    self.stack.clear();
                    i += 1;
                }
                4 => {
                    self.take_width(false);
                    let dy = self.stack.pop().unwrap_or(0.0);
                    self.move_to(0.0, dy);
                    self.stack.clear();
                    i += 1;
                }
                5 => {
                    let s = std::mem::take(&mut self.stack);
                    for c in s.chunks(2) {
                        if c.len() == 2 {
                            self.line_to(c[0], c[1]);
                        }
                    }
                    i += 1;
                }
                6 | 7 => {
                    let s = std::mem::take(&mut self.stack);
                    let mut horiz = b0 == 6;
                    for &v in &s {
                        if horiz {
                            self.line_to(v, 0.0);
                        } else {
                            self.line_to(0.0, v);
                        }
                        horiz = !horiz;
                    }
                    i += 1;
                }
                8 => {
                    let s = std::mem::take(&mut self.stack);
                    for c in s.chunks(6) {
                        if c.len() == 6 {
                            self.curve_to([c[0], c[1], c[2], c[3], c[4], c[5]]);
                        }
                    }
                    i += 1;
                }
                24 => {
                    // rcurveline
                    let s = std::mem::take(&mut self.stack);
                    let n = s.len();
                    let curves = (n.saturating_sub(2)) / 6;
                    for k in 0..curves {
                        let c = &s[k * 6..k * 6 + 6];
                        self.curve_to([c[0], c[1], c[2], c[3], c[4], c[5]]);
                    }
                    if n >= 2 {
                        self.line_to(s[n - 2], s[n - 1]);
                    }
                    i += 1;
                }
                25 => {
                    // rlinecurve
                    let s = std::mem::take(&mut self.stack);
                    let n = s.len();
                    if n >= 6 {
                        let lines = (n - 6) / 2;
                        for k in 0..lines {
                            self.line_to(s[2 * k], s[2 * k + 1]);
                        }
                        let c = &s[n - 6..];
                        self.curve_to([c[0], c[1], c[2], c[3], c[4], c[5]]);
                    }
                    i += 1;
                }
                26 => {
                    // vvcurveto
                    let mut s = std::mem::take(&mut self.stack);
                    let mut dx1 = 0.0;
                    if s.len() % 4 == 1 {
                        dx1 = s.remove(0);
                    }
                    for c in s.chunks(4) {
                        if c.len() == 4 {
                            self.curve_to([dx1, c[0], c[1], c[2], 0.0, c[3]]);
                            dx1 = 0.0;
                        }
                    }
                    i += 1;
                }
                27 => {
                    // hhcurveto
                    let mut s = std::mem::take(&mut self.stack);
                    let mut dy1 = 0.0;
                    if s.len() % 4 == 1 {
                        dy1 = s.remove(0);
                    }
                    for c in s.chunks(4) {
                        if c.len() == 4 {
                            self.curve_to([c[0], dy1, c[1], c[2], c[3], 0.0]);
                            dy1 = 0.0;
                        }
                    }
                    i += 1;
                }
                30 | 31 => {
                    // vhcurveto / hvcurveto
                    let s = std::mem::take(&mut self.stack);
                    let mut horiz = b0 == 31;
                    let mut k = 0;
                    while k + 4 <= s.len() {
                        let last = k + 4 == s.len() - 1;
                        let extra = if last { s[k + 4] } else { 0.0 };
                        if horiz {
                            self.curve_to([s[k], 0.0, s[k + 1], s[k + 2], extra, s[k + 3]]);
                        } else {
                            self.curve_to([0.0, s[k], s[k + 1], s[k + 2], s[k + 3], extra]);
                        }
                        horiz = !horiz;
                        k += 4;
                        if last {
                            break;
                        }
                    }
                    i += 1;
                }
                10 | 29 => {
                    let idx = self.stack.pop().unwrap_or(0.0) as i64;
                    let (subrs, global) = if b0 == 10 {
                        (&self.font.local_subrs, false)
                    } else {
                        (&self.cff.global_subrs, true)
                    };
                    let real = idx + subr_bias(subrs.len());
                    if real < 0 || real as usize >= subrs.len() {
                        return err("subroutine index out of range");
                    }
                    let real = real as usize;
                    if self.track_subrs {
                        if global {
                            self.used_gsubrs.insert(real);
                        } else {
                            self.used_lsubrs.insert(real);
                        }
                    }
                    let (a, b) = subrs[real];
                    let cff = self.cff;
                    let code2 = &cff.data[a..b];
                    self.depth += 1;
                    self.exec(code2)?;
                    self.depth -= 1;
                    i += 1;
                }
                11 => return Ok(()),
                14 => {
                    // endchar (optionally with width / deprecated seac args)
                    if !self.width_done {
                        self.width_done = true;
                        if self.stack.len() == 1 || self.stack.len() == 5 {
                            self.stack.remove(0);
                        }
                    }
                    self.ended = true;
                    return Ok(());
                }
                12 => {
                    let b1 = *code.get(i + 1).unwrap_or(&0);
                    let s = std::mem::take(&mut self.stack);
                    match b1 {
                        35 if s.len() >= 12 => {
                            self.curve_to([s[0], s[1], s[2], s[3], s[4], s[5]]);
                            self.curve_to([s[6], s[7], s[8], s[9], s[10], s[11]]);
                        }
                        34 if s.len() >= 7 => {
                            // hflex
                            self.curve_to([s[0], 0.0, s[1], s[2], s[3], 0.0]);
                            self.curve_to([s[4], 0.0, s[5], -s[2], s[6], 0.0]);
                        }
                        36 if s.len() >= 9 => {
                            // hflex1
                            self.curve_to([s[0], s[1], s[2], s[3], s[4], 0.0]);
                            self.curve_to([s[5], 0.0, s[6], s[7], s[8], -(s[1] + s[3] + s[7])]);
                        }
                        37 if s.len() >= 11 => {
                            // flex1
                            let dx = s[0] + s[2] + s[4] + s[6] + s[8];
                            let dy = s[1] + s[3] + s[5] + s[7] + s[9];
                            self.curve_to([s[0], s[1], s[2], s[3], s[4], s[5]]);
                            if dx.abs() > dy.abs() {
                                self.curve_to([s[6], s[7], s[8], s[9], s[10], -dy]);
                            } else {
                                self.curve_to([s[6], s[7], s[8], s[9], -dx, s[10]]);
                            }
                        }
                        _ => {}
                    }
                    i += 2;
                }
                _ => {
                    // reserved operators: ignore and clear
                    self.stack.clear();
                    i += 1;
                }
            }
            if self.stack.len() > 96 {
                return err("charstring stack overflow");
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn index_roundtrip() {
        let items = vec![b"abc".to_vec(), Vec::new(), b"defgh".to_vec()];
        let bytes = write_index(&items);
        assert_eq!(bytes.len(), index_len(&[3, 0, 5]));
        let (ranges, end) = parse_index(&bytes, 0).unwrap();
        assert_eq!(end, bytes.len());
        assert_eq!(&bytes[ranges[0].0..ranges[0].1], b"abc");
        assert_eq!(ranges[1].0, ranges[1].1);
        assert_eq!(&bytes[ranges[2].0..ranges[2].1], b"defgh");
    }

    #[test]
    fn dict_parses_numbers_and_reals() {
        // 100 (0xEF), -100 (0x27), real -2.25 (1e 0e 2a 25 ff? encoding: -2.25 => e2 a2 5f)
        let data = [0xEF, 0x27, 30, 0xE2, 0xA2, 0x5F, 12, 7];
        let d = Dict::parse(&data).unwrap();
        let v = d.get(1207).unwrap();
        assert_eq!(v, &[100.0, -100.0, -2.25]);
    }
}
