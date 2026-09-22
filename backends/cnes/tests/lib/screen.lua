-- Reading the NES picture as text.
--
-- The test ROMs draw their results with an ASCII-ordered font, so the nametable
-- can be read back directly. Dumping the screen on failure is usually the
-- fastest way to see what a ROM was actually complaining about.

local screen = {}

screen.COLUMNS = 32
screen.ROWS    = 30

-- Tile-to-character mapping. ROMs do not agree on one: blargg's use plain
-- ASCII, AccuracyCoin ships its own font. Callers install the right one.
local charset = nil

-- set_charset(fn) makes fn(tile) responsible for turning a tile index into a
-- character. Pass nil to go back to the ASCII default.
function screen.set_charset(fn)
	charset = fn
end

-- Tiles outside the printable ASCII range are ROM-specific glyphs (result
-- markers, box drawing); show them as '.' rather than pretending they are text.
function screen.tile_to_char(tile)
	if charset then
		return charset(tile)
	end
	if tile >= 0x20 and tile <= 0x7E then
		return string.char(tile)
	end
	return "."
end

function screen.tile(table_index, row, col)
	return ppu_read_nametable(table_index, row * screen.COLUMNS + col)
end

function screen.row_text(table_index, row)
	local chars = {}
	local base = row * screen.COLUMNS
	for col = 0, screen.COLUMNS - 1 do
		chars[#chars + 1] = screen.tile_to_char(ppu_read_nametable(table_index, base + col))
	end
	return table.concat(chars)
end

function screen.row_hex(table_index, row)
	local parts = {}
	local base = row * screen.COLUMNS
	for col = 0, screen.COLUMNS - 1 do
		parts[#parts + 1] = string.format("%02X", ppu_read_nametable(table_index, base + col))
	end
	return table.concat(parts, " ")
end

function screen.text(table_index)
	local rows = {}
	for row = 0, screen.ROWS - 1 do
		rows[#rows + 1] = screen.row_text(table_index, row)
	end
	return rows
end

-- Prints the visible nametable with row numbers, framed so it is obvious in a
-- CTest log where the screen dump starts and ends.
function screen.dump(table_index, label)
	table_index = table_index or 0
	print(string.format("--- screen dump (nametable %d)%s ---",
		table_index, label and (" " .. label) or ""))
	for row = 0, screen.ROWS - 1 do
		print(string.format("%2d |%s|", row, screen.row_text(table_index, row)))
	end
	print("--- end screen dump ---")
end

function screen.dump_hex(table_index, first_row, last_row)
	table_index = table_index or 0
	for row = first_row or 0, last_row or (screen.ROWS - 1) do
		print(string.format("%2d %s", row, screen.row_hex(table_index, row)))
	end
end

-- Finds text drawn on screen. Returns row, col or nil.
function screen.find(table_index, needle)
	for row = 0, screen.ROWS - 1 do
		local col = screen.row_text(table_index, row):find(needle, 1, true)
		if col then
			return row, col - 1
		end
	end
	return nil
end

-- A cheap whole-frame fingerprint, useful for spotting that rendering changed
-- at all when a ROM reports nothing in memory.
function screen.framebuffer_checksum()
	local sum = 0
	for y = 0, 239 do
		for x = 0, 255 do
			sum = (sum * 31 + ppu_read_pixel(x, y)) % 4294967296
		end
	end
	return sum
end

return screen
