-- cNES TAS panel: play an FCEUX .fm2, scrub it, and read its input log.
--
-- A movie is a list of controller states from power-on and nothing else, so
-- there is no seeking in the usual sense: landing on a frame means replaying
-- every frame before it. That work happens on the emulation thread a batch at a
-- time (cnes_tas_seek), which is why the scrub bar commits on release and shows
-- progress rather than blocking.

local ig  = require("mse.imgui")
local ui  = require("mse.ui")
local dbg = require("ui.debug")

local DIM     = ig.ImVec4(0.55, 0.55, 0.55, 1.0)
local VAL     = ig.ImVec4(0.55, 0.78, 1.0, 1.0)
local OK_COL  = ig.ImVec4(0.45, 0.95, 0.50, 1.0)
local BAD_COL = ig.ImVec4(1.00, 0.48, 0.45, 1.0)
local NOW_COL = ig.ImVec4(1.00, 0.82, 0.35, 1.0)

local ZERO = ig.ImVec2(0, 0)

-- FM2 order, left to right, which is bit 7 down to bit 0.
local BUTTONS = {
	{ mask = 0x80, label = "R" },
	{ mask = 0x40, label = "L" },
	{ mask = 0x20, label = "D" },
	{ mask = 0x10, label = "U" },
	{ mask = 0x08, label = "T" },
	{ mask = 0x04, label = "S" },
	{ mask = 0x02, label = "B" },
	{ mask = 0x01, label = "A" },
}

local scrub      = ig.int(0)
local scrubbing  = false
local follow     = ig.bool(true)
local movie_list = nil
local message, message_colour = nil, DIM

local function refresh_movie_list()
	movie_list = dbg.tas_list_movies()
end

local function load_movie(name)
	if dbg.tas_load(name) then
		message, message_colour = string.format("playing %s", name), OK_COL
		dbg.backend_resume()
	else
		message, message_colour = string.format("could not load %s", name), BAD_COL
	end
end

local function draw_picker()
	if movie_list == nil then
		refresh_movie_list()
	end

	if ig.Button("Open movie", ZERO) then
		refresh_movie_list()
		ig.OpenPopup_Str("tas_open", 0)
	end

	if ig.BeginPopup("tas_open", 0) then
		if #movie_list == 0 then
			ig.TextDisabled("no .fm2 files in the working directory")
		end
		for _, name in ipairs(movie_list) do
			if ig.Selectable_Bool(name, false, 0, ZERO) then
				load_movie(name)
			end
		end
		ig.EndPopup()
	end
end

local function draw_transport(state)
	draw_picker()

	local running = dbg.backend_state() == dbg.STATE_RUNNING

	ig.SameLine(0, 8)
	if ig.Button(running and "Pause" or "Play", ZERO) then
		if running then
			dbg.backend_pause()
		else
			dbg.backend_resume()
		end
	end

	ig.SameLine(0, 8)
	-- Frame advance pauses as well as stepping, so the button reads the same
	-- whether you reach for it while running or while already stopped.
	if ig.Button("Frame >", ZERO) then
		dbg.tas_frame_advance(1)
	end

	ig.SameLine(0, 8)
	if ig.Button("Restart", ZERO) then
		dbg.tas_restart()
		message, message_colour = "restarted from power-on", DIM
	end

	ig.SameLine(0, 8)
	if ig.Button("Eject", ZERO) then
		dbg.tas_stop()
		message, message_colour = "playback stopped, controllers released", DIM
	end

	ig.SameLine(0, 16)
	ig.Checkbox("Follow", follow)
	if ig.IsItemHovered(0) then
		ig.SetTooltip("Keep the input log on the frame being played")
	end

	if state.seeking then
		ig.SameLine(0, 16)
		if ig.Button("Cancel seek", ZERO) then
			dbg.tas_cancel_seek()
		end
	end
end

local function draw_scrub(state)
	-- While the slider is held it shows where the drag is; the rest of the time
	-- it tracks playback. Committing on release stops a drag from queueing a
	-- full replay for every pixel the mouse moves.
	if not scrubbing then
		scrub[0] = state.frame
	end

	ig.PushItemWidth(-1)
	ig.SliderInt("##tas_scrub", scrub, 0, math.max(1, state.total), "frame %d", 0)
	ig.PopItemWidth()

	if ig.IsItemActive() then
		scrubbing = true
	elseif scrubbing then
		scrubbing = false
		dbg.tas_seek(scrub[0])
	end

	if state.seeking then
		-- Measured from where the seek started, not from zero, so a short hop
		-- forwards does not read as a bar that never leaves the left edge.
		local from = math.min(state.frame, state.seek_target)
		local span = math.max(1, state.seek_target - from)
		ig.ProgressBar((state.frame - from) / span, ig.ImVec2(-1, 0),
			string.format("seeking to %d (at %d)", state.seek_target, state.frame))
	end
end

-- One row of the input log, as the FM2 mnemonics with the released buttons
-- shown as dots. The shape of a held run is what you read off a piano roll, so
-- the columns have to line up whether a button is down or not.
local function draw_input_row(pad)
	for index, button in ipairs(BUTTONS) do
		if index > 1 then
			ig.SameLine(0, 3)
		end
		if bit.band(pad, button.mask) ~= 0 then
			ig.TextColored(VAL, button.label)
		else
			ig.TextColored(DIM, ".")
		end
	end
