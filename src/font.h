/*
 * font.h -- OpenType font engine.
 *
 * Fonts are read in place: GSUB, GPOS and MATH lookups consult the font's
 * own binary tables directly, using a bounds-checked reader with a sticky
 * error flag.
 */
#ifndef FONT_H
#define FONT_H

#include "tekx.h"

/* bounds-checked big-endian reader; errors are sticky */
typedef struct {
	const u8 *p;
	size_t n, pos;
	int err;
} Rd;

Rd rd_at(const u8 *p, size_t n, size_t pos);
u8 rd_u8(Rd *r);
u16 rd_u16(Rd *r);
i16 rd_i16(Rd *r);
u32 rd_u24(Rd *r);
u32 rd_u32(Rd *r);
u32 rd_uint(Rd *r, int size);
void rd_skip(Rd *r, size_t n);
u16 u16at(const u8 *p, size_t n, size_t off);

typedef struct {
	double x0, y0, x1, y1;
} BBox;

/* MATH table constants, in table order */
enum {
	ScriptPercentScaleDown, ScriptScriptPercentScaleDown,
	DelimitedSubFormulaMinHeight, DisplayOperatorMinHeight,
	MathLeading, AxisHeight, AccentBaseHeight, FlattenedAccentBaseHeight,
	SubscriptShiftDown, SubscriptTopMax, SubscriptBaselineDropMin,
	SuperscriptShiftUp, SuperscriptShiftUpCramped, SuperscriptBottomMin,
	SuperscriptBaselineDropMax, SubSuperscriptGapMin,
	SuperscriptBottomMaxWithSubscript, SpaceAfterScript,
	UpperLimitGapMin, UpperLimitBaselineRiseMin, LowerLimitGapMin,
	LowerLimitBaselineDropMin, StackTopShiftUp, StackTopDisplayStyleShiftUp,
	StackBottomShiftDown, StackBottomDisplayStyleShiftDown, StackGapMin,
	StackDisplayStyleGapMin, StretchStackTopShiftUp,
	StretchStackBottomShiftDown, StretchStackGapAboveMin,
	StretchStackGapBelowMin, FractionNumeratorShiftUp,
	FractionNumeratorDisplayStyleShiftUp, FractionDenominatorShiftDown,
	FractionDenominatorDisplayStyleShiftDown, FractionNumeratorGapMin,
	FractionNumDisplayStyleGapMin, FractionRuleThickness,
	FractionDenominatorGapMin, FractionDenomDisplayStyleGapMin,
	SkewedFractionHorizontalGap, SkewedFractionVerticalGap,
	OverbarVerticalGap, OverbarRuleThickness, OverbarExtraAscender,
	UnderbarVerticalGap, UnderbarRuleThickness, UnderbarExtraDescender,
	RadicalVerticalGap, RadicalDisplayStyleVerticalGap,
	RadicalRuleThickness, RadicalExtraAscender, RadicalKernBeforeDegree,
	RadicalKernAfterDegree, RadicalDegreeBottomRaisePercent,
	MathConstCount
};
_Static_assert(MathConstCount == 56, "the OpenType MATH table has 56 constants, in this order");

typedef struct {
	u16 glyph, start, end, full;
	bool extender;
} GlyphPart;

typedef struct {
	int nvariants;
	u16 variant[64], advance[64];
	int nparts;
	GlyphPart part[16];
	i16 italic;
} Construction;

typedef struct Cff Cff;
typedef struct Ttf Ttf;

typedef struct Font {
	const u8 *data;
	size_t len;
	char *psname, *family;
	double upem, ascender, descender, linegap, capheight, xheight;
	double italic_angle, bbox[4];
	int weight, nglyphs;
	bool fixed, italic;
	u16 *adv;
	struct { u32 c; u16 g; } *cmap;
	size_t ncmap;
	Cff *cff;
	Ttf *ttf;
	BBox *bbcache;
	u8 *bbdone;
	/* table offsets (0 = absent) */
	size_t gsub, gsub_len, gpos, gpos_len, mathoff, mathlen;
	i16 mc[MathConstCount];
	bool hasmath;
} Font;

enum { PROGRAM_CFF, PROGRAM_TRUETYPE };

Font *font_load(const u8 *data, size_t len, char **err);
Font *font_builtin(const char *asset_name);
int font_glyph(Font *f, u32 cp);		/* 0 if missing */
double font_adv(Font *f, int gid);		/* font units */
BBox font_bbox(Font *f, int gid);		/* font units */
double font_kern(Font *f, int left, int right);
u32 font_pdf_flags(Font *f);

/* GSUB: apply a feature to a glyph run in place; clusters count the
 * characters each glyph stands for.  Returns the new length. */
int gsub_apply(Font *f, const char *tag, u16 *glyphs, int *clusters, int n);
int gsub_single(Font *f, const char *tag, int g, int alt);

/* MATH table queries (font units) */
int math_italic(Font *f, int g);
int math_top_accent(Font *f, int g, bool *found);
bool math_extended(Font *f, int g);
int math_kern(Font *f, int g, int corner, double height);	/* 0 TR, 1 TL, 2 BR, 3 BL */
bool math_construction(Font *f, int g, bool vertical, Construction *c);
int math_min_overlap(Font *f);

/* embedding */
u8 *font_subset(Font *f, const u8 *used, const char *name, size_t *len, int *kind);

/* CFF and TrueType internals */
Cff *cff_parse(const u8 *p, size_t n, char **err);
BBox cff_bbox(Cff *c, int gid);
int cff_nglyphs(Cff *c);
u8 *cff_subset(Cff *c, const u8 *used, int nglyphs, const char *name, size_t *len);
Ttf *ttf_parse(const u8 *loca, size_t nloca, const u8 *glyf, size_t nglyf, int nglyphs, bool longfmt);
BBox ttf_bbox(Ttf *t, int gid);
u8 *ttf_subset(Font *f, Ttf *t, const u8 *used, size_t *len);

/* sfnt helpers */
const u8 *font_table(Font *f, const char *tag, size_t *len);
u32 sfnt_checksum(const u8 *p, size_t n);

/* layout-table helpers */
int cov_index(const u8 *t, size_t n, size_t off, int g);
int class_of(const u8 *t, size_t n, size_t off, int g);

#endif
