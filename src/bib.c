/*
 * bib.c -- BibTeX databases: read them, and write their entries as tekx.
 *
 * A mathematician's .bib file is the one piece of the LaTeX ecosystem
 * every paper depends on, so tekx reads it as it is: @string macros,
 * # concatenation, month names, braces, accents (\"u, {\'e}, \ss), math
 * in titles, and names in all three BibTeX forms ("First von Last",
 * "von Last, First", "von Last, Jr, First").  Entries are written as
 * tekx inline text in the style of the AMS (amsplain):
 *
 *	Christopher Hooley, *On the intervals between numbers that are sums
 *	of two squares*, Acta Math. **127** (1971), 279--297.
 *
 * so the rest of the pipeline sets them like any other text.
 */
#include "bib.h"

#include <ctype.h>
#include <strings.h>

/* ---- reading ---- */

typedef struct {
	const char *s;
	size_t n, i;
	int line;
	const char *path;
	bool bad;
} Lex;

static VEC(BibField) macros;

/*
 * Problems in a .bib file are warnings, as BibTeX makes them: a shared
 * bibliography with one broken entry must not stop every paper that uses
 * it.  A citation of an entry that could not be read is still an error.
 */
static void
lex_error(Lex *x, const char *msg)
{
	const char *saved = diag_file;

	diag_file = x->path;
	warn_at(x->line, 0, "%s", msg);
	diag_file = saved;
	x->bad = true;
}

static int
peek(Lex *x)
{
	return x->i < x->n ? (unsigned char)x->s[x->i] : EOF;
}

static int
get(Lex *x)
{
	if (x->i >= x->n)
		return EOF;
	char c = x->s[x->i++];
	if (c == '\n')
		x->line++;
	return (unsigned char)c;
}

static void
skip_space(Lex *x)
{
	while (peek(x) != EOF && (isspace(peek(x)) || peek(x) == '%')) {
		if (peek(x) == '%')
			while (peek(x) != EOF && peek(x) != '\n')
				get(x);
		else
			get(x);
	}
}

static char *
ident(Lex *x)
{
	size_t a = x->i;

	while (peek(x) != EOF && !isspace(peek(x)) && !strchr("{}(),=#\"%", peek(x)))
		get(x);
	return xstrndup(x->s + a, x->i - a);
}

static char *
lower(const char *s)
{
	char *t = xstrdup(s);

	for (char *p = t; *p; p++)
		*p = (char)tolower((unsigned char)*p);
	return t;
}

/* a braced or quoted part, delimiters removed, inner braces kept */
static void
delimited(Lex *x, Buf *b, int close)
{
	int depth = 0, start = x->line;

	for (;;) {
		int c = get(x);
		if (c == EOF) {
			x->line = start;
			lex_error(x, close == '"' ? "unterminated \"...\" value" : "unterminated {...} value");
			return;
		}
		if (c == '\\' && peek(x) != EOF) {
			bputc(b, c);
			bputc(b, get(x));
			continue;
		}
		if (depth == 0 && c == close)
			return;
		if (c == '{')
			depth++;
		else if (c == '}')
			depth--;
		bputc(b, c);
	}
}

static const char *
macro(const char *name)
{
	static const char *const months[][2] = {
		{"jan", "January"}, {"feb", "February"}, {"mar", "March"}, {"apr", "April"},
		{"may", "May"}, {"jun", "June"}, {"jul", "July"}, {"aug", "August"},
		{"sep", "September"}, {"oct", "October"}, {"nov", "November"}, {"dec", "December"},
	};
	char *l = lower(name);

	for (size_t i = macros.n; i-- > 0;)
		if (streq(macros.v[i].name, l))
			return macros.v[i].value;
	for (size_t i = 0; i < NELEM(months); i++)
		if (streq(months[i][0], l))
			return months[i][1];
	return NULL;
}

/* value = part (# part)* */
static char *
value(Lex *x)
{
	Buf b = {0};

	for (;;) {
		skip_space(x);
		int c = peek(x);
		if (c == '{') {
			get(x);
			delimited(x, &b, '}');
		} else if (c == '"') {
			get(x);
			delimited(x, &b, '"');
		} else if (c != EOF && isalnum(c)) {
			char *w = ident(x);
			if (isdigit((unsigned char)w[0])) {
				bputs(&b, w);
			} else {
				const char *m = macro(w);
				if (m == NULL)
					lex_error(x, fmt("unknown @string macro '%s' (taken as empty, as BibTeX does)", w));
				else
					bputs(&b, m);
			}
		} else {
			lex_error(x, "expected a value: {...}, \"...\", a number or a macro name");
			return bstr(&b);
		}
		skip_space(x);
		if (peek(x) != '#')
			return bstr(&b);
		get(x);
	}
}

static void
skip_group(Lex *x, int close)
{
	Buf junk = {0};

	delimited(x, &junk, close);
}

