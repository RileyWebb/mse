-- cNES PPU panel: registers, palette, OAM and the nametables.
--
-- Most AccuracyCoin failures in this emulator are PPU behaviour, so this leans
-- towards showing the state those tests care about: where the PPU is on the
-- frame, what v/t/x/w hold, and what is actually in the tables.

local ig = require("mse.imgui")
local ui = require("mse.ui")
local tex = require("mse.texture")
local dbg = require("ui.debug")

local DIM  = ig.ImVec4(0.55, 0.55, 0.55, 1.0)
local VAL  = ig.ImVec4(0.55, 0.78, 1.0, 1.0)
local ON   = ig.ImVec4(0.45, 0.95, 0.5, 1.0)
local OFF  = ig.ImVec4(0.45, 0.45, 0.45, 1.0)

local MIRRORING = { [0] = "horizontal", "vertical", "single-screen A",
                    "single-screen B", "four-screen" }

local function field(label, value)
	ig.TextColored(DIM, label)
	ig.SameLine(0, 6)
	ig.TextColored(VAL, value)
end

local function flag(label, set)
	ig.TextColored(set and ON or OFF, label)
end

local function draw_position(state)
	local ppu = state.ppu

	local phase
	if ppu.scanline < 0 or ppu.scanline == 261 then
		phase = "pre-render"
	elseif ppu.scanline < 240 then
		phase = "visible"
	elseif ppu.scanline == 240 then
		phase = "post-render"
	else
		phase = "vblank"
	end

	field("scanline", string.format("%3d", ppu.scanline))
	ig.SameLine(0, 16); field("dot", string.format("%3d", ppu.dot))
	ig.SameLine(0, 16); field("frame", tostring(tonumber(ppu.frame)))
	ig.SameLine(0, 16); ig.TextColored(DIM, "(" .. phase .. ")")
end

-- The register view: every PPU register broken out a bit at a time, with the
-- address each bit lives at.
--
-- Laid out as address / name / value / hex because that is the order the
-- question usually arrives in -- something wrote $2000, what did it mean --
-- and because a raw $%02X of PPUCTRL tells you nothing without decoding it by
-- hand every time.

local GROUP_BG  = ig.ImVec4(0.20, 0.20, 0.24, 1.0)
local ADDR_COL  = ig.ImVec4(0.72, 0.62, 0.98, 1.0)
local NAME_COL  = ig.ImVec4(0.86, 0.86, 0.90, 1.0)

local checkbox_scratch = ig.bool(false)

local function register_group(label, address)
	ig.TableNextRow(0, 0)
	ig.TableSetBgColor(ig.lib.ImGuiTableBgTarget_RowBg0, ig.U32(0.20, 0.20, 0.24, 1.0), -1)

	ig.TableSetColumnIndex(0)
	if address then ig.TextColored(ADDR_COL, address) end
	ig.TableSetColumnIndex(1)
	ig.TextColored(NAME_COL, label)
end

--- One row. `value` is shown as-is; `hex` is optional and omitted for booleans,
--- which get a (disabled) checkbox instead so the column scans at a glance.
local function register_row(address, name, value, hex)
	ig.TableNextRow(0, 0)

	ig.TableSetColumnIndex(0)
	if address then ig.TextColored(DIM, address) end

	ig.TableSetColumnIndex(1)
	ig.Text(name)

	ig.TableSetColumnIndex(2)
	if type(value) == "boolean" then
		-- Disabled so it reads as a state rather than a control: none of this
		-- is writable, and a checkbox that ignores clicks is worse than one
		-- that is visibly inert.
		checkbox_scratch[0] = value
		ig.BeginDisabled(true)
		ig.Checkbox("##" .. name, checkbox_scratch)
		ig.EndDisabled()
		ig.SameLine(0, 4)
		ig.TextColored(value and ON or OFF, value and "true" or "false")
	else
		ig.TextColored(VAL, value)
	end

	ig.TableSetColumnIndex(3)
	if hex then ig.TextColored(DIM, hex) end
end

