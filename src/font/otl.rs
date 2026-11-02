//! OpenType Layout: GSUB (single, alternate, ligature substitution) and GPOS
//! (pair kerning), with extension lookups resolved. Enough for high-quality
//! Latin text and math script variants (`ssty`).

use super::reader::{u16_at, FontResult, Reader};
use std::collections::HashMap;

/// Coverage table: glyph -> coverage index.
#[derive(Clone, Debug, Default)]
pub struct Coverage {
    map: HashMap<u16, u16>,
}

impl Coverage {
    pub fn parse(data: &[u8], off: usize) -> FontResult<Coverage> {
        let mut r = Reader::at(data, off)?;
        let format = r.u16()?;
        let mut map = HashMap::new();
        match format {
            1 => {
                let n = r.u16()?;
                for i in 0..n {
                    map.insert(r.u16()?, i);
                }
            }
            2 => {
                let n = r.u16()?;
                for _ in 0..n {
                    let start = r.u16()?;
                    let end = r.u16()?;
                    let idx = r.u16()?;
                    for g in start..=end {
                        map.insert(g, idx + (g - start));
                    }
                }
            }
            _ => {}
        }
        Ok(Coverage { map })
    }

    pub fn get(&self, g: u16) -> Option<u16> {
        self.map.get(&g).copied()
    }

    pub fn glyphs(&self) -> impl Iterator<Item = (u16, u16)> + '_ {
        self.map.iter().map(|(&g, &i)| (g, i))
    }
}

/// Class definition table.
#[derive(Clone, Debug, Default)]
pub struct ClassDef {
    map: HashMap<u16, u16>,
}

impl ClassDef {
    pub fn parse(data: &[u8], off: usize) -> FontResult<ClassDef> {
        let mut r = Reader::at(data, off)?;
        let format = r.u16()?;
        let mut map = HashMap::new();
        match format {
            1 => {
                let start = r.u16()?;
                let n = r.u16()?;
                for i in 0..n {
                    let c = r.u16()?;
                    if c != 0 {
                        map.insert(start + i, c);
                    }
                }
            }
            2 => {
                let n = r.u16()?;
                for _ in 0..n {
                    let s = r.u16()?;
                    let e = r.u16()?;
                    let c = r.u16()?;
                    for g in s..=e {
                        map.insert(g, c);
                    }
                }
            }
            _ => {}
        }
        Ok(ClassDef { map })
    }

    pub fn get(&self, g: u16) -> u16 {
        self.map.get(&g).copied().unwrap_or(0)
    }
}

/// Collect lookup indices for a feature tag (union over all scripts/langsys).
fn feature_lookups(data: &[u8], tag: &[u8; 4]) -> FontResult<Vec<u16>> {
    let feature_list = u16_at(data, 6)? as usize;
    let mut r = Reader::at(data, feature_list)?;
    let n = r.u16()?;
    let mut lookups = Vec::new();
    for _ in 0..n {
        let t = r.tag()?;
        let off = r.u16()? as usize;
        if &t == tag {
            let mut fr = Reader::at(data, feature_list + off)?;
            let _params = fr.u16()?;
            let count = fr.u16()?;
            for _ in 0..count {
                lookups.push(fr.u16()?);
            }
        }
    }
    lookups.sort_unstable();
    lookups.dedup();
    Ok(lookups)
}

/// Return the subtable offsets (absolute) of a lookup, resolving extensions,
/// together with the effective lookup type.
fn lookup_subtables(data: &[u8], index: u16, extension_type: u16) -> FontResult<(u16, Vec<usize>)> {
    let lookup_list = u16_at(data, 8)? as usize;
    let lookup_off = lookup_list + u16_at(data, lookup_list + 2 + 2 * index as usize)? as usize;
    let mut r = Reader::at(data, lookup_off)?;
    let declared = r.u16()?;
    let mut kind = declared;
    let _flag = r.u16()?;
    let n = r.u16()?;
    let mut subs = Vec::new();
    for _ in 0..n {
        let off = lookup_off + r.u16()? as usize;
        if declared == extension_type {
            let mut er = Reader::at(data, off)?;
            let _fmt = er.u16()?;
            let real_type = er.u16()?;
            let ext_off = er.u32()? as usize;
            kind = real_type;
            subs.push(off + ext_off);
        } else {
            subs.push(off);
        }
    }
    Ok((kind, subs))
}