bool
bib_read(const char *path, BibEntries *out)
{
	size_t n;
	FILE *probe = fopen(path, "rb");

	/* a missing file is the document's error, reported where it is named */
	if (probe == NULL)
		return false;
	fclose(probe);
	char *s = read_file(path, &n);
	if (s == NULL)
		return false;
	Lex x = {s, n, 0, 1, path, false};
	for (;;) {
		while (peek(&x) != EOF && peek(&x) != '@')
			get(&x);
		if (get(&x) == EOF)
			break;
		skip_space(&x);
		char *type = lower(ident(&x));
		skip_space(&x);
		int open = get(&x);
		if (open != '{' && open != '(') {
			lex_error(&x, fmt("expected { after @%s", type));
			continue;
		}
		int close = open == '{' ? '}' : ')';
		if (streq(type, "comment") || streq(type, "preamble")) {
			skip_group(&x, close);
			continue;
		}
		if (streq(type, "string")) {
			skip_space(&x);
			char *name = lower(ident(&x));
			skip_space(&x);
			if (get(&x) != '=') {
				lex_error(&x, "expected = in @string");
				continue;
			}
			BibField m = {name, value(&x)};
			VPUSH(macros, m);
			skip_space(&x);
			if (get(&x) != close)
				lex_error(&x, "expected the end of @string");
			continue;
		}
		BibEntry e = {type, NULL, {0}, x.line};
		skip_space(&x);
		size_t a = x.i;
		while (peek(&x) != EOF && peek(&x) != ',' && peek(&x) != close && !isspace(peek(&x)))
			get(&x);
		e.key = xstrndup(s + a, x.i - a);
		if (!*e.key) {
			lex_error(&x, fmt("@%s entry without a key", type));
			continue;
		}
		for (;;) {
			skip_space(&x);
			int c = peek(&x);
			if (c == ',') {
				get(&x);
				continue;
			}
			if (c == close) {
				get(&x);
				break;
			}
			if (c == EOF) {
				lex_error(&x, fmt("entry %s is never closed", e.key));
				break;
			}
			char *name = lower(ident(&x));
			skip_space(&x);
			if (!*name || get(&x) != '=') {
				lex_error(&x, fmt("expected field = value in entry %s", e.key));
				/* resynchronize at the next field or entry */
				while (peek(&x) != EOF && peek(&x) != ',' && peek(&x) != '@')
					get(&x);
				if (peek(&x) == '@')
					break;
				continue;
			}
			BibField f = {name, value(&x)};
			VPUSH(e.f, f);
		}
		VPUSH(*out, e);
	}
	return true;
}

static void
set_field(BibEntry *e, const char *name, const char *value)
{
	BibField f = {xstrdup(name), xstrdup(value)};

	VPUSH(e->f, f);
}

/*
 * After reading: biblatex's names for BibTeX's fields (journaltitle,
 * date, location), and crossref, by which an entry inherits the fields
 * it lacks from another, its proceedings' title becoming its booktitle.
 * DBLP and Zotero write both.
 */
void
bib_resolve(BibEntries *db)
{
	static const char *const alias[][2] = {
		{"journaltitle", "journal"}, {"location", "address"}, {"institution", "school"},
	};

	for (size_t i = 0; i < db->n; i++) {
		BibEntry *e = &db->v[i];
		for (size_t k = 0; k < NELEM(alias); k++) {
			const char *v = bib_get(e, alias[k][0]);
			if (v && !bib_get(e, alias[k][1]) && !(streq(alias[k][1], "school") && !streq(e->type, "phdthesis") &&
			    !streq(e->type, "mastersthesis") && !streq(e->type, "thesis")))
				set_field(e, alias[k][1], v);
		}
		const char *date = bib_get(e, "date");
		if (date && !bib_get(e, "year") && strlen(date) >= 4)
			set_field(e, "year", xstrndup(date, 4));
		if (streq(e->type, "thesis")) {
			const char *ty = bib_get(e, "type");
			e->type = ty && (strstr(ty, "phd") || strstr(ty, "PhD") || strstr(ty, "Ph.D")) ? "phdthesis" : "mastersthesis";
		}
		if (streq(e->type, "online") || streq(e->type, "electronic") || streq(e->type, "www"))
			e->type = "misc";
	}
	for (size_t i = 0; i < db->n; i++) {
		BibEntry *e = &db->v[i];
		const char *xr = bib_get(e, "crossref");
		if (xr == NULL)
			continue;
		BibEntry *parent = NULL;
		for (size_t k = 0; k < db->n; k++)
			if (strcasecmp(db->v[k].key, xr) == 0)
				parent = &db->v[k];
		if (parent == NULL)
			continue;
		if (!bib_get(e, "booktitle") && bib_get(parent, "title"))
			set_field(e, "booktitle", bib_get(parent, "title"));
		for (size_t k = 0; k < parent->f.n; k++) {
			const char *name = parent->f.v[k].name;
			if (streq(name, "title") || streq(name, "crossref") || bib_get(e, name))
				continue;
			set_field(e, name, parent->f.v[k].value);
		}
	}
}

const char *
bib_get(const BibEntry *e, const char *name)
{
	for (size_t i = 0; i < e->f.n; i++)
		if (streq(e->f.v[i].name, name))
			return e->f.v[i].value;
	return NULL;
}

/* ---- LaTeX to text ---- */

