-- Overlay registry for scripts that draw on the picture itself.
--
-- A panel (mse.ui) is a window beside the game. An overlay draws *into* the
-- game, and it does it the way the console would: into an RGBA image the same
-- size as the framebuffer, which is then laid over the picture and scaled with
-- the same nearest-neighbour filter. A hitbox is therefore made of the same
-- pixels as the sprite it sits on, at any window size, instead of being a
-- screen-space rectangle that lands on fractions of one.
--
-- That is also why everything here takes integer game pixels and why the font
-- is a bitmap: there is no such thing as a half-pixel on this surface.
--
-- Scripts are loaded from data/lua/overlays and from whatever a backend ships,
-- so dropping a file in that directory is all it takes to add one.

local ffi     = require("ffi")
local ig      = require("mse.imgui")
local texture = require("mse.texture")

local band, bor, rshift, lshift = bit.band, bit.bor, bit.rshift, bit.lshift
local floor = math.floor

local overlay = {}

local overlays = {}
local by_id    = {}

-- ---------------------------------------------------------------- the canvas

-- One buffer and one texture, reallocated only if the console's resolution
-- changes. Textures registered with ImGui are never destroyed (see
-- mse.texture), so a backend that changed resolution every frame would leak
-- one per change; none of them do, and the NES never will.
local canvas = {
	width   = 0,
	height  = 0,
	pixels  = nil,
	bytes   = 0,
	texture = nil,
	dirty   = false,
}

local function canvas_resize(width, height)
	if canvas.width == width and canvas.height == height then
		return
	end

	canvas.width   = width
	canvas.height  = height
	canvas.bytes   = width * height * 4
	canvas.pixels  = ffi.new("uint32_t[?]", width * height)
	canvas.texture = texture.new(width, height)
end

-- Source-over, with both sides non-premultiplied: that is the form ImGui's
-- renderer expects, so a half-transparent panel drawn here blends against the
-- game exactly once rather than being darkened on the way in and again on the
-- way out.
local function blend(dst, src)
	local sa = band(rshift(src, 24), 0xFF)
	if sa == 0xFF then
		return src
	end
	if sa == 0 then
		return dst
	end

	local da = band(rshift(dst, 24), 0xFF)
	if da == 0 then
		return src
	end

	local weight = floor(da * (255 - sa) / 255)
	local out_a  = sa + weight
	if out_a == 0 then
		return 0
	end

	local function channel(shift)
		return floor((band(rshift(src, shift), 0xFF) * sa +
		              band(rshift(dst, shift), 0xFF) * weight) / out_a)
	end

	return bor(channel(0), lshift(channel(8), 8), lshift(channel(16), 16), lshift(out_a, 24))
end

-- The view handed to every draw. One table, refilled each frame: an overlay
-- runs every frame and allocating a fresh one per overlay per frame is pure
-- garbage. Scripts must not hold on to it.
local view = {}

--- Writes one pixel. Out-of-bounds coordinates are dropped, so an overlay can
--- work in level coordinates and let the edges clip themselves.
function view:plot(x, y, colour)
	x, y = floor(x), floor(y)
	if x < 0 or y < 0 or x >= canvas.width or y >= canvas.height then
		return
	end

	local index = y * canvas.width + x
	canvas.pixels[index] = blend(canvas.pixels[index], colour)
	canvas.dirty = true
end

--- A filled rectangle, in game pixels. x1/y1 are exclusive, like every other
--- rectangle in this file.
function view:filled(x0, y0, x1, y1, colour)
	x0, y0, x1, y1 = floor(x0), floor(y0), floor(x1), floor(y1)

	if x0 < 0 then x0 = 0 end
	if y0 < 0 then y0 = 0 end
	if x1 > canvas.width then x1 = canvas.width end
	if y1 > canvas.height then y1 = canvas.height end
	if x1 <= x0 or y1 <= y0 then
		return
	end

	local pixels = canvas.pixels
	local opaque = band(rshift(colour, 24), 0xFF) == 0xFF

	for y = y0, y1 - 1 do
		local row = y * canvas.width
		for x = x0, x1 - 1 do
			pixels[row + x] = opaque and colour or blend(pixels[row + x], colour)
		end
	end

	canvas.dirty = true