/// One compiled substitution subtable.
#[derive(Clone, Debug)]
enum Subst {
    Single(HashMap<u16, u16>),
    Alternate(HashMap<u16, Vec<u16>>),
    /// first glyph -> list of (remaining components, ligature glyph), longest first
    Ligature(HashMap<u16, Vec<(Vec<u16>, u16)>>),
    /// Chaining contextual substitution, coverage based (format 3).
    Chain {
        backtrack: Vec<Coverage>,
        input: Vec<Coverage>,
        lookahead: Vec<Coverage>,
        records: Vec<(usize, u16)>,
    },
    /// Chaining contextual, glyph/class based rules (formats 1 and 2) flattened
    /// to explicit glyph-class sequences.
    ChainRules {
        cov: Coverage,
        classes: Option<[ClassDef; 3]>,
        /// keyed by first input glyph (format 1) or its class (format 2)
        rules: HashMap<u16, Vec<ChainRule>>,
    },
}

#[derive(Clone, Debug)]
struct ChainRule {
    backtrack: Vec<u16>,
    input: Vec<u16>, // excluding the first element
    lookahead: Vec<u16>,
    records: Vec<(usize, u16)>,
}

#[derive(Clone, Debug, Default)]
pub struct Gsub {
    lookups: HashMap<u16, Vec<Subst>>,
    features: HashMap<[u8; 4], Vec<u16>>,
}

const MAX_NESTING: usize = 4;

impl Gsub {
    pub fn parse(data: &[u8]) -> FontResult<Gsub> {
        let mut g = Gsub::default();
        for tag in [
            b"liga", b"ssty", b"smcp", b"ccmp", b"onum", b"lnum", b"c2sc", b"dlig",
        ] {
            let idx = feature_lookups(data, tag)?;
            if idx.is_empty() {
                continue;
            }
            for &li in &idx {
                g.load_lookup(data, li, 0)?;
            }
            g.features.insert(*tag, idx);
        }
        Ok(g)
    }

    fn load_lookup(&mut self, data: &[u8], li: u16, depth: usize) -> FontResult<()> {
        if self.lookups.contains_key(&li) || depth > MAX_NESTING {
            return Ok(());
        }
        self.lookups.insert(li, Vec::new());
        let (kind, subs) = lookup_subtables(data, li, 7)?;
        let mut compiled = Vec::new();
        let mut nested = Vec::new();
        for off in subs {
            if let Some(s) = Self::compile(data, kind, off)? {
                match &s {
                    Subst::Chain { records, .. } => nested.extend(records.iter().map(|r| r.1)),
                    Subst::ChainRules { rules, .. } => {
                        for rs in rules.values() {
                            for r in rs {
                                nested.extend(r.records.iter().map(|x| x.1));
                            }
                        }
                    }
                    _ => {}
                }
                compiled.push(s);
            }
        }
        self.lookups.insert(li, compiled);
        for n in nested {
            self.load_lookup(data, n, depth + 1)?;
        }
        Ok(())
    }

    fn read_records(r: &mut Reader) -> FontResult<Vec<(usize, u16)>> {
        let n = r.u16()?;
        let mut v = Vec::new();
        for _ in 0..n {
            v.push((r.u16()? as usize, r.u16()?));
        }
        Ok(v)
    }