static const struct { char acc; u32 base, cp; } accents[] = {
	{'"', 'A', 0xC4}, {'"', 'E', 0xCB}, {'"', 'H', 0x1E26}, {'"', 'I', 0xCF}, {'"', 'O', 0xD6},
	{'"', 'U', 0xDC}, {'"', 'W', 0x1E84}, {'"', 'X', 0x1E8C}, {'"', 'Y', 0x178}, {'"', 'a', 0xE4},
	{'"', 'e', 0xEB}, {'"', 'h', 0x1E27}, {'"', 'i', 0xEF}, {'"', 'o', 0xF6}, {'"', 't', 0x1E97},
	{'"', 'u', 0xFC}, {'"', 'w', 0x1E85}, {'"', 'x', 0x1E8D}, {'"', 'y', 0xFF}, {'"', 0x131, 0xEF},
	{'\'', 'A', 0xC1}, {'\'', 'C', 0x106}, {'\'', 'E', 0xC9}, {'\'', 'G', 0x1F4}, {'\'', 'I', 0xCD},
	{'\'', 'K', 0x1E30}, {'\'', 'L', 0x139}, {'\'', 'M', 0x1E3E}, {'\'', 'N', 0x143},
	{'\'', 'O', 0xD3}, {'\'', 'P', 0x1E54}, {'\'', 'R', 0x154}, {'\'', 'S', 0x15A}, {'\'', 'U', 0xDA},
	{'\'', 'W', 0x1E82}, {'\'', 'Y', 0xDD}, {'\'', 'Z', 0x179}, {'\'', 'a', 0xE1}, {'\'', 'c', 0x107},
	{'\'', 'e', 0xE9}, {'\'', 'g', 0x1F5}, {'\'', 'i', 0xED}, {'\'', 'k', 0x1E31}, {'\'', 'l', 0x13A},
	{'\'', 'm', 0x1E3F}, {'\'', 'n', 0x144}, {'\'', 'o', 0xF3}, {'\'', 'p', 0x1E55},
	{'\'', 'r', 0x155}, {'\'', 's', 0x15B}, {'\'', 'u', 0xFA}, {'\'', 'w', 0x1E83}, {'\'', 'y', 0xFD},
	{'\'', 'z', 0x17A}, {'\'', 0x131, 0xED}, {'`', 'A', 0xC0}, {'`', 'E', 0xC8}, {'`', 'I', 0xCC},
	{'`', 'N', 0x1F8}, {'`', 'O', 0xD2}, {'`', 'U', 0xD9}, {'`', 'W', 0x1E80}, {'`', 'Y', 0x1EF2},
	{'`', 'a', 0xE0}, {'`', 'e', 0xE8}, {'`', 'i', 0xEC}, {'`', 'n', 0x1F9}, {'`', 'o', 0xF2},
	{'`', 'u', 0xF9}, {'`', 'w', 0x1E81}, {'`', 'y', 0x1EF3}, {'`', 0x131, 0xEC}, {'^', 'A', 0xC2},
	{'^', 'C', 0x108}, {'^', 'E', 0xCA}, {'^', 'G', 0x11C}, {'^', 'H', 0x124}, {'^', 'I', 0xCE},
	{'^', 'J', 0x134}, {'^', 'O', 0xD4}, {'^', 'S', 0x15C}, {'^', 'U', 0xDB}, {'^', 'W', 0x174},
	{'^', 'Y', 0x176}, {'^', 'Z', 0x1E90}, {'^', 'a', 0xE2}, {'^', 'c', 0x109}, {'^', 'e', 0xEA},
	{'^', 'g', 0x11D}, {'^', 'h', 0x125}, {'^', 'i', 0xEE}, {'^', 'j', 0x135}, {'^', 'o', 0xF4},
	{'^', 's', 0x15D}, {'^', 'u', 0xFB}, {'^', 'w', 0x175}, {'^', 'y', 0x177}, {'^', 'z', 0x1E91},
	{'^', 0x131, 0xEE}, {'~', 'A', 0xC3}, {'~', 'E', 0x1EBC}, {'~', 'I', 0x128}, {'~', 'N', 0xD1},
	{'~', 'O', 0xD5}, {'~', 'U', 0x168}, {'~', 'V', 0x1E7C}, {'~', 'Y', 0x1EF8}, {'~', 'a', 0xE3},
	{'~', 'e', 0x1EBD}, {'~', 'i', 0x129}, {'~', 'n', 0xF1}, {'~', 'o', 0xF5}, {'~', 'u', 0x169},
	{'~', 'v', 0x1E7D}, {'~', 'y', 0x1EF9}, {'~', 0x131, 0x129}, {'=', 'A', 0x100}, {'=', 'E', 0x112},
	{'=', 'G', 0x1E20}, {'=', 'I', 0x12A}, {'=', 'O', 0x14C}, {'=', 'U', 0x16A}, {'=', 'Y', 0x232},
	{'=', 'a', 0x101}, {'=', 'e', 0x113}, {'=', 'g', 0x1E21}, {'=', 'i', 0x12B}, {'=', 'o', 0x14D},
	{'=', 'u', 0x16B}, {'=', 'y', 0x233}, {'=', 0x131, 0x12B}, {'.', 'A', 0x226}, {'.', 'B', 0x1E02},
	{'.', 'C', 0x10A}, {'.', 'D', 0x1E0A}, {'.', 'E', 0x116}, {'.', 'F', 0x1E1E}, {'.', 'G', 0x120},
	{'.', 'H', 0x1E22}, {'.', 'I', 0x130}, {'.', 'M', 0x1E40}, {'.', 'N', 0x1E44}, {'.', 'O', 0x22E},
	{'.', 'P', 0x1E56}, {'.', 'R', 0x1E58}, {'.', 'S', 0x1E60}, {'.', 'T', 0x1E6A},
	{'.', 'W', 0x1E86}, {'.', 'X', 0x1E8A}, {'.', 'Y', 0x1E8E}, {'.', 'Z', 0x17B}, {'.', 'a', 0x227},
	{'.', 'b', 0x1E03}, {'.', 'c', 0x10B}, {'.', 'd', 0x1E0B}, {'.', 'e', 0x117}, {'.', 'f', 0x1E1F},
	{'.', 'g', 0x121}, {'.', 'h', 0x1E23}, {'.', 'm', 0x1E41}, {'.', 'n', 0x1E45}, {'.', 'o', 0x22F},
	{'.', 'p', 0x1E57}, {'.', 'r', 0x1E59}, {'.', 's', 0x1E61}, {'.', 't', 0x1E6B},
	{'.', 'w', 0x1E87}, {'.', 'x', 0x1E8B}, {'.', 'y', 0x1E8F}, {'.', 'z', 0x17C}, {'u', 'A', 0x102},
	{'u', 'E', 0x114}, {'u', 'G', 0x11E}, {'u', 'I', 0x12C}, {'u', 'O', 0x14E}, {'u', 'U', 0x16C},
	{'u', 'a', 0x103}, {'u', 'e', 0x115}, {'u', 'g', 0x11F}, {'u', 'i', 0x12D}, {'u', 'o', 0x14F},
	{'u', 'u', 0x16D}, {'u', 0x131, 0x12D}, {'v', 'A', 0x1CD}, {'v', 'C', 0x10C}, {'v', 'D', 0x10E},
	{'v', 'E', 0x11A}, {'v', 'G', 0x1E6}, {'v', 'H', 0x21E}, {'v', 'I', 0x1CF}, {'v', 'K', 0x1E8},
	{'v', 'L', 0x13D}, {'v', 'N', 0x147}, {'v', 'O', 0x1D1}, {'v', 'R', 0x158}, {'v', 'S', 0x160},
	{'v', 'T', 0x164}, {'v', 'U', 0x1D3}, {'v', 'Z', 0x17D}, {'v', 'a', 0x1CE}, {'v', 'c', 0x10D},
	{'v', 'd', 0x10F}, {'v', 'e', 0x11B}, {'v', 'g', 0x1E7}, {'v', 'h', 0x21F}, {'v', 'i', 0x1D0},
	{'v', 'j', 0x1F0}, {'v', 'k', 0x1E9}, {'v', 'l', 0x13E}, {'v', 'n', 0x148}, {'v', 'o', 0x1D2},
	{'v', 'r', 0x159}, {'v', 's', 0x161}, {'v', 't', 0x165}, {'v', 'u', 0x1D4}, {'v', 'z', 0x17E},
	{'v', 0x131, 0x1D0}, {'H', 'O', 0x150}, {'H', 'U', 0x170}, {'H', 'o', 0x151}, {'H', 'u', 0x171},
	{'c', 'C', 0xC7}, {'c', 'D', 0x1E10}, {'c', 'E', 0x228}, {'c', 'G', 0x122}, {'c', 'H', 0x1E28},
	{'c', 'K', 0x136}, {'c', 'L', 0x13B}, {'c', 'N', 0x145}, {'c', 'R', 0x156}, {'c', 'S', 0x15E},
	{'c', 'T', 0x162}, {'c', 'c', 0xE7}, {'c', 'd', 0x1E11}, {'c', 'e', 0x229}, {'c', 'g', 0x123},
	{'c', 'h', 0x1E29}, {'c', 'k', 0x137}, {'c', 'l', 0x13C}, {'c', 'n', 0x146}, {'c', 'r', 0x157},
	{'c', 's', 0x15F}, {'c', 't', 0x163}, {'k', 'A', 0x104}, {'k', 'E', 0x118}, {'k', 'I', 0x12E},
	{'k', 'O', 0x1EA}, {'k', 'U', 0x172}, {'k', 'a', 0x105}, {'k', 'e', 0x119}, {'k', 'i', 0x12F},
	{'k', 'o', 0x1EB}, {'k', 'u', 0x173}, {'k', 0x131, 0x12F}, {'r', 'A', 0xC5}, {'r', 'U', 0x16E},
	{'r', 'a', 0xE5}, {'r', 'u', 0x16F}, {'r', 'w', 0x1E98}, {'r', 'y', 0x1E99}, {'b', 'B', 0x1E06},
	{'b', 'D', 0x1E0E}, {'b', 'K', 0x1E34}, {'b', 'L', 0x1E3A}, {'b', 'N', 0x1E48},
	{'b', 'R', 0x1E5E}, {'b', 'T', 0x1E6E}, {'b', 'Z', 0x1E94}, {'b', 'b', 0x1E07},
	{'b', 'd', 0x1E0F}, {'b', 'h', 0x1E96}, {'b', 'k', 0x1E35}, {'b', 'l', 0x1E3B},
	{'b', 'n', 0x1E49}, {'b', 'r', 0x1E5F}, {'b', 't', 0x1E6F}, {'b', 'z', 0x1E95},
	{'d', 'A', 0x1EA0}, {'d', 'B', 0x1E04}, {'d', 'D', 0x1E0C}, {'d', 'E', 0x1EB8},
	{'d', 'H', 0x1E24}, {'d', 'I', 0x1ECA}, {'d', 'K', 0x1E32}, {'d', 'L', 0x1E36},
	{'d', 'M', 0x1E42}, {'d', 'N', 0x1E46}, {'d', 'O', 0x1ECC}, {'d', 'R', 0x1E5A},
	{'d', 'S', 0x1E62}, {'d', 'T', 0x1E6C}, {'d', 'U', 0x1EE4}, {'d', 'V', 0x1E7E},
	{'d', 'W', 0x1E88}, {'d', 'Y', 0x1EF4}, {'d', 'Z', 0x1E92}, {'d', 'a', 0x1EA1},
	{'d', 'b', 0x1E05}, {'d', 'd', 0x1E0D}, {'d', 'e', 0x1EB9}, {'d', 'h', 0x1E25},
	{'d', 'i', 0x1ECB}, {'d', 'k', 0x1E33}, {'d', 'l', 0x1E37}, {'d', 'm', 0x1E43},
	{'d', 'n', 0x1E47}, {'d', 'o', 0x1ECD}, {'d', 'r', 0x1E5B}, {'d', 's', 0x1E63},
	{'d', 't', 0x1E6D}, {'d', 'u', 0x1EE5}, {'d', 'v', 0x1E7F}, {'d', 'w', 0x1E89},
	{'d', 'y', 0x1EF5}, {'d', 'z', 0x1E93}, {'d', 0x131, 0x1ECB},
};

