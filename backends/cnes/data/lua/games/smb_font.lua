-- Super Mario Bros.'s own HUD font, read out of the cartridge.
--
-- The status bar font lives in the second pattern table: $00-$09 are the
-- digits, $0A-$23 the alphabet, $24 a blank. Reading the tiles rather than
-- redrawing them means an overlay's text is made of the same pixels as the
-- game's, which is the whole point of drawing into a framebuffer-sized image in
-- the first place.
--
-- The font has no punctuation beyond a dash, so ':' and '.' are drawn here. They
-- are built to the same two-pixel stroke weight as the cartridge glyphs, so a
-- clock reads as one piece of text rather than two fonts spliced together.

local ffi = require("ffi")
local dbg = require("ui.debug")

local band, rshift, lshift, bor = bit.band, bit.rshift, bit.lshift, bit.bor

local M = {}

M.CELL  = 8 -- the tile; the glyphs inside it are 7 wide and 7 tall
M.WIDTH = 8

local FONT_TABLE = 0x1000

-- character -> tile index. The alphabet runs in order from $0A, so only the
-- start of each run is worth writing down.
local TILES = { [" "] = 0x24, ["-"] = 0x28 }
for i = 0, 9 do
	TILES[tostring(i)] = i
end
for i = 0, 25 do
	TILES[string.char(65 + i)] = 0x0A + i
end

-- Drawn rather than read: see above. One string per row, '#' for ink.
local SYNTHETIC = {
	["."] = { "........", "........", "........", "........", "........", "..##....", "..##....", "........" },
	[":"] = { "........", "..##....", "..##....", "........", "..##....", "..##....", "........", "........" },
	["/"] = { "......##", ".....##.", "....##..", "...##...", "..##....", ".##.....", "##......", "........" },
	["+"] = { "........", "...##...", "...##...", ".######.", "...##...", "...##...", "........", "........" },
	["("] = { "....##..", "...##...", "..##....", "..##....", "..##....", "...##...", "....##..", "........" },
	[")"] = { "..##....", "...##...", "....##..", "....##..", "....##..", "...##...", "..##....", "........" },
}

-- character -> eight row bitmasks, bit 7 leftmost. Built once per cartridge.
local glyphs = nil

local function synthesise()
	local out = {}
	for character, rows in pairs(SYNTHETIC) do
		local masks = {}
		for row = 1, 8 do
			local mask = 0
			for column = 1, 8 do
				if rows[row]:sub(column, column) == "#" then
					mask = bor(mask, lshift(1, 8 - column))
				end
			end
			masks[row] = mask
		end
		out[character] = masks
	end
	return out
end

-- The font is CHR ROM on this cartridge, so one read covers the whole session.
-- It is read through the debug API rather than from the file because the
-- frontend has no idea where the file was.
local function build()
	if glyphs ~= nil then
		return glyphs
	end

	local built = synthesise()

	for character, tile in pairs(TILES) do
		local data, got = dbg.read(dbg.SPACE.CHR, FONT_TABLE + tile * 16, 16)
		if got < 16 then
			return nil -- no cartridge yet; try again next frame
		end

		local masks = {}
		for row = 1, 8 do
			-- Two bitplanes, eight bytes apart. Any non-zero colour index is
			-- ink: the HUD font is a single colour and which one is a detail of
			-- the palette, not of the glyph.
			masks[row] = bor(data[row - 1], data[row + 7])
		end
		built[character] = masks
	end

	glyphs = built
	return glyphs
end

--- Drops the cached font. Call when the cartridge changes.
function M.invalidate()
	glyphs = nil
end

function M.width(text, scale)
	return #text * M.WIDTH * (scale or 1)
end

function M.height(scale)
	return M.CELL * (scale or 1)
end

--- Draws text into an overlay view at a game pixel, `scale` pixels per font
--- pixel. Unknown characters are drawn as blanks rather than dropped, so a
--- column of numbers stays a column.
function M.text(view, gx, gy, colour, text, scale)
	local font = build()
	if font == nil then
		return
	end

	scale = scale or 1
	local pen = gx

	for i = 1, #text do
		local masks = font[text:sub(i, i):upper()]
		if masks then
			for row = 1, 8 do
				local mask = masks[row]
				if mask ~= 0 then
					for column = 0, 7 do
						if band(mask, lshift(1, 7 - column)) ~= 0 then
							local x = pen + column * scale
							local y = gy + (row - 1) * scale
							view:filled(x, y, x + scale, y + scale, colour)
						end
					end
				end
			end
		end
		pen = pen + M.WIDTH * scale
	end
end

return M
