-- print every glyph/rule of box 0 with absolute position, in em/1000 (font 10pt)
local SP = 65536*10/1000  -- sp per em/1000 at 10pt
local function r(v) return math.floor(v/SP*10+0.5)/10 end
local out = {}
local function walk(head, parent, x0, y0, isv)
  local x, y = x0, y0
  if isv then y = y0 - parent.height end
  for n in node.traverse(head) do
    local id = n.id
    if id == node.id("glyph") then
      local f = font.getfont(n.font)
      local sz = f and f.size or 0
      out[#out+1] = string.format("G U+%04X sz=%.0f x=%s y=%s w=%s", n.char, sz/65536*100, r(x + (n.xoffset or 0)), r(-y + (n.yoffset or 0)), r(n.width))
      x = x + n.width
    elseif id == node.id("hlist") or id == node.id("vlist") then
      if isv then
        y = y + n.height
        walk(n.head, n, x + n.shift, y, id == node.id("vlist"))
        y = y + n.depth
      else
        walk(n.head, n, x, y + n.shift, id == node.id("vlist"))
        x = x + n.width
      end
    elseif id == node.id("rule") then
      local w = n.width; if w == -1073741824 then w = parent.width end
      local h = n.height; if h == -1073741824 then h = parent.height end
      local d = n.depth; if d == -1073741824 then d = parent.depth end
      if isv then
        out[#out+1] = string.format("R x=%s top=%s w=%s t=%s", r(x), r(-(y)), r(w), r(h+d))
        y = y + h + d
      else
        out[#out+1] = string.format("R x=%s top=%s w=%s t=%s", r(x), r(-(y-h)), r(w), r(h+d))
        x = x + w
      end
    elseif id == node.id("kern") then
      if isv then y = y + n.kern else
        if n.kern ~= 0 then out[#out+1] = string.format("K %s at x=%s sub=%d", r(n.kern), r(x), n.subtype) end
        x = x + n.kern end
    elseif id == node.id("glue") then
      local w = node.effective_glue(n, parent)
      if isv then y = y + w else
        if w ~= 0 then out[#out+1] = string.format("S %s at x=%s", r(w), r(x)) end
        x = x + w end
    end
  end
end
function dumpbox(label)
  local b = tex.box[0]
  out = {}
  walk(b.head, b, 0, 0, b.id == node.id("vlist"))
  texio.write_nl("term and log", "@@ " .. label .. string.format(" W=%s H=%s D=%s", r(b.width), r(b.height), r(b.depth)))
  for _, l in ipairs(out) do texio.write_nl("term and log", "   " .. l) end
  texio.write_nl("term and log", "")
end
