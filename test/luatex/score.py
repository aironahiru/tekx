# score.py formulas.txt c.log layout.out: tekx's glyphs against LuaTeX's.
# Glyphs are paired after sorting by position; a formula agrees when every
# glyph and the width are within 2 em/1000.  Units are em/1000, y upwards.
import re, sys
labels = [l.split('\t')[0] for l in open(sys.argv[1]) if l.strip()]
lt, cur = {}, None
for l in open(sys.argv[2], errors='replace'):
    if l.startswith('@@ '):
        cur = l[3:].split()[0]; lt[cur] = [l]
    elif cur and l.startswith('   '):
        lt[cur].append(l)
    else:
        cur = None
tk, cur = {}, None
for l in open(sys.argv[3]):
    s = l.split()
    if s and (s[0].startswith('math@') or s[0].startswith('line@')):
        cur = len(tk); tk[cur] = []
    elif cur is not None:
        tk[cur].append(s)
def lt_glyphs(b):
    return sorted((float(x), float(y), float(w)) for x, y, w in
                  re.findall(r'G U\+\S+ sz=\d+ x=(-?[\d.]+) y=(-?[\d.]+) w=(-?[\d.]+)', ''.join(b)))
def tk_glyphs(b):
    return sorted((float(s[3]), -float(s[4]) if s[4] != '0' else 0.0, float(s[6])) for s in b if s[0] == 'g')
ok, rows = 0, []
for i, lab in enumerate(labels):
    a, b = lt.get(lab), tk.get(i)
    if not a or b is None:
        rows.append((9999, lab, 'missing')); continue
    ga, gb = lt_glyphs(a), tk_glyphs(b)
    W = float(re.search(r'W=(-?[\d.]+)', a[0]).group(1))
    w = next((float(s[1]) for s in b if s[0] == 'box'), 0)
    if len(ga) != len(gb):
        rows.append((999, lab, f'{len(ga)} glyphs against {len(gb)}')); continue
    dev = max([abs(W - w)] + [max(abs(p[0] - q[0]), abs(p[1] - q[1]), abs(p[2] - q[2])) for p, q in zip(ga, gb)])
    rows.append((dev, lab, ''))
    ok += dev <= 2
print(f"luatex: {ok}/{len(labels)} formulas within 2 em/1000 of LuaTeX")
for d, lab, why in sorted(rows, reverse=True):
    if d > 2:
        print(f"  {d:7.1f}  {lab} {why}")