    fn compile(data: &[u8], kind: u16, off: usize) -> FontResult<Option<Subst>> {
        let mut r = Reader::at(data, off)?;
        let format = r.u16()?;
        if kind == 6 && format == 3 {
            let mut covs = |r: &mut Reader| -> FontResult<Vec<Coverage>> {
                let n = r.u16()?;
                let mut v = Vec::new();
                for _ in 0..n {
                    v.push(Coverage::parse(data, off + r.u16()? as usize)?);
                }
                Ok(v)
            };
            let backtrack = covs(&mut r)?;
            let input = covs(&mut r)?;
            let lookahead = covs(&mut r)?;
            let records = Self::read_records(&mut r)?;
            return Ok(Some(Subst::Chain {
                backtrack,
                input,
                lookahead,
                records,
            }));
        }
        if (kind == 6 || kind == 5) && (format == 1 || format == 2) {
            return Self::compile_rules(data, kind, format, off).map(Some);
        }
        if kind == 5 && format == 3 {
            let n_glyphs = r.u16()?;
            let n_rec = r.u16()?;
            let mut input = Vec::new();
            for _ in 0..n_glyphs {
                input.push(Coverage::parse(data, off + r.u16()? as usize)?);
            }
            let mut records = Vec::new();
            for _ in 0..n_rec {
                records.push((r.u16()? as usize, r.u16()?));
            }
            return Ok(Some(Subst::Chain {
                backtrack: vec![],
                input,
                lookahead: vec![],
                records,
            }));
        }
        let cov_off = off + r.u16()? as usize;
        let cov = Coverage::parse(data, cov_off)?;
        Ok(match (kind, format) {
            (1, 1) => {
                let delta = r.i16()?;
                let map = cov
                    .glyphs()
                    .map(|(g, _)| (g, (g as i32 + delta as i32) as u16))
                    .collect();
                Some(Subst::Single(map))
            }
            (1, 2) => {
                let n = r.u16()? as usize;
                let mut subs = Vec::with_capacity(n);
                for _ in 0..n {
                    subs.push(r.u16()?);
                }
                let map = cov
                    .glyphs()
                    .filter_map(|(g, i)| subs.get(i as usize).map(|&s| (g, s)))
                    .collect();
                Some(Subst::Single(map))
            }
            (3, 1) => {
                let n = r.u16()? as usize;
                let mut sets = Vec::with_capacity(n);
                for _ in 0..n {
                    sets.push(off + r.u16()? as usize);
                }
                let mut map = HashMap::new();
                for (g, i) in cov.glyphs() {
                    if let Some(&so) = sets.get(i as usize) {
                        let mut sr = Reader::at(data, so)?;
                        let c = sr.u16()?;
                        let mut alts = Vec::new();
                        for _ in 0..c {
                            alts.push(sr.u16()?);
                        }
                        map.insert(g, alts);
                    }
                }
                Some(Subst::Alternate(map))
            }
            (4, 1) => {
                let n = r.u16()? as usize;
                let mut sets = Vec::with_capacity(n);
                for _ in 0..n {
                    sets.push(off + r.u16()? as usize);
                }
                let mut map: HashMap<u16, Vec<(Vec<u16>, u16)>> = HashMap::new();
                for (g, i) in cov.glyphs() {
                    let Some(&so) = sets.get(i as usize) else {
                        continue;
                    };
                    let mut sr = Reader::at(data, so)?;
                    let c = sr.u16()?;
                    let mut entries = Vec::new();
                    for _ in 0..c {
                        let lo = so + sr.u16()? as usize;
                        let mut lr = Reader::at(data, lo)?;
                        let lig = lr.u16()?;
                        let comp = lr.u16()?;
                        let mut rest = Vec::new();
                        for _ in 1..comp {
                            rest.push(lr.u16()?);
                        }
                        entries.push((rest, lig));
                    }
                    entries.sort_by(|a, b| b.0.len().cmp(&a.0.len()));
                    map.insert(g, entries);
                }
                Some(Subst::Ligature(map))
            }
            _ => None,
        })
    }

