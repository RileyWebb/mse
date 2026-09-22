-- cNES CPU panel: registers, flags and a running disassembly.

local ig = require("mse.imgui")
local ui = require("mse.ui")
local dbg = require("ui.debug")

local FOLLOW_PC = ig.bool(true)
local ADDRESS   = ig.int(0x8000)
local LINES     = 24

local MONO_COL = ig.ImVec4(0.55, 0.78, 1.0, 1.0)
local DIM_COL  = ig.ImVec4(0.55, 0.55, 0.55, 1.0)
local PC_COL   = ig.ImVec4(1.0, 0.85, 0.35, 1.0)

-- The status byte, most significant bit first. An unset flag is shown in its
-- lower-case form rather than hidden, so the field keeps a fixed width and the
-- eye can track one column.
local FLAG_NAMES = { "N", "V", "U", "B", "D", "I", "Z", "C" }

local function draw_flags(status)
	for i = 1, 8 do
		local mask = bit.lshift(1, 8 - i)
		local set = bit.band(status, mask) ~= 0
		if i > 1 then ig.SameLine(0, 4) end
		ig.TextColored(set and MONO_COL or DIM_COL,
			set and FLAG_NAMES[i] or FLAG_NAMES[i]:lower())
	end
end

local function draw_registers(state)
	if ig.BeginTable("cnes_cpu_regs", 4, ig.lib.ImGuiTableFlags_SizingStretchProp) then
		local cpu = state.cpu

		local function cell(label, value)
			ig.TableNextColumn()
			ig.TextColored(DIM_COL, label)
			ig.SameLine(0, 6)
			ig.TextColored(MONO_COL, value)
		end

		ig.TableNextRow()
		cell("PC", string.format("$%04X", cpu.pc))
		cell("A", string.format("$%02X", cpu.a))
		cell("X", string.format("$%02X", cpu.x))
		cell("Y", string.format("$%02X", cpu.y))

		ig.TableNextRow()
		cell("SP", string.format("$%02X", cpu.sp))
		cell("P", string.format("$%02X", cpu.status))
		cell("CYC", string.format("%d", tonumber(cpu.cycles)))
		cell("NMI", cpu.nmi_pending ~= 0 and "pending" or "-")

		ig.EndTable()
	end

	ig.Spacing()
	ig.TextColored(DIM_COL, "Flags")
	ig.SameLine(0, 8)
	draw_flags(state.cpu.status)
end

local function draw_ppu_position(state)
	ig.TextColored(DIM_COL, "PPU")
	ig.SameLine(0, 8)
	ig.Text(string.format("scanline %3d  dot %3d  frame %d",
		state.ppu.scanline, state.ppu.dot, tonumber(state.ppu.frame)))
end

local function draw_disassembly(state)
	ig.Checkbox("Follow PC", FOLLOW_PC)
	ig.SameLine(0, 12)

	ig.PushItemWidth(90)
	if FOLLOW_PC[0] then
		ig.BeginDisabled(true)
		ig.InputInt("##addr", ADDRESS, 0, 0, ig.lib.ImGuiInputTextFlags_CharsHexadecimal)
		ig.EndDisabled()
	else
		ig.InputInt("##addr", ADDRESS, 0, 0, ig.lib.ImGuiInputTextFlags_CharsHexadecimal)
	end
	ig.PopItemWidth()

	local pc = state.cpu.pc
	local address = FOLLOW_PC[0] and pc or bit.band(ADDRESS[0], 0xFFFF)

	ig.Separator()

	if ig.BeginChild_Str("cnes_disasm", ig.ImVec2(0, 0), 0, 0) then
		for _ = 1, LINES do
			local text, size = dbg.disassemble(address)
			if size == 0 then break end

			local is_pc = (address == pc)
			ig.TextColored(is_pc and PC_COL or DIM_COL, is_pc and ">" or " ")
			ig.SameLine(0, 4)
			ig.TextColored(is_pc and PC_COL or MONO_COL,
				string.format("%04X  %s", address, text))

			address = bit.band(address + size, 0xFFFF)
		end
	end
	ig.EndChild()
end

ui.panel {
	id    = "cnes.cpu",
	title = "cNES CPU",
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

		draw_registers(state)
		draw_ppu_position(state)
		ig.Separator()
		draw_disassembly(state)
	end,
}
