//! OpenType `MATH` table: constants, glyph info and stretchy variants.

use super::otl::Coverage;
use super::reader::{FontResult, Reader};
use std::collections::HashMap;

/// All MathConstants, in font units (percentages as plain numbers).
#[derive(Clone, Debug, Default)]
pub struct MathConstants {
    pub script_percent_scale_down: i16,
    pub script_script_percent_scale_down: i16,
    pub delimited_sub_formula_min_height: i16,
    pub display_operator_min_height: i16,
    pub math_leading: i16,
    pub axis_height: i16,
    pub accent_base_height: i16,
    pub flattened_accent_base_height: i16,
    pub subscript_shift_down: i16,
    pub subscript_top_max: i16,
    pub subscript_baseline_drop_min: i16,
    pub superscript_shift_up: i16,
    pub superscript_shift_up_cramped: i16,
    pub superscript_bottom_min: i16,
    pub superscript_baseline_drop_max: i16,
    pub sub_superscript_gap_min: i16,
    pub superscript_bottom_max_with_subscript: i16,
    pub space_after_script: i16,
    pub upper_limit_gap_min: i16,
    pub upper_limit_baseline_rise_min: i16,
    pub lower_limit_gap_min: i16,
    pub lower_limit_baseline_drop_min: i16,
    pub stack_top_shift_up: i16,
    pub stack_top_display_style_shift_up: i16,
    pub stack_bottom_shift_down: i16,
    pub stack_bottom_display_style_shift_down: i16,
    pub stack_gap_min: i16,
    pub stack_display_style_gap_min: i16,
    pub stretch_stack_top_shift_up: i16,
    pub stretch_stack_bottom_shift_down: i16,
    pub stretch_stack_gap_above_min: i16,
    pub stretch_stack_gap_below_min: i16,
    pub fraction_numerator_shift_up: i16,
    pub fraction_numerator_display_style_shift_up: i16,
    pub fraction_denominator_shift_down: i16,
    pub fraction_denominator_display_style_shift_down: i16,
    pub fraction_numerator_gap_min: i16,
    pub fraction_num_display_style_gap_min: i16,
    pub fraction_rule_thickness: i16,
    pub fraction_denominator_gap_min: i16,
    pub fraction_denom_display_style_gap_min: i16,
    pub skewed_fraction_horizontal_gap: i16,
    pub skewed_fraction_vertical_gap: i16,
    pub overbar_vertical_gap: i16,
    pub overbar_rule_thickness: i16,
    pub overbar_extra_ascender: i16,
    pub underbar_vertical_gap: i16,
    pub underbar_rule_thickness: i16,
    pub underbar_extra_descender: i16,
    pub radical_vertical_gap: i16,
    pub radical_display_style_vertical_gap: i16,
    pub radical_rule_thickness: i16,
    pub radical_extra_ascender: i16,
    pub radical_kern_before_degree: i16,
    pub radical_kern_after_degree: i16,
    pub radical_degree_bottom_raise_percent: i16,
}

/// One part of a glyph assembly.
#[derive(Clone, Copy, Debug)]
pub struct GlyphPart {
    pub glyph: u16,
    pub start_connector: u16,
    pub end_connector: u16,
    pub full_advance: u16,
    pub extender: bool,
}

/// Size variants and assembly recipe for a stretchable glyph.
#[derive(Clone, Debug, Default)]
pub struct Construction {
    /// (glyph, advance measurement) in increasing size
    pub variants: Vec<(u16, u16)>,
    pub parts: Vec<GlyphPart>,
    pub assembly_italic: i16,
}

/// Cut-in kerning for one corner of a glyph (MathKern table).
#[derive(Clone, Debug, Default)]
pub struct MathKern {
    pub heights: Vec<i16>,
    pub kerns: Vec<i16>,
}

impl MathKern {
    pub fn at(&self, height: f64) -> i16 {
        if self.kerns.is_empty() {
            return 0;
        }
        for (i, &h) in self.heights.iter().enumerate() {
            if height < h as f64 {
                return self.kerns[i];
            }
        }
        *self.kerns.last().unwrap()
    }
}

#[derive(Clone, Debug, Default)]
pub struct MathKernInfo {
    pub top_right: MathKern,
    pub top_left: MathKern,
    pub bottom_right: MathKern,
    pub bottom_left: MathKern,
}

#[derive(Clone, Debug, Default)]
pub struct MathTable {
    pub constants: MathConstants,
    pub italics: HashMap<u16, i16>,
    pub top_accent: HashMap<u16, i16>,
    pub extended_shapes: HashMap<u16, ()>,
    pub kerns: HashMap<u16, MathKernInfo>,
    pub min_connector_overlap: u16,
    pub vertical: HashMap<u16, Construction>,
    pub horizontal: HashMap<u16, Construction>,
}