static const struct { const char *name, *text; } symbols[] = {
	{"ss", "ß"}, {"o", "ø"}, {"O", "Ø"}, {"ae", "æ"}, {"AE", "Æ"}, {"oe", "œ"}, {"OE", "Œ"},
	{"aa", "å"}, {"AA", "Å"}, {"l", "ł"}, {"L", "Ł"}, {"i", "ı"}, {"j", "ȷ"}, {"dh", "ð"},
	{"DH", "Ð"}, {"th", "þ"}, {"TH", "Þ"}, {"ng", "ŋ"}, {"NG", "Ŋ"}, {"dj", "đ"}, {"DJ", "Đ"},
	{"textendash", "--"}, {"textemdash", "---"}, {"ldots", "..."}, {"dots", "..."},
	{"textquoteright", "’"}, {"textquoteleft", "‘"}, {"textquotedblleft", "“"},
	{"textquotedblright", "”"}, {"S", "§"}, {"P", "¶"}, {"copyright", "©"},
	{"TeX", "TeX"}, {"LaTeX", "LaTeX"}, {"BibTeX", "BibTeX"}, {"AmS", "AMS"},
	{"textsuperscript", ""}, {"relax", ""}, {"protect", ""}, {"noopsort", ""},
};

static u32
compose(char acc, u32 base)
{
	for (size_t i = 0; i < NELEM(accents); i++)
		if (accents[i].acc == acc && accents[i].base == base)
			return accents[i].cp;
	return 0;
}