    fn compile_rules(data: &[u8], kind: u16, format: u16, off: usize) -> FontResult<Subst> {
        let mut r = Reader::at(data, off + 2)?;
        let cov = Coverage::parse(data, off + r.u16()? as usize)?;
        let classes = if format == 2 {
            if kind == 6 {
                let b = ClassDef::parse(data, off + r.u16()? as usize)?;
                let i = ClassDef::parse(data, off + r.u16()? as usize)?;
                let l = ClassDef::parse(data, off + r.u16()? as usize)?;
                Some([b, i, l])
            } else {
                let i = ClassDef::parse(data, off + r.u16()? as usize)?;
                Some([i.clone(), i.clone(), i])
            }
        } else {
            None
        };
        let n_sets = r.u16()? as usize;
        let mut set_offs = Vec::with_capacity(n_sets);
        for _ in 0..n_sets {
            set_offs.push(r.u16()? as usize);
        }
        // Keys: format 1 -> coverage glyph for set index; format 2 -> class value.
        let mut key_for_set: HashMap<usize, u16> = HashMap::new();
        if format == 1 {
            for (g, i) in cov.glyphs() {
                key_for_set.insert(i as usize, g);
            }
        }
        let mut rules: HashMap<u16, Vec<ChainRule>> = HashMap::new();
        for (si, &so) in set_offs.iter().enumerate() {
            if so == 0 {
                continue;
            }
            let key = if format == 1 {
                match key_for_set.get(&si) {
                    Some(&g) => g,
                    None => continue,
                }
            } else {
                si as u16
            };
            let set = off + so;
            let mut sr = Reader::at(data, set)?;
            let n_rules = sr.u16()?;
            for _ in 0..n_rules {
                let ro = set + sr.u16()? as usize;
                let mut rr = Reader::at(data, ro)?;
                let seq = |rr: &mut Reader, n: usize| -> FontResult<Vec<u16>> {
                    let mut v = Vec::with_capacity(n);
                    for _ in 0..n {
                        v.push(rr.u16()?);
                    }
                    Ok(v)
                };
                let rule = if kind == 6 {
                    let nb = rr.u16()? as usize;
                    let backtrack = seq(&mut rr, nb)?;
                    let ni = rr.u16()? as usize;
                    let input = seq(&mut rr, ni.saturating_sub(1))?;
                    let nl = rr.u16()? as usize;
                    let lookahead = seq(&mut rr, nl)?;
                    let records = Self::read_records(&mut rr)?;
                    ChainRule {
                        backtrack,
                        input,
                        lookahead,
                        records,
                    }
                } else {
                    let ni = rr.u16()? as usize;
                    let nrec = rr.u16()? as usize;
                    let input = seq(&mut rr, ni.saturating_sub(1))?;
                    let mut records = Vec::new();
                    for _ in 0..nrec {
                        records.push((rr.u16()? as usize, rr.u16()?));
                    }
                    ChainRule {
                        backtrack: vec![],
                        input,
                        lookahead: vec![],
                        records,
                    }
                };
                rules.entry(key).or_default().push(rule);
            }
        }
        Ok(Subst::ChainRules {
            cov,
            classes,
            rules,
        })
    }

    pub fn has_feature(&self, tag: &[u8; 4]) -> bool {
        self.features.contains_key(tag)
    }

    /// Apply a feature to a glyph run. `clusters[i]` is the number of input
    /// characters merged into output glyph `i`; ligatures sum their parts.
    pub fn apply(&self, tag: &[u8; 4], glyphs: &mut Vec<u16>, clusters: &mut Vec<usize>) {
        let Some(lookups) = self.features.get(tag) else {
            return;
        };
        for &li in lookups {
            let mut i = 0;
            while i < glyphs.len() {
                match self.apply_lookup_at(li, glyphs, clusters, i, 0) {
                    Some(next) => i = next.max(i + 1),
                    None => i += 1,
                }
            }
        }
    }