fn mvr(r: &mut Reader) -> FontResult<i16> {
    let v = r.i16()?;
    let _device = r.u16()?;
    Ok(v)
}

impl MathTable {
    pub fn parse(data: &[u8]) -> FontResult<MathTable> {
        let mut r = Reader::new(data);
        let _version = r.u32()?;
        let consts_off = r.u16()? as usize;
        let glyph_info_off = r.u16()? as usize;
        let variants_off = r.u16()? as usize;
        let mut t = MathTable {
            constants: Self::parse_constants(data, consts_off)?,
            ..Default::default()
        };
        if glyph_info_off != 0 {
            t.parse_glyph_info(data, glyph_info_off)?;
        }
        if variants_off != 0 {
            t.parse_variants(data, variants_off)?;
        }
        Ok(t)
    }

    fn parse_constants(data: &[u8], off: usize) -> FontResult<MathConstants> {
        let mut r = Reader::at(data, off)?;
        let mut c = MathConstants {
            script_percent_scale_down: r.i16()?,
            script_script_percent_scale_down: r.i16()?,
            delimited_sub_formula_min_height: r.u16()? as i16,
            display_operator_min_height: r.u16()? as i16,
            ..Default::default()
        };
        let fields: [&mut i16; 51] = [
            &mut c.math_leading,
            &mut c.axis_height,
            &mut c.accent_base_height,
            &mut c.flattened_accent_base_height,
            &mut c.subscript_shift_down,
            &mut c.subscript_top_max,
            &mut c.subscript_baseline_drop_min,
            &mut c.superscript_shift_up,
            &mut c.superscript_shift_up_cramped,
            &mut c.superscript_bottom_min,
            &mut c.superscript_baseline_drop_max,
            &mut c.sub_superscript_gap_min,
            &mut c.superscript_bottom_max_with_subscript,
            &mut c.space_after_script,
            &mut c.upper_limit_gap_min,
            &mut c.upper_limit_baseline_rise_min,
            &mut c.lower_limit_gap_min,
            &mut c.lower_limit_baseline_drop_min,
            &mut c.stack_top_shift_up,
            &mut c.stack_top_display_style_shift_up,
            &mut c.stack_bottom_shift_down,
            &mut c.stack_bottom_display_style_shift_down,
            &mut c.stack_gap_min,
            &mut c.stack_display_style_gap_min,
            &mut c.stretch_stack_top_shift_up,
            &mut c.stretch_stack_bottom_shift_down,
            &mut c.stretch_stack_gap_above_min,
            &mut c.stretch_stack_gap_below_min,
            &mut c.fraction_numerator_shift_up,
            &mut c.fraction_numerator_display_style_shift_up,
            &mut c.fraction_denominator_shift_down,
            &mut c.fraction_denominator_display_style_shift_down,
            &mut c.fraction_numerator_gap_min,
            &mut c.fraction_num_display_style_gap_min,
            &mut c.fraction_rule_thickness,
            &mut c.fraction_denominator_gap_min,
            &mut c.fraction_denom_display_style_gap_min,
            &mut c.skewed_fraction_horizontal_gap,
            &mut c.skewed_fraction_vertical_gap,
            &mut c.overbar_vertical_gap,
            &mut c.overbar_rule_thickness,
            &mut c.overbar_extra_ascender,
            &mut c.underbar_vertical_gap,
            &mut c.underbar_rule_thickness,
            &mut c.underbar_extra_descender,
            &mut c.radical_vertical_gap,
            &mut c.radical_display_style_vertical_gap,
            &mut c.radical_rule_thickness,
            &mut c.radical_extra_ascender,
            &mut c.radical_kern_before_degree,
            &mut c.radical_kern_after_degree,
        ];
        for f in fields {
            *f = mvr(&mut r)?;
        }
        c.radical_degree_bottom_raise_percent = r.i16()?;
        Ok(c)
    }

    fn parse_value_coverage(data: &[u8], off: usize) -> FontResult<HashMap<u16, i16>> {
        let mut r = Reader::at(data, off)?;
        let cov = Coverage::parse(data, off + r.u16()? as usize)?;
        let n = r.u16()? as usize;
        let mut vals = Vec::with_capacity(n);
        for _ in 0..n {
            vals.push(mvr(&mut r)?);
        }
        Ok(cov
            .glyphs()
            .filter_map(|(g, i)| vals.get(i as usize).map(|&v| (g, v)))
            .collect())
    }

