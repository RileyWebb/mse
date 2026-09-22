-- cNES memory panel: a hex view over any of the emulator's address spaces.
--
-- Only the visible rows are pulled from the backend each frame. Reading takes
-- the emulator's lock, so pulling all 64KB every frame to show 20 rows would
-- slow the emulation thread for nothing.

local ig = require("mse.imgui")
local ui = require("mse.ui")
local dbg = require("ui.debug")

local SPACES = {
	{ name = "CPU bus",    space = dbg.SPACE.CPU,     digits = 4 },
	{ name = "Work RAM",   space = dbg.SPACE.RAM,     digits = 4 },
	{ name = "PPU bus",    space = dbg.SPACE.PPU,     digits = 4 },
	{ name = "OAM",        space = dbg.SPACE.OAM,     digits = 2 },
	{ name = "Palette",    space = dbg.SPACE.PALETTE, digits = 2 },
	{ name = "CHR",        space = dbg.SPACE.CHR,     digits = 6 },
	{ name = "PRG ROM",    space = dbg.SPACE.PRG,     digits = 6 },
}

local BYTES_PER_ROW = 16

local selected   = ig.int(0)
local goto_addr  = ig.int(0)
local ascii_view = ig.bool(true)

local ADDR_COL  = ig.ImVec4(0.55, 0.55, 0.55, 1.0)
local BYTE_COL  = ig.ImVec4(0.85, 0.88, 0.92, 1.0)
local ZERO_COL  = ig.ImVec4(0.38, 0.38, 0.38, 1.0)
local ASCII_COL = ig.ImVec4(0.55, 0.78, 1.0, 1.0)

local function draw_toolbar()
	ig.PushItemWidth(140)
	if ig.BeginCombo("##space", SPACES[selected[0] + 1].name, 0) then
		for i, entry in ipairs(SPACES) do
			if ig.Selectable_Bool(entry.name, selected[0] == i - 1, 0, ig.ImVec2(0, 0)) then
				selected[0] = i - 1
			end
		end
		ig.EndCombo()
	end
	ig.PopItemWidth()

	ig.SameLine(0, 12)
	ig.PushItemWidth(90)
	ig.InputInt("##goto", goto_addr, 0, 0, ig.lib.ImGuiInputTextFlags_CharsHexadecimal)
	ig.PopItemWidth()
	ig.SameLine(0, 6)
	local jump = ig.Button("Go", ig.ImVec2(0, 0))

	ig.SameLine(0, 12)
	ig.Checkbox("ASCII", ascii_view)

	return jump
end

local function printable(byte)
	if byte >= 0x20 and byte <= 0x7E then
		return string.char(byte)
	end
	return "."
end

ui.panel {
	id    = "cnes.memory",
	title = "cNES Memory",
	group = "cNES",
	draw  = function()
		if not dbg.available then
			ig.TextWrapped(dbg.error or "the cNES debug API is unavailable")
			return
		end

		if dbg.state() == nil then
			ig.TextDisabled("No ROM running.")
			return
		end

		local jump = draw_toolbar()
		local entry = SPACES[selected[0] + 1]
		local size = dbg.space_size(entry.space)

		ig.Separator()

		if size == 0 then
			ig.TextDisabled("This address space is empty for the loaded ROM.")
			return
		end

		local rows = math.ceil(size / BYTES_PER_ROW)
		local addr_fmt = "%0" .. entry.digits .. "X"

		if ig.BeginChild_Str("cnes_hex", ig.ImVec2(0, 0), 0, 0) then
			if jump then
				local row = math.floor(bit.band(goto_addr[0], 0xFFFFFF) / BYTES_PER_ROW)
				ig.SetScrollY_Float(row * ig.GetTextLineHeightWithSpacing())
			end

			-- Only the rows ImGui is about to show are read from the emulator.
			local clipper = ig.ImGuiListClipper()
			clipper:Begin(rows, -1.0)

			while clipper:Step() do
				local first = clipper.DisplayStart
				local last  = clipper.DisplayEnd
				local start_addr = first * BYTES_PER_ROW
				local count = (last - first) * BYTES_PER_ROW

				local data, got = dbg.read(entry.space, start_addr, count)

				for row = first, last - 1 do
					local base = row * BYTES_PER_ROW
					local offset = base - start_addr

					ig.TextColored(ADDR_COL, string.format(addr_fmt .. " ", base))

					local ascii = {}
					for i = 0, BYTES_PER_ROW - 1 do
						if offset + i < got then
							local byte = data[offset + i]
							ig.SameLine(0, i % 8 == 0 and 10 or 6)
							ig.TextColored(byte == 0 and ZERO_COL or BYTE_COL,
								string.format("%02X", byte))
							ascii[#ascii + 1] = printable(byte)
						end
					end

					if ascii_view[0] and #ascii > 0 then
						ig.SameLine(0, 14)
						ig.TextColored(ASCII_COL, table.concat(ascii))
					end
				end
			end

			clipper:End()
		end
		ig.EndChild()
	end,
}