/* the base letter of an accented one, for sorting */
static u32
decompose(u32 cp)
{
	for (size_t i = 0; i < NELEM(accents); i++)
		if (accents[i].cp == cp)
			return accents[i].base == 0x131 ? 'i' : accents[i].base;
	return cp;
}

typedef struct {
	const char *s;
	size_t i;
	bool emph;	/* \emph may become *...* (not inside a title, already italic) */
} Tex;

static void tex_text(Tex *t, Buf *b, int stop);
static char *url_escape(const char *u);

/* the argument of a command: a group or a single token */
static char *
tex_arg_raw(Tex *t)
{
	while (t->s[t->i] == ' ')
		t->i++;
	if (t->s[t->i] == '{') {
		int depth = 0;
		size_t a = ++t->i;
		for (; t->s[t->i]; t->i++) {
			if (t->s[t->i] == '\\' && t->s[t->i + 1]) {
				t->i++;
				continue;
			}
			if (t->s[t->i] == '{')
				depth++;
			else if (t->s[t->i] == '}' && depth-- == 0)
				break;
		}
		char *r = xstrndup(t->s + a, t->i - a);
		if (t->s[t->i])
			t->i++;
		return r;
	}
	if (t->s[t->i] == '\\') {
		size_t a = t->i++;
		while (isalpha((unsigned char)t->s[t->i]))
			t->i++;
		if (t->i == a + 1 && t->s[t->i])
			t->i++;
		return xstrndup(t->s + a, t->i - a);
	}
	if (!t->s[t->i])
		return xstrdup("");
	u32 cp;
	int k = utf8_dec(t->s + t->i, strlen(t->s + t->i), &cp);
	char *r = xstrndup(t->s + t->i, (size_t)k);
	t->i += (size_t)k;
	return r;
}

static char *
tex_convert(const char *s, bool emph)
{
	Tex t = {s, 0, emph};
	Buf b = {0};

	tex_text(&t, &b, 0);
	return bstr(&b);
}

/* characters that mean something in tekx inline text are escaped */
static void
put_escaped(Buf *b, int c)
{
	if (strchr("\\*`@[]^{<$", c))
		bputc(b, '\\');
	bputc(b, c);
}