    fn parse_kern(data: &[u8], off: usize) -> FontResult<MathKern> {
        let mut r = Reader::at(data, off)?;
        let n = r.u16()? as usize;
        let mut heights = Vec::with_capacity(n);
        for _ in 0..n {
            heights.push(mvr(&mut r)?);
        }
        let mut kerns = Vec::with_capacity(n + 1);
        for _ in 0..=n {
            kerns.push(mvr(&mut r)?);
        }
        Ok(MathKern { heights, kerns })
    }

    fn parse_glyph_info(&mut self, data: &[u8], off: usize) -> FontResult<()> {
        let mut r = Reader::at(data, off)?;
        let italics = r.u16()? as usize;
        let accent = r.u16()? as usize;
        let ext = r.u16()? as usize;
        let kern = r.u16()? as usize;
        if italics != 0 {
            self.italics = Self::parse_value_coverage(data, off + italics)?;
        }
        if accent != 0 {
            self.top_accent = Self::parse_value_coverage(data, off + accent)?;
        }
        if ext != 0 {
            let cov = Coverage::parse(data, off + ext)?;
            self.extended_shapes = cov.glyphs().map(|(g, _)| (g, ())).collect();
        }
        if kern != 0 {
            let base = off + kern;
            let mut kr = Reader::at(data, base)?;
            let cov = Coverage::parse(data, base + kr.u16()? as usize)?;
            let n = kr.u16()? as usize;
            let mut recs = Vec::with_capacity(n);
            for _ in 0..n {
                let mut corners = [0usize; 4];
                for c in corners.iter_mut() {
                    *c = kr.u16()? as usize;
                }
                recs.push(corners);
            }
            for (g, i) in cov.glyphs() {
                if let Some(c) = recs.get(i as usize) {
                    let load = |o: usize| -> FontResult<MathKern> {
                        if o == 0 {
                            Ok(MathKern::default())
                        } else {
                            Self::parse_kern(data, base + o)
                        }
                    };
                    self.kerns.insert(
                        g,
                        MathKernInfo {
                            top_right: load(c[0])?,
                            top_left: load(c[1])?,
                            bottom_right: load(c[2])?,
                            bottom_left: load(c[3])?,
                        },
                    );
                }
            }
        }
        Ok(())
    }

    fn parse_construction(data: &[u8], off: usize) -> FontResult<Construction> {
        let mut r = Reader::at(data, off)?;
        let assembly = r.u16()? as usize;
        let n = r.u16()? as usize;
        let mut variants = Vec::with_capacity(n);
        for _ in 0..n {
            variants.push((r.u16()?, r.u16()?));
        }
        let mut c = Construction {
            variants,
            ..Default::default()
        };
        if assembly != 0 {
            let mut ar = Reader::at(data, off + assembly)?;
            c.assembly_italic = mvr(&mut ar)?;
            let pc = ar.u16()? as usize;
            for _ in 0..pc {
                c.parts.push(GlyphPart {
                    glyph: ar.u16()?,
                    start_connector: ar.u16()?,
                    end_connector: ar.u16()?,
                    full_advance: ar.u16()?,
                    extender: ar.u16()? & 1 == 1,
                });
            }
        }
        Ok(c)
    }

    fn parse_variants(&mut self, data: &[u8], off: usize) -> FontResult<()> {
        let mut r = Reader::at(data, off)?;
        self.min_connector_overlap = r.u16()?;
        let vcov = r.u16()? as usize;
        let hcov = r.u16()? as usize;
        let vn = r.u16()? as usize;
        let hn = r.u16()? as usize;
        let mut voffs = Vec::with_capacity(vn);
        for _ in 0..vn {
            voffs.push(r.u16()? as usize);
        }
        let mut hoffs = Vec::with_capacity(hn);
        for _ in 0..hn {
            hoffs.push(r.u16()? as usize);
        }
        if vcov != 0 {
            let cov = Coverage::parse(data, off + vcov)?;
            for (g, i) in cov.glyphs() {
                if let Some(&o) = voffs.get(i as usize) {
                    if o != 0 {
                        self.vertical
                            .insert(g, Self::parse_construction(data, off + o)?);
                    }
                }
            }
        }
        if hcov != 0 {
            let cov = Coverage::parse(data, off + hcov)?;
            for (g, i) in cov.glyphs() {
                if let Some(&o) = hoffs.get(i as usize) {
                    if o != 0 {
                        self.horizontal
                            .insert(g, Self::parse_construction(data, off + o)?);
                    }
                }
            }
        }
        Ok(())
    }
}