    /// Try lookup `li` at position `i`; returns the position after the
    /// affected glyphs if something was applied.
    fn apply_lookup_at(
        &self,
        li: u16,
        glyphs: &mut Vec<u16>,
        clusters: &mut Vec<usize>,
        i: usize,
        depth: usize,
    ) -> Option<usize> {
        if depth > MAX_NESTING || i >= glyphs.len() {
            return None;
        }
        let subs = self.lookups.get(&li)?;
        for s in subs {
            match s {
                Subst::Single(map) => {
                    if let Some(&n) = map.get(&glyphs[i]) {
                        glyphs[i] = n;
                        return Some(i + 1);
                    }
                }
                Subst::Alternate(map) => {
                    if let Some(a) = map.get(&glyphs[i]).and_then(|v| v.first()) {
                        glyphs[i] = *a;
                        return Some(i + 1);
                    }
                }
                Subst::Ligature(map) => {
                    if let Some(entries) = map.get(&glyphs[i]) {
                        for (rest, lig) in entries {
                            let end = i + 1 + rest.len();
                            if end <= glyphs.len() && glyphs[i + 1..end] == rest[..] {
                                let c: usize = clusters[i..end].iter().sum();
                                glyphs.splice(i..end, [*lig]);
                                clusters.splice(i..end, [c]);
                                return Some(i + 1);
                            }
                        }
                    }
                }
                Subst::Chain {
                    backtrack,
                    input,
                    lookahead,
                    records,
                } => {
                    if input.is_empty() || i + input.len() > glyphs.len() || i < backtrack.len() {
                        continue;
                    }
                    if i + input.len() + lookahead.len() > glyphs.len() {
                        continue;
                    }
                    let ok_in = input
                        .iter()
                        .enumerate()
                        .all(|(k, c)| c.get(glyphs[i + k]).is_some());
                    let ok_bt = backtrack
                        .iter()
                        .enumerate()
                        .all(|(k, c)| c.get(glyphs[i - 1 - k]).is_some());
                    let ok_la = lookahead
                        .iter()
                        .enumerate()
                        .all(|(k, c)| c.get(glyphs[i + input.len() + k]).is_some());
                    if ok_in && ok_bt && ok_la {
                        let before = glyphs.len();
                        for &(seq, nested) in records {
                            self.apply_lookup_at(nested, glyphs, clusters, i + seq, depth + 1);
                        }
                        let consumed =
                            input.len() as isize - (before as isize - glyphs.len() as isize);
                        return Some(i + consumed.max(1) as usize);
                    }
                }
                Subst::ChainRules {
                    cov,
                    classes,
                    rules,
                } => {
                    if cov.get(glyphs[i]).is_none() {
                        continue;
                    }
                    let key_of = |which: usize, g: u16| -> u16 {
                        match classes {
                            Some(c) => c[which].get(g),
                            None => g,
                        }
                    };
                    let Some(rs) = rules.get(&key_of(1, glyphs[i])) else {
                        continue;
                    };
                    for rule in rs {
                        let n_in = 1 + rule.input.len();
                        if i + n_in + rule.lookahead.len() > glyphs.len()
                            || i < rule.backtrack.len()
                        {
                            continue;
                        }
                        let ok_in = rule
                            .input
                            .iter()
                            .enumerate()
                            .all(|(k, &v)| key_of(1, glyphs[i + 1 + k]) == v);
                        let ok_bt = rule
                            .backtrack
                            .iter()
                            .enumerate()
                            .all(|(k, &v)| key_of(0, glyphs[i - 1 - k]) == v);
                        let ok_la = rule
                            .lookahead
                            .iter()
                            .enumerate()
                            .all(|(k, &v)| key_of(2, glyphs[i + n_in + k]) == v);
                        if ok_in && ok_bt && ok_la {
                            let before = glyphs.len();
                            for &(seq, nested) in &rule.records {
                                self.apply_lookup_at(nested, glyphs, clusters, i + seq, depth + 1);
                            }
                            let consumed =
                                n_in as isize - (before as isize - glyphs.len() as isize);
                            return Some(i + consumed.max(1) as usize);
                        }
                    }
                }
            }
        }
        None
    }

    /// Single-glyph substitution through a feature; for alternate lookups the
    /// alternate at `alt_index` (clamped) is chosen. Used for `ssty`.
    pub fn substitute(&self, tag: &[u8; 4], g: u16, alt_index: usize) -> u16 {
        let Some(lookups) = self.features.get(tag) else {
            return g;
        };
        let mut g = g;
        for li in lookups {
            for s in self.lookups.get(li).map(|v| v.as_slice()).unwrap_or(&[]) {
                match s {
                    Subst::Single(map) => {
                        if let Some(&n) = map.get(&g) {
                            g = n;
                        }
                    }
                    Subst::Alternate(map) => {
                        if let Some(alts) = map.get(&g) {
                            if !alts.is_empty() {
                                g = alts[alt_index.min(alts.len() - 1)];
                            }
                        }
                    }
                    _ => {}
                }
            }
        }
        g
    }
}

/// Pair-kerning subtable.
#[derive(Clone, Debug)]
enum PairSub {
    Format1 {
        cov: Coverage,
        pairs: Vec<HashMap<u16, i16>>,
    },
    Format2 {
        cov: Coverage,
        class1: ClassDef,
        class2: ClassDef,
        n_class2: usize,
        values: Vec<i16>,
    },
}