static void
tex_text(Tex *t, Buf *b, int stop)
{
	const char *s = t->s;

	while (s[t->i] && s[t->i] != stop) {
		char c = s[t->i];
		if (c == '{') {
			t->i++;
			tex_text(t, b, '}');
			if (s[t->i] == '}')
				t->i++;
			continue;
		}
		if (c == '}') {		/* unbalanced: drop it */
			t->i++;
			continue;
		}
		if (c == '$') {		/* mathematics stays mathematics: tekx reads TeX notation */
			size_t e = t->i + 1;
			while (s[e] && s[e] != '$')
				e += s[e] == '\\' && s[e + 1] ? 2 : 1;
			bput(b, s + t->i, e - t->i + (s[e] == '$'));
			t->i = s[e] ? e + 1 : e;
			continue;
		}
		if (c == '`' && s[t->i + 1] == '`') {
			bputs(b, "“");
			t->i += 2;
			continue;
		}
		if (c == '\'' && s[t->i + 1] == '\'') {
			bputs(b, "”");
			t->i += 2;
			continue;
		}
		if (c == '\n' || c == '\t' || c == '\r') {
			bputc(b, ' ');
			t->i++;
			continue;
		}
		if (c != '\\') {
			if ((unsigned char)c < 0x80)
				put_escaped(b, c);
			else
				bputc(b, c);
			t->i++;
			continue;
		}
		/* a command */
		t->i++;
		char d = s[t->i];
		if (d && strchr("\"'`^~=.uvHckrbd", d) && (!isalpha((unsigned char)d) || !isalpha((unsigned char)s[t->i + 1]))) {
			t->i++;
			char *arg = tex_arg_raw(t);
			u32 base;
			if (streq(arg, "\\i") || streq(arg, "i") || streq(arg, "\\j"))
				base = streq(arg, "\\j") ? 'j' : (streq(arg, "i") ? 'i' : 0x131);
			else
				utf8_dec(arg, strlen(arg), &base);
			u32 cp = compose(d, base);
			if (cp)
				utf8_enc(b, cp);
			else
				bputs(b, tex_convert(arg, false));
			continue;
		}
		if (d && !isalpha((unsigned char)d)) {
			/* \& \% \$ \# \_ \{ \} \, \  \- \/ \\ */
			t->i++;
			if (strchr("&%$#_{}", d))
				put_escaped(b, d);
			else if (d == ',' || d == ' ' || d == '\\')
				bputc(b, ' ');
			continue;
		}
		size_t a = t->i;
		while (isalpha((unsigned char)s[t->i]))
			t->i++;
		char *name = xstrndup(s + a, t->i - a);
		const char *sym = NULL;
		for (size_t k = 0; k < NELEM(symbols); k++)
			if (streq(symbols[k].name, name))
				sym = symbols[k].text;
		if (sym) {
			bputs(b, sym);
			if (s[t->i] == '{' && s[t->i + 1] == '}')
				t->i += 2;
			else
				while (s[t->i] == ' ')
					t->i++;
			continue;
		}
		if (streq(name, "emph") || streq(name, "textit") || streq(name, "textsl") || streq(name, "mathit")) {
			char *in = tex_convert(tex_arg_raw(t), false);
			bputs(b, t->emph && *in ? fmt("*%s*", in) : in);
		} else if (streq(name, "textbf")) {
			char *in = tex_convert(tex_arg_raw(t), false);
			bputs(b, *in ? fmt("**%s**", in) : in);
		} else if (streq(name, "url")) {
			bprintf(b, "<%s>", url_escape(tex_arg_raw(t)));
		} else if (streq(name, "href")) {
			char *u = tex_arg_raw(t), *txt = tex_convert(tex_arg_raw(t), false);
			bprintf(b, "[%s](%s)", txt, url_escape(u));
		} else {
			/* \textsc{x}, \mbox{x}, \textrm{x}, \texttt{x}, unknown: keep the text */
			while (s[t->i] == ' ')
				t->i++;
			if (s[t->i] == '{')
				bputs(b, tex_convert(tex_arg_raw(t), t->emph));
		}
	}
}

/* ---- names ---- */

typedef struct {
	char *first, *von, *last, *jr;
} Name;

typedef VEC(char *) Words;

/* split at top-level occurrences of `sep` (a word: "and", or ",") */
static Words *
split_top(const char *s, const char *sep)
{
	Words *v = xcalloc(1, sizeof *v);
	size_t sl = strlen(sep), a = 0;
	int depth = 0;

	for (size_t i = 0; s[i]; i++) {
		if (s[i] == '{')
			depth++;
		else if (s[i] == '}')
			depth--;
		else if (depth == 0 && strncmp(s + i, sep, sl) == 0) {
			bool word = isalpha((unsigned char)sep[0]);
			if (word && !((i == 0 || isspace((unsigned char)s[i - 1])) && (isspace((unsigned char)s[i + sl]) || !s[i + sl])))
				continue;
			VPUSH(*v, trim(xstrndup(s + a, i - a)));
			a = i + sl;
			i += sl - 1;
		}
	}
	VPUSH(*v, trim(xstrdup(s + a)));
	return v;
}

static bool
lowercase_word(const char *w)
{
	/* a von part starts with a lowercase letter outside braces */
	while (*w == '{' || *w == '\\') {
		if (*w == '{')
			return false;
		w++;
		while (isalpha((unsigned char)*w))
			w++;
	}
	return islower((unsigned char)*w);
}

static Name
parse_name(const char *raw)
{
	Name nm = {"", "", "", ""};
	Words *parts = split_top(raw, ",");
	const char *head = parts->v[0];

	if (parts->n >= 3) {
		nm.jr = parts->v[1];
		nm.first = parts->v[2];
	} else if (parts->n == 2) {
		nm.first = parts->v[1];
	}
	Words *w = split_top(head, " ");
	Words words = {0};
	for (size_t i = 0; i < w->n; i++)
		if (*w->v[i])
			VPUSH(words, w->v[i]);
	if (words.n == 0)
		return nm;
	Buf first = {0}, von = {0}, last = {0};
	size_t i = 0;
	if (parts->n == 1) {
		/* First von Last: the last word is the last name, a run of lowercase
		 * words before it is the von part, and the rest is the first name */
		while (i + 1 < words.n && !lowercase_word(words.v[i]))
			bprintf(&first, "%s%s", first.n ? " " : "", words.v[i++]);
	}
	while (i + 1 < words.n && lowercase_word(words.v[i]))
		bprintf(&von, "%s%s", von.n ? " " : "", words.v[i++]);
	while (i < words.n)
		bprintf(&last, "%s%s", last.n ? " " : "", words.v[i++]);
	if (parts->n == 1)
		nm.first = bstr(&first);
	nm.von = bstr(&von);
	nm.last = bstr(&last);
	return nm;
}