end

--- A one-pixel outline. The coordinates are the same ones a game keeps in RAM,
--- so a bounding box can be passed through unchanged.
function view:box(x0, y0, x1, y1, colour)
	self:filled(x0, y0, x1, y0 + 1, colour)
	self:filled(x0, y1 - 1, x1, y1, colour)
	self:filled(x0, y0 + 1, x0 + 1, y1 - 1, colour)
	self:filled(x1 - 1, y0 + 1, x1, y1 - 1, colour)
end

--- A one-pixel line, Bresenham.
function view:line(x0, y0, x1, y1, colour)
	x0, y0, x1, y1 = floor(x0), floor(y0), floor(x1), floor(y1)

	local dx = math.abs(x1 - x0)
	local dy = -math.abs(y1 - y0)
	local sx = x0 < x1 and 1 or -1
	local sy = y0 < y1 and 1 or -1
	local err = dx + dy

	while true do
		self:plot(x0, y0, colour)
		if x0 == x1 and y0 == y1 then
			break
		end
		local e2 = 2 * err
		if e2 >= dy then
			err = err + dy
			x0 = x0 + sx
		end
		if e2 <= dx then
			err = err + dx
			y0 = y0 + sy
		end
	end
end

-- ------------------------------------------------------------------ the font

-- A 3x5 bitmap font, one glyph per line: the character, then its five rows.
--
-- Bitmap rather than the UI font because this surface is 256 pixels across and
-- gets magnified four times: a proportional anti-aliased glyph rendered at this
-- size and then scaled up is a grey smear. Three pixels wide is also what fits
-- a useful amount of text across a NES screen.
--
-- Lowercase is folded to uppercase. Losing the distinction costs nothing over a
-- game whose own font has no lowercase either, and a readable 3x5 lowercase
-- alphabet does not exist.
local FONT_SOURCE = [[
0 ### #.# #.# #.# ###
1 .#. ##. .#. .#. ###
2 ### ..# ### #.. ###
3 ### ..# ### ..# ###
4 #.# #.# ### ..# ..#
5 ### #.. ### ..# ###
6 ### #.. ### #.# ###
7 ### ..# ..# ..# ..#
8 ### #.# ### #.# ###
9 ### #.# ### ..# ###
A .#. #.# ### #.# #.#
B ##. #.# ##. #.# ##.
C .## #.. #.. #.. .##
D ##. #.# #.# #.# ##.
E ### #.. ##. #.. ###
F ### #.. ##. #.. #..
G .## #.. #.# #.# .##
H #.# #.# ### #.# #.#
I ### .#. .#. .#. ###
J ..# ..# ..# #.# .#.
K #.# #.# ##. #.# #.#
L #.. #.. #.. #.. ###
M #.# ### ### #.# #.#
N ##. #.# #.# #.# #.#
O .#. #.# #.# #.# .#.
P ##. #.# ##. #.. #..
Q .#. #.# #.# ### ..#
R ##. #.# ##. #.# #.#
S .## #.. .#. ..# ##.
T ### .#. .#. .#. .#.
U #.# #.# #.# #.# ###
V #.# #.# #.# #.# .#.
W #.# #.# ### ### #.#
X #.# #.# .#. #.# #.#
Y #.# #.# .#. .#. .#.
Z ### ..# .#. #.. ###
. ... ... ... ... .#.
, ... ... ... .#. #..
: ... .#. ... .#. ...
; ... .#. ... .#. #..
- ... ... ### ... ...
+ ... .#. ### .#. ...
* #.# .#. ### .#. #.#
= ... ### ... ### ...
_ ... ... ... ... ###
/ ..# ..# .#. #.. #..
\ #.. #.. .#. ..# ..#
( ..# .#. .#. .#. ..#
) #.. .#. .#. .#. #..
[ ### #.. #.. #.. ###
] ### ..# ..# ..# ###
< ..# .#. #.. .#. ..#
> #.. .#. ..# .#. #..
! .#. .#. .#. ... .#.
? ### ..# .#. ... .#.
' .#. .#. ... ... ...
" #.# #.# ... ... ...
# #.# ### #.# ### #.#
% #.# ..# .#. #.. #.#
$ .## ##. .#. .## ##.
& ##. ##. ##. #.# ###
@ ### #.# ### #.. .##
]]