#[derive(Clone, Debug, Default)]
pub struct Gpos {
    subs: Vec<PairSub>,
}

fn value_record_size(fmt: u16) -> usize {
    (fmt & 0xFF).count_ones() as usize * 2
}

/// Read the XAdvance (or XPlacement fallback) from a value record.
fn read_x_advance(r: &mut Reader, fmt: u16) -> FontResult<i16> {
    let mut x_adv = 0i16;
    let mut x_place = 0i16;
    for bit in 0..8 {
        if fmt & (1 << bit) != 0 {
            let v = r.i16()?;
            match bit {
                0 => x_place = v,
                2 => x_adv = v,
                _ => {}
            }
        }
    }
    Ok(if x_adv != 0 { x_adv } else { x_place })
}

impl Gpos {
    pub fn parse(data: &[u8]) -> FontResult<Gpos> {
        let mut subs = Vec::new();
        for li in feature_lookups(data, b"kern")? {
            let (kind, offs) = lookup_subtables(data, li, 9)?;
            if kind != 2 {
                continue;
            }
            for off in offs {
                if let Some(s) = Self::compile_pair(data, off)? {
                    subs.push(s);
                }
            }
        }
        Ok(Gpos { subs })
    }

    fn compile_pair(data: &[u8], off: usize) -> FontResult<Option<PairSub>> {
        let mut r = Reader::at(data, off)?;
        let format = r.u16()?;
        let cov = Coverage::parse(data, off + r.u16()? as usize)?;
        let vf1 = r.u16()?;
        let vf2 = r.u16()?;
        match format {
            1 => {
                let n = r.u16()? as usize;
                let mut set_offs = Vec::with_capacity(n);
                for _ in 0..n {
                    set_offs.push(off + r.u16()? as usize);
                }
                let mut pairs = Vec::with_capacity(n);
                for so in set_offs {
                    let mut sr = Reader::at(data, so)?;
                    let c = sr.u16()?;
                    let mut m = HashMap::new();
                    for _ in 0..c {
                        let second = sr.u16()?;
                        let v = read_x_advance(&mut sr, vf1)?;
                        sr.skip(value_record_size(vf2))?;
                        if v != 0 {
                            m.insert(second, v);
                        }
                    }
                    pairs.push(m);
                }
                Ok(Some(PairSub::Format1 { cov, pairs }))
            }
            2 => {
                let class1 = ClassDef::parse(data, off + r.u16()? as usize)?;
                let class2 = ClassDef::parse(data, off + r.u16()? as usize)?;
                let n1 = r.u16()? as usize;
                let n2 = r.u16()? as usize;
                let mut values = Vec::with_capacity(n1 * n2);
                for _ in 0..n1 * n2 {
                    let v = read_x_advance(&mut r, vf1)?;
                    r.skip(value_record_size(vf2))?;
                    values.push(v);
                }
                Ok(Some(PairSub::Format2 {
                    cov,
                    class1,
                    class2,
                    n_class2: n2,
                    values,
                }))
            }
            _ => Ok(None),
        }
    }

    /// Kerning adjustment (font units) between two glyphs.
    pub fn kern(&self, left: u16, right: u16) -> i16 {
        for s in &self.subs {
            match s {
                PairSub::Format1 { cov, pairs } => {
                    if let Some(i) = cov.get(left) {
                        if let Some(v) = pairs.get(i as usize).and_then(|m| m.get(&right)) {
                            return *v;
                        }
                        // A pair set exists for `left`: stop only if right matched.
                    }
                }
                PairSub::Format2 {
                    cov,
                    class1,
                    class2,
                    n_class2,
                    values,
                } => {
                    if cov.get(left).is_some() {
                        let c1 = class1.get(left) as usize;
                        let c2 = class2.get(right) as usize;
                        if let Some(&v) = values.get(c1 * n_class2 + c2) {
                            if v != 0 {
                                return v;
                            }
                        }
                    }
                }
            }
        }
        0
    }

    pub fn is_empty(&self) -> bool {
        self.subs.is_empty()
    }
}