static char *
name_text(Name nm)
{
	Buf b = {0};

	if (*nm.first)
		bprintf(&b, "%s ", tex_convert(nm.first, false));
	if (*nm.von)
		bprintf(&b, "%s ", tex_convert(nm.von, false));
	bputs(&b, tex_convert(nm.last, false));
	if (*nm.jr)
		bprintf(&b, ", %s", tex_convert(nm.jr, false));
	return bstr(&b);
}

/* "A", "A and B", "A, B, and C" */
static char *
names(const char *raw, int *count)
{
	Words *v = split_top(raw, "and");
	Buf b = {0};

	*count = (int)v->n;
	bool etal = v->n > 1 && streq(v->v[v->n - 1], "others");
	size_t n = etal ? v->n - 1 : v->n;
	for (size_t i = 0; i < n; i++) {
		if (i > 0)
			bputs(&b, etal || n > 2 ? (i + 1 == n && !etal ? ", and " : ", ") : " and ");
		bputs(&b, name_text(parse_name(v->v[i])));
	}
	if (etal)
		bputs(&b, n > 1 ? ", et al." : " et al.");
	return bstr(&b);
}

/* ---- formatting ---- */

/* a URL inside [text](url) or <url>: parentheses and blanks would end it */
static char *
url_escape(const char *u)
{
	Buf b = {0};

	for (; *u; u++) {
		if (*u == '(' || *u == ')' || *u == ' ' || *u == '<' || *u == '>')
			bprintf(&b, "%%%02X", (unsigned char)*u);
		else
			bputc(&b, *u);
	}
	return bstr(&b);
}

/* the visible text of a DOI or arXiv link: tekx markup escaped */
static char *
plain_escaped(const char *s)
{
	Buf b = {0};

	for (; *s; s++)
		put_escaped(&b, *s);
	return bstr(&b);
}

static const char *
field_text(const BibEntry *e, const char *name)
{
	const char *v = bib_get(e, name);

	return v && *v ? tex_convert(v, true) : NULL;
}

static void
part(Buf *b, const char *s)
{
	if (s && *s)
		bprintf(b, "%s%s", b->n ? ", " : "", s);
}

static char *
ordinal(const char *ed)
{
	int n = atoi(ed);

	if (n <= 0)
		return fmt("%s ed.", ed);
	const char *suf = (n % 100 >= 11 && n % 100 <= 13) ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
	return fmt("%d%s ed.", n, suf);
}

char *
bib_format(const BibEntry *e)
{
	Buf b = {0};
	const char *t = e->type;
	const char *year = field_text(e, "year"), *pages = field_text(e, "pages");
	const char *vol = field_text(e, "volume"), *num = field_text(e, "number");
	const char *raw_title = bib_get(e, "title");
	int n;

	const char *au = bib_get(e, "author");
	const char *ed = bib_get(e, "editor");
	if (au) {
		part(&b, names(au, &n));
	} else if (ed) {
		char *who = names(ed, &n);
		part(&b, fmt("%s (%s)", who, n > 1 ? "eds." : "ed."));
	}
	if (raw_title)
		part(&b, fmt("*%s*", tex_convert(raw_title, false)));

	bool article = streq(t, "article");
	bool incoll = streq(t, "incollection") || streq(t, "inproceedings") || streq(t, "conference") || streq(t, "inbook");
	if (article) {
		Buf j = {0};
		const char *journal = field_text(e, "journal");
		if (journal)
			bputs(&j, journal);
		if (vol)
			bprintf(&j, "%s**%s**", j.n ? " " : "", vol);
		if (year)
			bprintf(&j, "%s(%s)", j.n ? " " : "", year);
		part(&b, bstr(&j));
		if (num)
			part(&b, fmt("no. %s", num));
		if (pages)
			part(&b, pages);
	} else {
		if (incoll) {
			const char *bt = field_text(e, "booktitle");
			if (bt) {
				if (au && ed) {
					int ne;
					char *who = names(ed, &ne);
					part(&b, fmt("%s (%s, %s)", bt, who, ne > 1 ? "eds." : "ed."));
				} else {
					part(&b, bt);
				}
			}
		}
		const char *series = field_text(e, "series");
		part(&b, series);
		if (vol)
			part(&b, fmt("vol. %s", vol));
		if (num && !vol && series)
			part(&b, fmt("no. %s", num));
		const char *edition = field_text(e, "edition");
		if (edition)
			part(&b, ordinal(edition));
		if (streq(t, "phdthesis"))
			part(&b, "Ph.D. thesis");
		else if (streq(t, "mastersthesis"))
			part(&b, "Master's thesis");
		else if (streq(t, "techreport")) {
			const char *ty = field_text(e, "type");
			part(&b, num ? fmt("%s %s", ty ? ty : "Tech. Report", num) : (ty ? ty : "Tech. report"));
		}
		part(&b, field_text(e, "school"));
		part(&b, field_text(e, "institution"));
		part(&b, field_text(e, "organization"));
		part(&b, field_text(e, "publisher"));
		part(&b, field_text(e, "address"));
		part(&b, field_text(e, "howpublished"));
		part(&b, year);
		if (pages)
			part(&b, fmt("%s %s", strpbrk(pages, "-,–") ? "pp." : "p.", pages));
	}
	part(&b, field_text(e, "note"));
	const char *eprint = bib_get(e, "eprint"), *doi = bib_get(e, "doi"), *url = bib_get(e, "url");
	const char *arch = bib_get(e, "archiveprefix") ? bib_get(e, "archiveprefix") : bib_get(e, "eprinttype");
	if (eprint && (!arch || strstr(lower(arch), "arxiv")))
		part(&b, fmt("[arXiv:%s](https://arxiv.org/abs/%s)", plain_escaped(eprint), url_escape(eprint)));
	if (doi) {
		const char *d = startswith(doi, "https://doi.org/") ? doi + 16 : doi;
		part(&b, fmt("[doi:%s](https://doi.org/%s)", plain_escaped(d), url_escape(d)));
	} else if (url) {
		part(&b, fmt("<%s>", url_escape(url)));
	}
	bputc(&b, '.');
	return bstr(&b);
}

