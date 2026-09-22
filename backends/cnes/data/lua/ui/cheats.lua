-- cNES cheats: Game Genie, Pro Action Replay and raw codes.
--
-- The list lives in the core (cNES/cheats.h) and is reached through
-- cNES/external/cheat_api.h, which does the locking. Codes are answered on the
-- bus as reads go past, so nothing here has to know about banks or timing.
-- This panel is a view onto that list.

local ig  = require("mse.imgui")
local ui  = require("mse.ui")
local dbg = require("ui.debug")

local DIM     = ig.ImVec4(0.55, 0.55, 0.55, 1.0)
local VAL     = ig.ImVec4(0.55, 0.78, 1.0, 1.0)
local OK_COL  = ig.ImVec4(0.45, 0.95, 0.50, 1.0)
local BAD_COL = ig.ImVec4(1.00, 0.48, 0.45, 1.0)

local ZERO = ig.ImVec2(0, 0)

-- One scratch bool for every row: the checkbox needs a bool* and allocating a
-- fresh cdata per row per frame is pure garbage.
local enabled_scratch = ig.bool(false)

local code_input = ig.buffer(32)
local name_input = ig.buffer(64)
local message, message_colour = nil, DIM

local ENTER = ig.lib.ImGuiInputTextFlags_EnterReturnsTrue

local function add_cheat()
	local code = ig.str(code_input)
	if code == "" then
		return
	end

	local index = dbg.cheat_add(code, ig.str(name_input))
	if index < 0 then
		message = string.format("'%s' is not a code this understands, or the list is full", code)
		message_colour = BAD_COL
		return
	end

	message = string.format("%s added", code)
	message_colour = OK_COL

	-- Emptying a char buffer is a terminator at the front; there is no length
	-- to reset.
	code_input[0] = 0
	name_input[0] = 0
end

local function draw_entry_form()
	ig.TextColored(DIM, "Code")
	ig.SameLine(0, 6)
	ig.PushItemWidth(150)
	local submitted = ig.InputText("##cheat_code", code_input, ig.ffi.sizeof(code_input), ENTER, nil, nil)
	ig.PopItemWidth()

	ig.SameLine(0, 12)
	ig.TextColored(DIM, "Description")
	ig.SameLine(0, 6)
	ig.PushItemWidth(220)
	if ig.InputText("##cheat_name", name_input, ig.ffi.sizeof(name_input), ENTER, nil, nil) then
		submitted = true
	end
	ig.PopItemWidth()

	ig.SameLine(0, 12)
	if ig.Button("Add", ZERO) or submitted then
		add_cheat()
	end

	ig.SameLine(0, 12)
	if ig.Button("Clear all", ZERO) then
		dbg.cheat_clear()
		message, message_colour = "all cheats removed", DIM
	end

	ig.TextColored(DIM, "Game Genie (SXIOPO / GXNTLZEX), Pro Action Replay (0010FF00), or raw AAAA:VV / AAAA?CC:VV")

	if message then
		ig.TextColored(message_colour, message)
	end
end

local function draw_list()
	local count = dbg.cheat_count()
	if count == 0 then
		ig.TextDisabled("No cheats.")
		return
	end

	local flags = bit.bor(ig.lib.ImGuiTableFlags_Borders,
		ig.lib.ImGuiTableFlags_RowBg,
		ig.lib.ImGuiTableFlags_ScrollY,
		ig.lib.ImGuiTableFlags_SizingFixedFit,
		ig.lib.ImGuiTableFlags_Resizable)

	if not ig.BeginTable("cnes_cheats", 6, flags, ZERO, 0) then
		return
	end

	ig.TableSetupColumn("On", ig.lib.ImGuiTableColumnFlags_WidthFixed, 30, 0)
	ig.TableSetupColumn("Code", ig.lib.ImGuiTableColumnFlags_WidthFixed, 100, 0)
	ig.TableSetupColumn("Effect", ig.lib.ImGuiTableColumnFlags_WidthFixed, 150, 0)
	ig.TableSetupColumn("Applies to", ig.lib.ImGuiTableColumnFlags_WidthFixed, 80, 0)
	ig.TableSetupColumn("Description", ig.lib.ImGuiTableColumnFlags_WidthStretch, 0, 0)
	ig.TableSetupColumn("", ig.lib.ImGuiTableColumnFlags_WidthFixed, 30, 0)
	ig.TableSetupScrollFreeze(0, 1)
	ig.TableHeadersRow()

	-- Removal is deferred: taking an entry out mid-iteration would shuffle every
	-- index after it while the loop is still using them.
	local remove_index = nil

	for index = 0, count - 1 do
		local cheat = dbg.cheat_get(index)
		if cheat then
			ig.PushID_Int(index)
			ig.TableNextRow(0, 0)

			ig.TableSetColumnIndex(0)
			enabled_scratch[0] = cheat.enabled ~= 0
			if ig.Checkbox("##on", enabled_scratch) then
				dbg.cheat_set_enabled(index, enabled_scratch[0])
			end

			ig.TableSetColumnIndex(1)
			ig.TextColored(VAL, cheat.code)

			ig.TableSetColumnIndex(2)
			if cheat.has_compare ~= 0 then
				ig.Text(string.format("$%04X = $%02X if $%02X",
					cheat.address, cheat.value, cheat.compare))
			else
				ig.Text(string.format("$%04X = $%02X", cheat.address, cheat.value))
			end

			ig.TableSetColumnIndex(3)
			local cartridge = cheat.kind == dbg.CHEAT.CARTRIDGE
			ig.TextColored(DIM, cartridge and "cartridge" or "system")
			if ig.IsItemHovered(0) then
				ig.SetTooltip(cartridge
					and "answered in place of the cartridge, so it follows whatever bank is mapped in"
					or "answered in place of work RAM, leaving the game's own value where it is")
			end

			ig.TableSetColumnIndex(4)
			ig.Text(cheat.name)

			ig.TableSetColumnIndex(5)
			if ig.SmallButton("x") then
				remove_index = index
			end

			ig.PopID()
		end
	end

	ig.EndTable()

	if remove_index then
		dbg.cheat_remove(remove_index)
		message, message_colour = "cheat removed", DIM
	end
end

ui.panel {
	id    = "cnes.cheats",
	title = "cNES Cheats",
	group = "cNES",
	draw  = function()
		if not dbg.available then
			ig.TextWrapped(dbg.error or "the cNES debug API is unavailable")
			return
		end

		draw_entry_form()
		ig.Separator()

		-- Deliberately not gated on a ROM running: codes can be queued up
		-- before one is loaded, and load_rom re-applies them.
		if dbg.state() == nil then
			ig.TextDisabled("No ROM running - ROM codes will apply when one is loaded.")
			ig.Spacing()
		end

		draw_list()
	end,
}