local function draw_registers(state)
	local ppu   = state.ppu
	local ctrl  = ppu.ctrl
	local mask  = ppu.mask
	local status = ppu.status

	local function bit_set(value, index)
		return bit.band(value, bit.lshift(1, index)) ~= 0
	end

	local flags = bit.bor(ig.lib.ImGuiTableFlags_Borders,
		ig.lib.ImGuiTableFlags_RowBg,
		ig.lib.ImGuiTableFlags_ScrollY,
		ig.lib.ImGuiTableFlags_SizingFixedFit,
		ig.lib.ImGuiTableFlags_Resizable)

	if not ig.BeginTable("cnes_ppu_registers", 4, flags, ig.ImVec2(0, 0), 0) then
		return
	end

	ig.TableSetupColumn("Address", ig.lib.ImGuiTableColumnFlags_WidthFixed, 90, 0)
	ig.TableSetupColumn("Name", ig.lib.ImGuiTableColumnFlags_WidthStretch, 0, 0)
	ig.TableSetupColumn("Value", ig.lib.ImGuiTableColumnFlags_WidthFixed, 130, 0)
	ig.TableSetupColumn("Value (Hex)", ig.lib.ImGuiTableColumnFlags_WidthFixed, 90, 0)
	ig.TableSetupScrollFreeze(0, 1)
	ig.TableHeadersRow()

	register_group("State", nil)
	register_row(nil, "Cycle (H)", string.format("%d", ppu.dot), string.format("$%02X", ppu.dot))
	register_row(nil, "Scanline (V)", string.format("%d", ppu.scanline),
		string.format("$%02X", bit.band(ppu.scanline, 0xFF)))
	register_row(nil, "Frame Number", string.format("%d", tonumber(ppu.frame)), nil)
	register_row(nil, "PPU Bus Address", string.format("%d", ppu.v), string.format("$%04X", ppu.v))
	register_row(nil, "PPU Register Buffer", string.format("%d", ppu.data_buffer),
		string.format("$%02X", ppu.data_buffer))
	register_row(nil, "Mirroring",
		MIRRORING[tonumber(state.mirroring)] or tostring(state.mirroring), nil)

	register_group("Control", "$2000")
	local nametable = bit.band(ctrl, 0x03)
	register_row("$2000.0-1", "Nametable", string.format("%d", nametable),
		string.format("$%04X", 0x2000 + nametable * 0x400))
	register_row("$2000.2", "Increment Mode", bit_set(ctrl, 2) and "32 bytes" or "1 byte",
		string.format("$%02X", bit_set(ctrl, 2) and 0x20 or 0x01))
	register_row("$2000.3", "Sprite Table Address", bit_set(ctrl, 3) and "$1000" or "$0000",
		bit_set(ctrl, 3) and "$1000" or "$0000")
	register_row("$2000.4", "BG Table Address", bit_set(ctrl, 4) and "$1000" or "$0000",
		bit_set(ctrl, 4) and "$1000" or "$0000")
	register_row("$2000.5", "Sprite Size", bit_set(ctrl, 5) and "8x16" or "8x8",
		string.format("$%02X", bit_set(ctrl, 5) and 0x01 or 0x00))
	register_row("$2000.6", "Main/secondary PPU select", bit_set(ctrl, 6) and "Secondary" or "Main",
		string.format("$%02X", bit_set(ctrl, 6) and 0x01 or 0x00))
	register_row("$2000.7", "NMI enabled", bit_set(ctrl, 7), nil)

	register_group("Mask", "$2001")
	register_row("$2001.0", "Grayscale", bit_set(mask, 0), nil)
	register_row("$2001.1", "BG - Show leftmost 8 pixels", bit_set(mask, 1), nil)
	register_row("$2001.2", "Sprites - Show leftmost 8 pixels", bit_set(mask, 2), nil)
	register_row("$2001.3", "Background enabled", bit_set(mask, 3), nil)
	register_row("$2001.4", "Sprites enabled", bit_set(mask, 4), nil)
	register_row("$2001.5", "Red emphasis", bit_set(mask, 5), nil)
	register_row("$2001.6", "Green emphasis", bit_set(mask, 6), nil)
	register_row("$2001.7", "Blue emphasis", bit_set(mask, 7), nil)

	register_group("Status", "$2002")
	register_row("$2002.5", "Sprite overflow", bit_set(status, 5), nil)
	register_row("$2002.6", "Sprite 0 hit", bit_set(status, 6), nil)
	register_row("$2002.7", "Vertical blank", bit_set(status, 7), nil)

	register_group("OAM address", "$2003")
	register_row(nil, "OAM address", string.format("%d", ppu.oam_addr),
		string.format("$%02X", ppu.oam_addr))

	register_group("VRAM Address / Scrolling", "$2005-2006")
	register_row(nil, "VRAM Address", string.format("%d", ppu.v), string.format("$%04X", ppu.v))
	register_row(nil, "T", string.format("%d", ppu.t), string.format("$%04X", ppu.t))
	register_row(nil, "X Scroll", string.format("%d", ppu.x), string.format("$%02X", ppu.x))
	register_row(nil, "Write Toggle", ppu.w ~= 0, nil)

	ig.EndTable()