end

local LOG_MIN_ROWS = 18

local function draw_input_log(state)
	local row_height = ig.GetTextLineHeightWithSpacing()

	-- The host lets the window auto-fit, so there is nothing "left over" to
	-- size the log against until someone drags the window bigger. Claiming a
	-- minimum makes it fit around the log instead.
	local height = math.max(ig.GetContentRegionAvail().y, row_height * LOG_MIN_ROWS)
	if not ig.BeginChild_Str("tas_log_area", ig.ImVec2(0, height), 0, 0) then
		ig.EndChild()
		return
	end

	local rows = math.max(1, math.floor(height / row_height) - 1)

	-- The playing row sits a third of the way down, so there is history above
	-- it and what is coming below it.
	local first = math.floor(state.frame - rows / 3)
	if not follow[0] then
		first = math.floor(scrub[0] - rows / 3)
	end
	if first + rows > state.total then
		first = state.total - rows
	end
	if first < 0 then
		first = 0
	end

	local data, got = dbg.tas_read_frames(first, rows)
	if got == 0 then
		ig.TextDisabled("no input log")
		ig.EndChild()
		return
	end

	local flags = bit.bor(ig.lib.ImGuiTableFlags_RowBg, ig.lib.ImGuiTableFlags_SizingFixedFit)
	if not ig.BeginTable("tas_log", state.ports > 1 and 3 or 2, flags, ZERO, 0) then
		ig.EndChild()
		return
	end

	ig.TableSetupColumn("frame", ig.lib.ImGuiTableColumnFlags_WidthFixed, 64, 0)
	ig.TableSetupColumn("1P", ig.lib.ImGuiTableColumnFlags_WidthFixed, 130, 0)
	if state.ports > 1 then
		ig.TableSetupColumn("2P", ig.lib.ImGuiTableColumnFlags_WidthFixed, 130, 0)
	end
	ig.TableHeadersRow()

	for i = 0, got - 1 do
		local row = first + i
		ig.TableNextRow(0, 0)

		-- The row about to be applied, which is where the console is.
		local is_now = row == state.frame
		if is_now then
			ig.TableSetBgColor(ig.lib.ImGuiTableBgTarget_RowBg0,
				ig.GetColorU32_Vec4(ig.ImVec4(0.30, 0.24, 0.06, 1.0)), -1)
		end

		ig.TableSetColumnIndex(0)
		ig.TextColored(is_now and NOW_COL or DIM, string.format("%6d", row))

		ig.TableSetColumnIndex(1)
		draw_input_row(data[i * 2])

		if state.ports > 1 then
			ig.TableSetColumnIndex(2)
			draw_input_row(data[i * 2 + 1])
		end
	end

	ig.EndTable()
	ig.EndChild()
end

local function draw_header(state)
	ig.TextColored(DIM, "Movie")
	ig.SameLine(0, 6)
	ig.TextColored(VAL, state.path ~= "" and state.path or "(none)")

	ig.SameLine(0, 16)
	ig.TextColored(DIM, "frame")
	ig.SameLine(0, 6)
	ig.TextColored(VAL, string.format("%d / %d", state.frame, state.total))

	ig.SameLine(0, 16)
	ig.TextColored(DIM, state.ports > 1 and "2 controllers" or "1 controller")

	if state.pal then
		ig.SameLine(0, 16)
		ig.TextColored(BAD_COL, "recorded on PAL")
	end

	if state.loaded and not state.playing then
		ig.SameLine(0, 16)
		ig.TextColored(DIM, "finished")
	end

	-- There is no MD5 on this side to check the movie's checksum against, so
	-- the header is reported rather than verified. A movie recorded against a
	-- different dump of the same game plays without complaint and desyncs
	-- minutes later, which is worth being able to rule out by eye.
	if state.rom_filename ~= "" then
		ig.TextColored(DIM, "Recorded against")
		ig.SameLine(0, 6)
		ig.TextUnformatted(state.rom_filename, nil)
		if state.rom_checksum ~= "" and ig.IsItemHovered(0) then
			ig.SetTooltip(state.rom_checksum)
		end
	end
end

ui.panel {
	id    = "cnes.tas",
	title = "cNES TAS",
	group = "cNES",
	size  = { 520, 560 },
	draw  = function()
		if not dbg.available then
			ig.TextDisabled("cNES debug API unavailable")
			return
		end

		local state = dbg.tas_state()
		if state == nil then
			ig.TextDisabled("No ROM running.")
			return
		end

		draw_transport(state)

		if message then
			ig.TextColored(message_colour, message)
		end

		ig.Separator()

		if not state.loaded then
			ig.TextDisabled("No movie loaded. Open an .fm2 from the working directory,")
			ig.TextDisabled("or run: cnes_tas_play <movie.fm2>")
			return
		end

		draw_header(state)
		draw_scrub(state)
		ig.Separator()
		draw_input_log(state)
	end,
}
