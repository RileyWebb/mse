-- cNES disassembler: a 6502 listing over the CPU bus.
--
-- The backend does the decoding (see cNES/external/cpu_debug.h) and hands back decoded
-- instructions rather than text, so this panel can colour an illegal opcode,
-- follow a branch and resolve an effective address without parsing strings
-- back apart.
--
-- Scrolling moves the address the listing starts at rather than an ImGui
-- scrollbar. 6502 instructions are one to three bytes, so there is no fixed
-- mapping from a scroll offset to an address for a clipper to use: the only
-- way to know where line N begins is to decode the N-1 lines before it. The
-- view therefore draws exactly as many instructions as fit, and the wheel and
-- the page keys move the anchor by whole instructions.

local ig  = require("mse.imgui")
local ui  = require("mse.ui")
local dbg = require("ui.debug")

local INSN = dbg.INSN

local ADDR_COL    = ig.ImVec4(0.55, 0.55, 0.55, 1.0)
local BYTES_COL   = ig.ImVec4(0.42, 0.45, 0.50, 1.0)
local CODE_COL    = ig.ImVec4(0.85, 0.88, 0.92, 1.0)
local OPERAND_COL = ig.ImVec4(0.55, 0.78, 1.00, 1.0)
local FLOW_COL    = ig.ImVec4(0.62, 0.85, 0.55, 1.0)
local ILLEGAL_COL = ig.ImVec4(1.00, 0.48, 0.45, 1.0)
local COMMENT_COL = ig.ImVec4(0.48, 0.50, 0.54, 1.0)
local PC_COL      = ig.ImVec4(1.00, 0.85, 0.35, 1.0)

local MODE_NAMES = {
	[dbg.MODE.IMP]  = "implied",
	[dbg.MODE.ACC]  = "accumulator",
	[dbg.MODE.IMM]  = "immediate",
	[dbg.MODE.ZP]   = "zero page",
	[dbg.MODE.ZPX]  = "zero page,X",
	[dbg.MODE.ZPY]  = "zero page,Y",
	[dbg.MODE.REL]  = "relative",
	[dbg.MODE.ABS]  = "absolute",
	[dbg.MODE.ABSX] = "absolute,X",
	[dbg.MODE.ABSY] = "absolute,Y",
	[dbg.MODE.IND]  = "indirect",
	[dbg.MODE.IZX]  = "(indirect,X)",
	[dbg.MODE.IZY]  = "(indirect),Y",
}

local ZERO     = ig.ImVec2(0, 0)
local ROW_SIZE = ig.ImVec2(0, 0)

local ROW_FLAGS = bit.bor(
	ig.lib.ImGuiSelectableFlags_SpanAllColumns,
	ig.lib.ImGuiSelectableFlags_AllowDoubleClick,
	ig.lib.ImGuiSelectableFlags_AllowOverlap)

local TABLE_FLAGS = bit.bor(
	ig.lib.ImGuiTableFlags_SizingFixedFit,
	ig.lib.ImGuiTableFlags_RowBg,
	ig.lib.ImGuiTableFlags_NoSavedSettings)

-- The listing does its own scrolling, so the child must not also scroll: a
-- scrollbar here would move pixels while the wheel moves addresses, and the
-- two would fight each other.
local CHILD_FLAGS = bit.bor(
	ig.lib.ImGuiWindowFlags_NoScrollbar,
	ig.lib.ImGuiWindowFlags_NoScrollWithMouse)

local GOTO_FLAGS = bit.bor(
	ig.lib.ImGuiInputTextFlags_CharsHexadecimal,
	ig.lib.ImGuiInputTextFlags_EnterReturnsTrue)

local follow_pc  = ig.bool(true)
local show_bytes = ig.bool(true)
local goto_input = ig.int(0x8000)

-- Address of the first line on screen.
local anchor = 0x8000

-- Where the view was before each jump, so following a JSR can be undone.
local back, forward = {}, {}
local HISTORY_MAX = 64

-- Fractions of a wheel notch, carried between frames; see handle_input.
local wheel_accum = 0

local selected = -1

--- The address `n` instructions after `address`.
local function advance(address, n)
	if n <= 0 then
		return address
	end

	local insns, got = dbg.disassemble_range(address, n + 1)
	if got == 0 then
		return address
	end

	-- Short at the top of the address space: stop on the last real line
	-- rather than wrapping round to $0000.
	return insns[math.min(n, got - 1)].address
end

--- Scrolls the listing by whole instructions. Negative scrolls backwards.
local function move(n)
	if n > 0 then
		anchor = advance(anchor, n)
	elseif n < 0 then
		anchor = dbg.rewind(anchor, -n)
	end
end