end

local function draw_palette()
	local data, got = dbg.read(dbg.SPACE.PALETTE, 0, 32)
	if got < 32 then
		ig.TextDisabled("palette unavailable")
		return
	end

	local draw_list = ig.GetWindowDrawList()
	local swatch = 18

	for row = 0, 1 do
		ig.TextColored(DIM, row == 0 and "BG " or "SPR")
		for col = 0, 15 do
			ig.SameLine(0, col % 4 == 0 and 8 or 2)
			local index = row * 16 + col

			local pos = ig.GetCursorScreenPos()
			-- The index is what matters for debugging; the swatch is drawn from
			-- it directly rather than through the active palette, so emphasis
			-- and greyscale do not disguise what is stored.
			local entry = data[index]
			local shade = (bit.band(entry, 0x0F)) / 15.0
			local tint = bit.rshift(bit.band(entry, 0x30), 4) / 3.0
			local col32 = ig.U32(shade, shade * 0.6 + tint * 0.4, tint, 1.0)

			-- ImDrawList functions are bound as methods on the struct, not as
			-- ig.ImDrawList_* on the module.
			draw_list:AddRectFilled(pos,
				ig.ImVec2(pos.x + swatch, pos.y + swatch), col32, 2.0, 0)
			ig.Dummy(ig.ImVec2(swatch, swatch))

			if ig.IsItemHovered(0) then
				ig.SetTooltip(string.format("$3F%02X = $%02X", index, entry))
			end
		end
	end
end

-- Textures for the rasterised views. Created on first use rather than at load,
-- because this file is run before ImGui has a context in the headless tests.
local pattern_textures = {}
local sprite_texture

local function pattern_texture(index)
	if not pattern_textures[index] then
		pattern_textures[index] = tex.new(dbg.PATTERN_DIM, dbg.PATTERN_DIM)
	end
	return pattern_textures[index]
end

--- Draws a texture with nearest-neighbour sampling.
--
-- ImGui samples linearly, which turns an 8x8 tile at 3x into mush -- and these
-- views exist to show you individual pixels. The renderer backend publishes a
-- pair of draw callbacks to switch the sampler and back; the emulator's own
-- video output goes through the same two.
--
-- Wrapping each image rather than a whole run of them is deliberate: the
-- callbacks apply to everything drawn between them, and the OAM table has text
-- interleaved with its thumbnails. Nearest-sampled font glyphs look worse than
-- the extra draw calls cost.
local function image_nearest(texture, size, uv0, uv1)
	local draw_list = ig.GetWindowDrawList()
	local platform  = ig.GetPlatformIO_Nil()

	draw_list:AddCallback(platform.DrawCallback_SetSamplerNearest, nil, 0)
	ig.Image(texture:ref(), size, uv0, uv1)
	draw_list:AddCallback(platform.DrawCallback_SetSamplerLinear, nil, 0)
end

-- A checkerboard under anything with transparency, so a black sprite is still
-- a sprite rather than a hole.
local function draw_checkerboard(pos, size, cell)
	local draw_list = ig.GetWindowDrawList()
	local dark  = ig.U32(0.16, 0.16, 0.18, 1.0)
	local light = ig.U32(0.22, 0.22, 0.25, 1.0)

	draw_list:AddRectFilled(pos, ig.ImVec2(pos.x + size.x, pos.y + size.y), dark, 0, 0)

	local rows = math.ceil(size.y / cell)
	local cols = math.ceil(size.x / cell)
	for row = 0, rows - 1 do
		for col = 0, cols - 1 do
			if (row + col) % 2 == 1 then
				local x = pos.x + col * cell
				local y = pos.y + row * cell
				draw_list:AddRectFilled(ig.ImVec2(x, y),
					ig.ImVec2(math.min(x + cell, pos.x + size.x),
					          math.min(y + cell, pos.y + size.y)), light, 0, 0)
			end
		end
	end