local GLYPH_W, GLYPH_H = 3, 5
local ADVANCE = GLYPH_W + 1
local LINE_H  = GLYPH_H + 1

-- Parsed once into { character = { row bitmasks } }, high bit leftmost.
local GLYPHS = {}
for line in FONT_SOURCE:gmatch("[^\n]+") do
	local character = line:sub(1, 1)
	local rows = {}
	for group in line:sub(3):gmatch("%S+") do
		local mask = 0
		for column = 1, GLYPH_W do
			if group:sub(column, column) == "#" then
				mask = bor(mask, lshift(1, GLYPH_W - column))
			end
		end
		rows[#rows + 1] = mask
	end
	if #rows == GLYPH_H then
		GLYPHS[character] = rows
	end
end

--- Text at a game pixel, `scale` pixels per font pixel (1 by default). Drawn
--- with a one-pixel shadow, because an overlay has no say in what is behind it
--- and a thin glyph over a bright level is otherwise unreadable.
function view:text(gx, gy, colour, text, scale)
	scale = floor(scale or 1)
	if scale < 1 then
		scale = 1
	end

	local shadow = 0xC0000000 -- black at 75%, in ImGui's RGBA byte order

	local pen = floor(gx)
	for i = 1, #text do
		local character = text:sub(i, i):upper()
		local glyph = GLYPHS[character]

		if glyph then
			for row = 1, GLYPH_H do
				local mask = glyph[row]
				for column = 0, GLYPH_W - 1 do
					if band(mask, lshift(1, GLYPH_W - 1 - column)) ~= 0 then
						local x = pen + column * scale
						local y = floor(gy) + (row - 1) * scale
						self:filled(x + scale, y + scale,
							x + scale * 2, y + scale * 2, shadow)
						self:filled(x, y, x + scale, y + scale, colour)
					end
				end
			end
		end

		pen = pen + ADVANCE * scale
	end
end

--- How wide that text will be, in game pixels. Exact: the font is fixed width.
function view:text_width(text, scale)
	scale = floor(scale or 1)
	if scale < 1 then
		scale = 1
	end
	if #text == 0 then
		return 0
	end
	return (#text * ADVANCE - 1) * scale
end

--- How tall one line is, in game pixels.
function view:text_height(scale)
	return LINE_H * (floor(scale or 1))
end

--- Text ending at a game pixel, for anything pinned to the right edge.
function view:text_right(gx, gy, colour, text, scale)
	self:text(gx - self:text_width(text, scale), gy, colour, text, scale)
end

--- Game pixel (gx, gy) as a screen position, for the rare overlay that wants
--- to reach past the canvas and draw at UI resolution through view.draw_list.
function view:pos(gx, gy)
	return ig.ImVec2(self.x + gx * self.scale, self.y + gy * self.scale)
end

-- -------------------------------------------------------------- the registry

--- Registers an overlay.
--
--   overlay.register{
--     id      = "cnes.smb.hitboxes",
--     title   = "SMB hitboxes",
--     group   = "cNES",             -- menu grouping, optional
--     enabled = false,              -- on at startup?
--     draw    = function(view) ... end
--   }
function overlay.register(def)
	assert(type(def) == "table", "overlay.register expects a table")
	assert(type(def.id) == "string", "overlay.register: id must be a string")
	assert(type(def.draw) == "function", "overlay.register: draw must be a function")

	if by_id[def.id] then
		-- Re-registering replaces the definition so a script can be reloaded
		-- without restarting the frontend.
		local existing = by_id[def.id]
		existing.title  = def.title or existing.title
		existing.group  = def.group or existing.group
		existing.draw   = def.draw
		existing.failed = nil
		return existing
	end

	local entry = {
		id      = def.id,
		title   = def.title or def.id,
		group   = def.group or "Overlays",
		draw    = def.draw,
		enabled = ig.bool(def.enabled and true or false),
	}

	overlays[#overlays + 1] = entry
	by_id[def.id] = entry
	return entry
end

function overlay.get(id)
	return by_id[id]
end

function overlay.show(id, visible)
	local entry = by_id[id]
	if entry then
		entry.enabled[0] = visible ~= false
	end
end

function overlay.count()
	return #overlays
end

--- Every overlay's id and state, for listing from the console.
function overlay.list()
	local out = {}
	for index, entry in ipairs(overlays) do
		out[index] = {
			id      = entry.id,
			title   = entry.title,
			group   = entry.group,
			enabled = entry.enabled[0],
			failed  = entry.failed,
		}
	end
	return out
end

-- Menu entries, grouped, the same shape as the panel menu.
function overlay.menu()
	local groups, order = {}, {}
	for _, entry in ipairs(overlays) do
		if not groups[entry.group] then
			groups[entry.group] = {}
			order[#order + 1] = entry.group
		end
		local g = groups[entry.group]
		g[#g + 1] = entry
	end

	for _, name in ipairs(order) do
		if ig.BeginMenu(name, true) then
			for _, entry in ipairs(groups[name]) do
				local label = entry.failed and (entry.title .. "  (errored)") or entry.title
				if ig.MenuItem_Bool(label, nil, entry.enabled[0], not entry.failed) then
					entry.enabled[0] = not entry.enabled[0]
				end
			end
			ig.EndMenu()
		end
	end
end

local function report(entry, err)
	entry.failed = tostring(err)
	entry.enabled[0] = false
	print(string.format("[mse.overlay] overlay '%s' errored and was disabled: %s",
		entry.id, entry.failed))
end

--- The canvas as it stands: the RGBA buffer, its width and its height, or nil
--- before anything has been drawn. For tests, and for an overlay that wants to
--- read back what an earlier one put down.
function overlay.pixels()
	if canvas.pixels == nil then
		return nil
	end
	return canvas.pixels, canvas.width, canvas.height
end

--- Draws every enabled overlay. Called by the frontend from inside the
--- emulation window, with the picture's screen rect and its own resolution.
function overlay.draw(x, y, w, h, pixels_x, pixels_y)
	if #overlays == 0 or pixels_x <= 0 or pixels_y <= 0 or w <= 0 or h <= 0 then
		return
	end

	local any = false
	for _, entry in ipairs(overlays) do
		if entry.enabled[0] and not entry.failed then
			any = true
			break
		end
	end
	if not any then
		-- Nothing on: no clear, no upload, no draw call. An overlay system
		-- nobody is using should not cost a texture upload every frame.
		return
	end

	canvas_resize(pixels_x, pixels_y)

	view.x, view.y, view.w, view.h = x, y, w, h
	view.pixels_x, view.pixels_y   = pixels_x, pixels_y
	view.scale                     = w / pixels_x
	view.draw_list                 = ig.GetWindowDrawList()

	ffi.fill(canvas.pixels, canvas.bytes, 0)
	canvas.dirty = false

	for _, entry in ipairs(overlays) do
		if entry.enabled[0] and not entry.failed then
			local ok, err = pcall(entry.draw, view)
			if not ok then
				report(entry, err)
			end
		end
	end

	if not canvas.dirty then
		return
	end

	canvas.texture:upload(canvas.pixels)

	-- Nearest, and over the picture's own rect, so one canvas pixel is exactly
	-- one game pixel however the window is sized. Sampling it linearly would
	-- put a soft grey halo around every hitbox.
	local draw_list = view.draw_list
	local platform  = ig.GetPlatformIO_Nil()

	draw_list:AddCallback(platform.DrawCallback_SetSamplerNearest, nil, 0)
	draw_list:AddImage(canvas.texture:ref(), ig.ImVec2(x, y), ig.ImVec2(x + w, y + h),
		ig.ImVec2(0, 0), ig.ImVec2(1, 1), 0xFFFFFFFF)
	draw_list:AddCallback(platform.DrawCallback_SetSamplerLinear, nil, 0)
end

return overlay