--- Jumps the listing to an address, remembering where it came from.
local function navigate(address)
	address = bit.band(address, 0xFFFF)

	if address ~= anchor then
		back[#back + 1] = anchor
		if #back > HISTORY_MAX then
			table.remove(back, 1)
		end
		forward = {}
	end

	anchor = address
	goto_input[0] = address
	-- A deliberate jump and "follow the PC" are contradictory instructions.
	-- The explicit one wins, or the view would snap straight back.
	follow_pc[0] = false
end

local function go_back()
	local previous = table.remove(back)
	if previous then
		forward[#forward + 1] = anchor
		anchor = previous
		goto_input[0] = anchor
		follow_pc[0] = false
	end
end

local function go_forward()
	local next_address = table.remove(forward)
	if next_address then
		back[#back + 1] = anchor
		anchor = next_address
		goto_input[0] = anchor
		follow_pc[0] = false
	end
end

--- Jumps to one of the CPU vectors. Read on demand rather than every frame,
--- since each read takes the emulator lock.
local function go_vector(name)
	local vectors = dbg.vectors()
	if vectors and vectors[name] then
		navigate(vectors[name])
	end
end

local function draw_toolbar(state)
	ig.Checkbox("Follow PC", follow_pc)
	ig.SameLine(0, 12)

	ig.PushItemWidth(80)
	local entered = ig.InputInt("##disasm_goto", goto_input, 0, 0, GOTO_FLAGS)
	ig.PopItemWidth()
	ig.SameLine(0, 6)
	if ig.Button("Go", ZERO) or entered then
		navigate(goto_input[0])
	end

	ig.SameLine(0, 14)
	if ig.Button("PC", ZERO) then
		navigate(state.cpu.pc)
	end
	ig.SameLine(0, 6)
	if ig.Button("RESET", ZERO) then
		go_vector("reset")
	end
	ig.SameLine(0, 6)
	if ig.Button("NMI", ZERO) then
		go_vector("nmi")
	end
	ig.SameLine(0, 6)
	if ig.Button("IRQ", ZERO) then
		go_vector("irq")
	end

	ig.SameLine(0, 14)
	ig.BeginDisabled(#back == 0)
	if ig.Button("<##disasm_back", ZERO) then
		go_back()
	end
	ig.EndDisabled()
	ig.SameLine(0, 4)
	ig.BeginDisabled(#forward == 0)
	if ig.Button(">##disasm_forward", ZERO) then
		go_forward()
	end
	ig.EndDisabled()

	ig.SameLine(0, 14)
	ig.Checkbox("Bytes", show_bytes)
end

--- Mouse wheel and page keys, both measured in instructions.
local function handle_input(lines)
	if ig.IsWindowHovered(ig.lib.ImGuiHoveredFlags_ChildWindows) then
		local wheel = ig.GetIO().MouseWheel
		if wheel ~= 0 then
			-- A trackpad delivers fractions of a notch. Carrying the remainder
			-- between frames lets slow movement add up instead of rounding
			-- away to nothing every frame.
			wheel_accum = wheel_accum + wheel * 3
			local steps = wheel_accum >= 0 and math.floor(wheel_accum)
				or math.ceil(wheel_accum)
			if steps ~= 0 then
				wheel_accum = wheel_accum - steps
				move(-steps)
				follow_pc[0] = false
			end
		end
	end

	-- IsKeyPressed is global and does not know which widget has the keyboard.
	-- Without this guard, using Home or an arrow key to edit the address box
	-- would scroll the listing at the same time.
	if not ig.IsWindowFocused(ig.lib.ImGuiFocusedFlags_ChildWindows)
		or ig.IsAnyItemActive() then
		return
	end

	-- A page keeps one line of overlap, which is what makes paging through a
	-- listing readable.
	local page = math.max(1, lines - 1)
	local moved = 0

	if ig.IsKeyPressed_Bool(ig.lib.ImGuiKey_PageDown, true)  then moved = page end
	if ig.IsKeyPressed_Bool(ig.lib.ImGuiKey_PageUp, true)    then moved = -page end
	if ig.IsKeyPressed_Bool(ig.lib.ImGuiKey_DownArrow, true) then moved = 1 end
	if ig.IsKeyPressed_Bool(ig.lib.ImGuiKey_UpArrow, true)   then moved = -1 end

	if moved ~= 0 then
		move(moved)
		follow_pc[0] = false
	end
end

--- The trailing note on a line: where it goes, or what it touches.
local function comment_for(insn)
	local flags = insn.flags

	if bit.band(flags, INSN.TARGET) ~= 0 then
		local symbol = dbg.symbol(insn.target)
		return string.format("-> $%04X%s", insn.target, symbol and ("  " .. symbol) or "")
	end

	if bit.band(flags, INSN.EFFECTIVE) ~= 0 then
		local symbol = dbg.symbol(insn.effective)
		if symbol then
			return string.format("%s = $%02X", symbol, insn.value)
		end
		return string.format("[$%04X] = $%02X", insn.effective, insn.value)
	end

	return nil
end

--- Colour for the mnemonic: illegal opcodes first, then anything that sends
--- the PC somewhere other than the next line.
local function mnemonic_colour(flags)
	if bit.band(flags, INSN.ILLEGAL) ~= 0 then
		return ILLEGAL_COL
	end

	local control = bit.bor(INSN.BRANCH, INSN.JUMP, INSN.CALL, INSN.RETURN)
	if bit.band(flags, control) ~= 0 then
		return FLOW_COL
	end

	return CODE_COL
end

local function byte_text(insn)
	local bytes = {}
	for i = 0, insn.length - 1 do
		bytes[#bytes + 1] = string.format("%02X", insn.bytes[i])
	end
	return table.concat(bytes, " ")
end

local function draw_row(index, insn, pc)
	local address  = insn.address
	local flags    = insn.flags
	local is_pc    = (address == pc)
	local mnemonic = ig.ffi.string(insn.mnemonic)
	local operand  = ig.ffi.string(insn.operand)

	ig.TableNextRow()
	ig.TableSetColumnIndex(0)

	-- An invisible hit target covering the whole row, drawn first so the
	-- coloured text lands on top of it. Putting the text in the Selectable's
	-- own label would be simpler but would force the whole line to a single
	-- colour, which is most of what a listing is for.
	local origin  = ig.GetCursorPos()
	local clicked = ig.Selectable_Bool("##disasm_row" .. index,
		is_pc or selected == address, ROW_FLAGS, ROW_SIZE)
	local hovered = ig.IsItemHovered(0)
	ig.SetCursorPos(origin)

	if clicked then
		selected = address
		goto_input[0] = address
		-- Double-clicking a branch, JSR or JMP is the quickest way to read
		-- through a routine; the back button undoes it.
		if ig.IsMouseDoubleClicked(0) and bit.band(flags, INSN.TARGET) ~= 0 then
			navigate(insn.target)
		end
	end

	ig.TextColored(is_pc and PC_COL or ADDR_COL,
		string.format("%s %04X", is_pc and ">" or " ", address))

	ig.TableSetColumnIndex(1)
	if show_bytes[0] then
		ig.TextColored(BYTES_COL, byte_text(insn))
	end

	ig.TableSetColumnIndex(2)
	ig.TextColored(mnemonic_colour(flags), mnemonic)
	if operand ~= "" then
		ig.SameLine(0, 6)
		ig.TextColored(OPERAND_COL, operand)
	end

	ig.TableSetColumnIndex(3)
	local comment = comment_for(insn)
	if comment then
		ig.TextColored(COMMENT_COL, comment)
	end

	if hovered then
		local note = {
			string.format("$%04X  %s", address, byte_text(insn)),
			string.format("%s %s", mnemonic, operand),
			ig.ffi.string(insn.description),
			string.format("%s, %d byte%s, %d cycle%s",
				MODE_NAMES[insn.mode] or "unknown mode",
				insn.length, insn.length == 1 and "" or "s",
				insn.cycles, insn.cycles == 1 and "" or "s"),
		}
		if bit.band(flags, INSN.ILLEGAL) ~= 0 then
			note[#note + 1] = "undocumented opcode"
		end
		if bit.band(flags, INSN.TARGET) ~= 0 then
			note[#note + 1] = "double-click to follow"
		end
		ig.SetTooltip(table.concat(note, "\n"))
	end
end

ui.panel {
	id    = "cnes.disasm",
	title = "cNES Disassembler",
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

		draw_toolbar(state)
		ig.Separator()

		local pc = state.cpu.pc

		if ig.BeginChild_Str("cnes_disasm_view", ZERO, 0, CHILD_FLAGS) then
			local avail  = ig.GetContentRegionAvail()
			local height = ig.GetTextLineHeightWithSpacing()
			local lines  = math.max(1, math.floor(avail.y / height))

			handle_input(lines)

			local insns, got = dbg.disassemble_range(anchor, lines)

			-- Re-anchor only once the PC has actually left the view. Following
			-- it line by line would slide the listing under the reader on
			-- every instruction; this holds a routine still while stepping
			-- through it and moves only when execution really goes elsewhere.
			if follow_pc[0] then
				local visible = false
				for i = 0, got - 1 do
					if insns[i].address == pc then
						visible = true
						break
					end
				end

				if not visible then
					-- A little history above the PC reads better than pinning
					-- it to the top line.
					anchor = dbg.rewind(pc, math.floor(lines / 4))
					insns, got = dbg.disassemble_range(anchor, lines)
				end
			end

			if got == 0 then
				ig.TextDisabled("Nothing to disassemble here.")
			elseif ig.BeginTable("cnes_disasm_rows", 4, TABLE_FLAGS, ZERO, 0) then
				ig.TableSetupColumn("addr", ig.lib.ImGuiTableColumnFlags_WidthFixed, 0, 0)
				ig.TableSetupColumn("bytes", ig.lib.ImGuiTableColumnFlags_WidthFixed, 0, 0)
				ig.TableSetupColumn("code", ig.lib.ImGuiTableColumnFlags_WidthFixed, 0, 0)
				ig.TableSetupColumn("comment", ig.lib.ImGuiTableColumnFlags_WidthStretch, 0, 0)

				for i = 0, got - 1 do
					draw_row(i, insns[i], pc)
				end

				ig.EndTable()
			end
		end
		ig.EndChild()
	end,
}