/* the letters of a name, accents folded: "Erdős" -> "Erdos" */
static char *
fold(const char *s)
{
	Buf b = {0};

	for (size_t i = 0; s[i];) {
		u32 cp;
		i += (size_t)utf8_dec(s + i, strlen(s + i), &cp);
		cp = decompose(cp);
		if (cp < 0x80 && isalpha((int)cp))
			bputc(&b, (int)cp);
		else if (cp == 0xDF)
			bputs(&b, "ss");
		else if (cp >= 0x80)
			utf8_enc(&b, cp);
	}
	return bstr(&b);
}

/*
 * The label of amsalpha: one author, the first three letters of the last
 * name and the year's last two digits (Hoo71); two or three authors,
 * their initials (BC47); more, three initials and a plus (ABC+99).
 */
char *
bib_alpha_label(const BibEntry *e)
{
	const char *who = bib_get(e, "author") ? bib_get(e, "author") : bib_get(e, "editor");
	const char *year = bib_get(e, "year");
	Buf b = {0};

	if (who) {
		Words *v = split_top(who, "and");
		size_t n = v->n;
		bool others = n > 1 && streq(v->v[n - 1], "others");
		if (n == 1) {
			/* the initials of the von and last parts (de la Vallée Poussin:
			 * dlVP), or, when that is one letter, the last name's first three */
			Name nm = parse_name(v->v[0]);
			char *parts = fmt("%s %s", nm.von, nm.last);
			Words *w = split_top(parts, " ");
			Buf ini = {0};
			for (size_t i = 0; i < w->n; i++) {
				char *f = fold(tex_convert(w->v[i], false));
				for (char *h = f; *h; ) {
					u32 cp;
					size_t k = (size_t)utf8_dec(h, strlen(h), &cp);
					utf8_enc(&ini, cp);
					char *dash = strchr(h, '-');
					if (dash == NULL)
						break;
					h = dash + 1;
					(void)k;
				}
			}
			if (utf8_len(bstr(&ini)) >= 2) {
				bputs(&b, bstr(&ini));
			} else {
				char *last = fold(tex_convert(nm.last, false));
				u32 cp;
				for (size_t i = 0, k = 0; last[i] && k < 3; k++)
					i += (size_t)utf8_dec(last + i, strlen(last + i), &cp), utf8_enc(&b, cp);
			}
		} else {
			size_t take = n > 4 || others ? 3 : n;
			for (size_t i = 0; i < take && i < n; i++) {
				if (streq(v->v[i], "others"))
					break;
				char *last = fold(tex_convert(parse_name(v->v[i]).last, false));
				u32 cp;
				if (*last) {
					utf8_dec(last, strlen(last), &cp);
					utf8_enc(&b, cp);
				}
			}
			if (n > 4 || others)
				bputc(&b, '+');
		}
	} else {
		bputs(&b, "Anon");
	}
	if (year) {
		size_t yl = strlen(year);
		bputs(&b, yl >= 2 ? year + yl - 2 : year);
	}
	return bstr(&b);
}

/* last names, year and title, folded to plain lowercase letters */
char *
bib_sort_key(const BibEntry *e)
{
	Buf raw = {0}, key = {0};
	const char *who = bib_get(e, "author") ? bib_get(e, "author") : bib_get(e, "editor");

	if (who) {
		Words *v = split_top(who, "and");
		for (size_t i = 0; i < v->n; i++) {
			Name nm = parse_name(v->v[i]);
			bprintf(&raw, "%s %s ", tex_convert(nm.last, false), tex_convert(nm.first, false));
		}
	}
	bprintf(&raw, "%s %s", bib_get(e, "year") ? bib_get(e, "year") : "", bib_get(e, "title") ? tex_convert(bib_get(e, "title"), false) : "");
	const char *s = bstr(&raw);
	for (size_t i = 0; s[i];) {
		u32 cp;
		i += (size_t)utf8_dec(s + i, strlen(s + i), &cp);
		cp = decompose(cp);
		if (cp == 0xDF)
			bputs(&key, "ss");
		else if (cp < 0x80 && (isalnum((int)cp) || cp == ' '))
			bputc(&key, tolower((int)cp));
		else if (cp >= 0x80)
			utf8_enc(&key, cp);
	}
	return bstr(&key);
}