end

local pattern_palette = ig.int(0)
local pattern_zoom    = ig.int(2)

local function draw_pattern_tables(state)
	ig.TextColored(DIM, "Palette")
	ig.SameLine(0, 6)
	ig.PushItemWidth(150)
	ig.SliderInt("##pattern_palette", pattern_palette, 0, 7,
		pattern_palette[0] < 4 and "background %d" or "sprite %d", 0)
	ig.PopItemWidth()

	ig.SameLine(0, 14)
	ig.TextColored(DIM, "Zoom")
	ig.SameLine(0, 6)
	ig.PushItemWidth(110)
	ig.SliderInt("##pattern_zoom", pattern_zoom, 1, 4, "%dx", 0)
	ig.PopItemWidth()

	-- Which table the PPU is currently fetching from, so the view can say which
	-- of the two is the one being used right now.
	local bg_table  = bit.band(state.ppu.ctrl, 0x10) ~= 0 and 1 or 0
	local spr_table = bit.band(state.ppu.ctrl, 0x08) ~= 0 and 1 or 0

	ig.Separator()

	local dim   = dbg.PATTERN_DIM
	local scale = pattern_zoom[0]
	local size  = ig.ImVec2(dim * scale, dim * scale)

	for index = 0, 1 do
		local pixels = dbg.render_pattern_table(index, pattern_palette[0])
		if pixels == nil then
			ig.TextDisabled(string.format("pattern table %d unavailable", index))
		else
			local texture = pattern_texture(index)
			texture:upload(pixels)

			if index == 1 then
				ig.SameLine(0, 12)
			end

			ig.BeginGroup()

			local uses = {}
			if bg_table == index  then uses[#uses + 1] = "BG" end
			if spr_table == index then uses[#uses + 1] = "SPR" end
			ig.TextColored(DIM, string.format("$%04X", index * 0x1000))
			if #uses > 0 then
				ig.SameLine(0, 6)
				ig.TextColored(ON, table.concat(uses, "+"))
			end

			local pos = ig.GetCursorScreenPos()
			image_nearest(texture, size, ig.ImVec2(0, 0), ig.ImVec2(1, 1))

			-- Grid over the tile boundaries, and the tile under the pointer.
			local draw_list = ig.GetWindowDrawList()
			if scale >= 2 then
				local grid = ig.U32(1, 1, 1, 0.10)
				for line = 1, 15 do
					local offset = line * 8 * scale
					draw_list:AddLine(ig.ImVec2(pos.x + offset, pos.y),
						ig.ImVec2(pos.x + offset, pos.y + size.y), grid, 1.0)
					draw_list:AddLine(ig.ImVec2(pos.x, pos.y + offset),
						ig.ImVec2(pos.x + size.x, pos.y + offset), grid, 1.0)
				end
			end

			if ig.IsItemHovered(0) then
				local mouse = ig.GetIO().MousePos
				local col = math.floor((mouse.x - pos.x) / (8 * scale))
				local row = math.floor((mouse.y - pos.y) / (8 * scale))
				if col >= 0 and col < 16 and row >= 0 and row < 16 then
					local tile = row * 16 + col
					draw_list:AddRect(
						ig.ImVec2(pos.x + col * 8 * scale, pos.y + row * 8 * scale),
						ig.ImVec2(pos.x + (col + 1) * 8 * scale, pos.y + (row + 1) * 8 * scale),
						ig.U32(1, 0.85, 0.35, 0.9), 0, 0, 1.0)

					ig.SetTooltip(string.format("tile $%02X   address $%04X",
						tile, index * 0x1000 + tile * 16))
				end
			end

			ig.EndGroup()
		end
	end
end

local function draw_oam()
	local data, got = dbg.read(dbg.SPACE.OAM, 0, 256)
	if got < 256 then
		ig.TextDisabled("OAM unavailable")
		return
	end

	-- One texture holding all 64 sprites; each row samples its own cell out of
	-- it with UVs, so the table costs one upload rather than 64.
	if not sprite_texture then
		sprite_texture = tex.new(dbg.SPRITE_WIDTH, dbg.SPRITE_HEIGHT)
	end

	local pixels = dbg.render_sprites()
	if pixels ~= nil then
		sprite_texture:upload(pixels)
	end

	local tall = bit.band(dbg.state().ppu.ctrl, 0x20) ~= 0
	local cell_w, cell_h = 8, tall and 16 or 8
	local zoom = 3

	local flags = bit.bor(ig.lib.ImGuiTableFlags_Borders,
		ig.lib.ImGuiTableFlags_RowBg,
		ig.lib.ImGuiTableFlags_ScrollY)

	if ig.BeginTable("cnes_oam", 6, flags, ig.ImVec2(0, 320), 0) then
		ig.TableSetupColumn("#", 0, 0, 0)
		ig.TableSetupColumn("", ig.lib.ImGuiTableColumnFlags_WidthFixed, cell_w * zoom, 0)
		ig.TableSetupColumn("Y", 0, 0, 0)
		ig.TableSetupColumn("Tile", 0, 0, 0)
		ig.TableSetupColumn("Attr", 0, 0, 0)
		ig.TableSetupColumn("X", 0, 0, 0)
		ig.TableHeadersRow()

		for sprite = 0, 63 do
			local base = sprite * 4
			local y = data[base]
			local attribute = data[base + 2]

			ig.TableNextRow(0, cell_h * zoom)
			ig.TableSetColumnIndex(0)
			-- Y >= $EF puts the sprite off the bottom of the screen; those are
			-- how games park unused entries, so dim them.
			local parked = y >= 0xEF
			ig.TextColored(parked and OFF or VAL, tostring(sprite))

			ig.TableSetColumnIndex(1)
			if pixels ~= nil then
				-- The cell this sprite occupies in the sheet, as UVs.
				local cx = (sprite % dbg.SPRITE_COLUMNS) * 8
				local cy = math.floor(sprite / dbg.SPRITE_COLUMNS) * 16
				local uv0 = ig.ImVec2(cx / dbg.SPRITE_WIDTH, cy / dbg.SPRITE_HEIGHT)
				local uv1 = ig.ImVec2((cx + cell_w) / dbg.SPRITE_WIDTH,
				                      (cy + cell_h) / dbg.SPRITE_HEIGHT)

				local size = ig.ImVec2(cell_w * zoom, cell_h * zoom)
				local pos  = ig.GetCursorScreenPos()
				draw_checkerboard(pos, size, 4)
				image_nearest(sprite_texture, size, uv0, uv1)
			end

			ig.TableSetColumnIndex(2); ig.Text(string.format("%3d", y))
			ig.TableSetColumnIndex(3); ig.Text(string.format("$%02X", data[base + 1]))
			ig.TableSetColumnIndex(4)
			ig.Text(string.format("$%02X", attribute))
			if ig.IsItemHovered(0) then
				ig.SetTooltip(string.format("palette %d%s%s  %s",
					bit.band(attribute, 0x03),
					bit.band(attribute, 0x40) ~= 0 and "  flip-H" or "",
					bit.band(attribute, 0x80) ~= 0 and "  flip-V" or "",
					bit.band(attribute, 0x20) ~= 0 and "behind background" or "in front"))
			end
			ig.TableSetColumnIndex(5); ig.Text(string.format("%3d", data[base + 3]))
		end

		ig.EndTable()
	end
end

local NT_TILES = 0
local NT_IDS   = 1

local nt_mode = ig.int(NT_TILES)
local nt_grid = ig.bool(true)

local nt_textures = {}

local function nametable_texture(index)
	if not nt_textures[index] then
		nt_textures[index] = tex.new(dbg.NAMETABLE_WIDTH, dbg.NAMETABLE_HEIGHT)
	end
	return nt_textures[index]
end

-- The four nametables sit in a 2x2 arrangement on the PPU's address map, so
-- that is how they are drawn: $2000 top-left, $2400 top-right, $2800 and
-- $2C00 below. Four collapsing headers hid the one thing the layout tells you,
-- which is how a scrolling game's screens join up.
local NT_COLUMNS = 2
local NT_GAP     = 6

local function draw_nametable_ids(draw_list, data, base, x, y, width, height)
	-- One string per row rather than one per tile: 960 separate draws per
	-- nametable is a lot of work for text most of which is the same width.
	local sample = string.rep("XX ", 32)

	ig.PushFont(nil, 100)
	local measured = ig.CalcTextSize(sample, nil, false, 0)
	ig.PopFont()

	local size = measured.x > 0 and (100 * (width / measured.x)) or 0
	if size < 4 then
		-- Below this the digits are not glyphs any more, just grey mush.
		ig.PushFont(nil, 0)
		draw_list:AddText_Vec2(ig.ImVec2(x + 6, y + 6), ig.U32(0.6, 0.6, 0.6, 1),
			"too small - resize the panel", nil)
		ig.PopFont()
		return
	end

	local row_height = height / 30
	local colour     = ig.U32(0.55, 0.78, 1.0, 1.0)
	local font       = ig.GetFont()

	local cells = {}
	for row = 0, 29 do
		local offset = base + row * 32
		for col = 0, 31 do
			cells[col + 1] = string.format("%02X", data[offset + col])
		end
		draw_list:AddText_FontPtr(font, size, ig.ImVec2(x, y + row * row_height), colour,
			table.concat(cells, " "), nil, 0, nil)
	end
end

local function draw_nametables(state)
	local data, got = dbg.read(dbg.SPACE.PPU, 0x2000, 0x1000)
	if got < 0x1000 then
		ig.TextDisabled("nametables unavailable")
		return
	end

	ig.TextColored(DIM, "Show")
	ig.SameLine(0, 8)
	ig.RadioButton_IntPtr("Tiles", nt_mode, NT_TILES)
	ig.SameLine(0, 10)
	ig.RadioButton_IntPtr("IDs", nt_mode, NT_IDS)

	ig.SameLine(0, 16)
	ig.Checkbox("Grid", nt_grid)

	ig.SameLine(0, 16)
	ig.TextColored(DIM, "mirroring")
	ig.SameLine(0, 6)
	ig.TextColored(VAL, MIRRORING[tonumber(state.mirroring)] or tostring(state.mirroring))

	ig.Separator()

	-- Sized to whatever room is left, keeping 256:240 so the picture is not
	-- stretched. Both axes are considered, so a short wide panel shrinks the
	-- cells rather than running off the bottom.
	local avail = ig.GetContentRegionAvail()
	local cell_w = (avail.x - NT_GAP) / NT_COLUMNS
	local cell_h = (avail.y - NT_GAP) / 2

	local scale = math.min(cell_w / dbg.NAMETABLE_WIDTH, cell_h / dbg.NAMETABLE_HEIGHT)
	if scale <= 0 then
		return
	end

	local w = dbg.NAMETABLE_WIDTH * scale
	local h = dbg.NAMETABLE_HEIGHT * scale

	local origin = ig.GetCursorScreenPos()
	local grid_w = w * NT_COLUMNS + NT_GAP
	origin = ig.ImVec2(origin.x + math.max(0, (avail.x - grid_w) * 0.5), origin.y)

	local draw_list = ig.GetWindowDrawList()
	local active    = bit.band(state.ppu.ctrl, 0x03)
	local mouse     = ig.GetIO().MousePos

	for index = 0, 3 do
		local col = index % NT_COLUMNS
		local row = math.floor(index / NT_COLUMNS)
		local x   = origin.x + col * (w + NT_GAP)
		local y   = origin.y + row * (h + NT_GAP)
		local base = index * 0x400

		if nt_mode[0] == NT_TILES then
			local pixels = dbg.render_nametable(index)
			if pixels ~= nil then
				local texture = nametable_texture(index)
				texture:upload(pixels)
				ig.SetCursorScreenPos(ig.ImVec2(x, y))
				image_nearest(texture, ig.ImVec2(w, h), ig.ImVec2(0, 0), ig.ImVec2(1, 1))
			end
		else
			draw_list:AddRectFilled(ig.ImVec2(x, y), ig.ImVec2(x + w, y + h),
				ig.U32(0.05, 0.05, 0.07, 1), 0, 0)
			draw_nametable_ids(draw_list, data, base, x, y, w, h)
		end

		if nt_grid[0] then
			-- Attribute boundaries every 32px; the 8px tile grid only once the
			-- cells are big enough for it to be lines rather than a haze.
			if scale >= 1.5 then
				local faint = ig.U32(1, 1, 1, 0.07)
				for line = 1, 31 do
					local at = line * 8 * scale
					draw_list:AddLine(ig.ImVec2(x + at, y), ig.ImVec2(x + at, y + h), faint, 1.0)
				end
				for line = 1, 29 do
					local at = line * 8 * scale
					draw_list:AddLine(ig.ImVec2(x, y + at), ig.ImVec2(x + w, y + at), faint, 1.0)
				end
			end

			local attr = ig.U32(1, 1, 1, 0.16)
			for line = 1, 7 do
				local at = line * 32 * scale
				draw_list:AddLine(ig.ImVec2(x + at, y), ig.ImVec2(x + at, y + h), attr, 1.0)
			end
			for line = 1, 7 do
				local at = line * 32 * scale
				if at < h then
					draw_list:AddLine(ig.ImVec2(x, y + at), ig.ImVec2(x + w, y + at), attr, 1.0)
				end
			end
		end

		-- The one PPUCTRL currently points at, so the map says which screen the
		-- game thinks it is drawing.
		local is_active = index == active
		draw_list:AddRect(ig.ImVec2(x, y), ig.ImVec2(x + w, y + h),
			is_active and ig.U32(0.45, 0.95, 0.5, 0.9) or ig.U32(1, 1, 1, 0.18),
			0, 0, is_active and 2.0 or 1.0)

		-- Address in the corner, on its own chip so it stays readable over any
		-- tile underneath it.
		local label = string.format("$%04X", 0x2000 + base)
		ig.PushFont(nil, 0)
		local extent = ig.CalcTextSize(label, nil, false, 0)
		draw_list:AddRectFilled(ig.ImVec2(x + 3, y + 3),
			ig.ImVec2(x + 9 + extent.x, y + 5 + extent.y), ig.U32(0, 0, 0, 0.65), 3, 0)
		draw_list:AddText_Vec2(ig.ImVec2(x + 6, y + 4),
			is_active and ig.U32(0.45, 0.95, 0.5, 1) or ig.U32(0.75, 0.75, 0.78, 1), label, nil)
		ig.PopFont()

		if mouse.x >= x and mouse.x < x + w and mouse.y >= y and mouse.y < y + h then
			local tile_col = math.floor((mouse.x - x) / (8 * scale))
			local tile_row = math.floor((mouse.y - y) / (8 * scale))
			if tile_col >= 0 and tile_col < 32 and tile_row >= 0 and tile_row < 30 then
				local tile = data[base + tile_row * 32 + tile_col]
				local attribute = data[base + 0x3C0 +
					math.floor(tile_row / 4) * 8 + math.floor(tile_col / 4)]
				local shift = bit.bor(bit.lshift(bit.band(tile_row, 2), 1), bit.band(tile_col, 2))
				local palette = bit.band(bit.rshift(attribute, shift), 0x03)

				draw_list:AddRect(
					ig.ImVec2(x + tile_col * 8 * scale, y + tile_row * 8 * scale),
					ig.ImVec2(x + (tile_col + 1) * 8 * scale, y + (tile_row + 1) * 8 * scale),
					ig.U32(1, 0.85, 0.35, 0.9), 0, 0, 1.0)

				ig.SetTooltip(string.format(
					"tile $%02X at %d,%d\naddress $%04X\nattribute $%02X  palette %d",
					tile, tile_col, tile_row,
					0x2000 + base + tile_row * 32 + tile_col, attribute, palette))
			end
		end
	end

	ig.SetCursorScreenPos(ig.ImVec2(origin.x, origin.y + h * 2 + NT_GAP))
	ig.Dummy(ig.ImVec2(grid_w, 0))
end

ui.panel {
	id    = "cnes.ppu",
	title = "cNES PPU",
	group = "cNES",
	draw  = function()
		if not dbg.available then
			ig.TextWrapped(dbg.error or "the cNES debug API is unavailable")
			return
		end

		local state = dbg.state()
		if state == nil then
			ig.TextDisabled("No ROM running.")
			return
		end

		draw_position(state)
		ig.Separator()

		if ig.BeginTabBar("cnes_ppu_tabs", 0) then
			if ig.BeginTabItem("Registers", nil, 0) then
				draw_registers(state)
				ig.EndTabItem()
			end
			if ig.BeginTabItem("Palette", nil, 0) then
				draw_palette()
				ig.EndTabItem()
			end
			if ig.BeginTabItem("Pattern tables", nil, 0) then
				draw_pattern_tables(state)
				ig.EndTabItem()
			end
			if ig.BeginTabItem("Sprites", nil, 0) then
				draw_oam()
				ig.EndTabItem()
			end
			if ig.BeginTabItem("Nametables", nil, 0) then
				draw_nametables(state)
				ig.EndTabItem()
			end
			ig.EndTabBar()
		end
	end,
}
