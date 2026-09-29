-- cNES SMB Run: the splits table for a Super Mario Bros. run.
--
-- The same run the on-picture timer shows (games.smb_run), with the part that
-- does not fit over a 256-pixel screen: every level, when it finished, how long
-- it took, and what the game's own clock and coin counter said as it ended.
--
-- Nothing here starts or stops anything. The run is detected from the game's
-- own state, so the only control is a reset for when you want to throw one
-- away without going back to the title screen.

local ig      = require("mse.imgui")
local ui      = require("mse.ui")
local dbg     = require("ui.debug")
local smb     = require("games.smb")
local smb_run = require("games.smb_run")

local DIM     = ig.ImVec4(0.55, 0.55, 0.55, 1.0)
local VAL     = ig.ImVec4(0.55, 0.78, 1.0, 1.0)
local OK_COL  = ig.ImVec4(0.45, 0.95, 0.50, 1.0)
local WAITING = ig.ImVec4(0.75, 0.75, 0.80, 1.0)

local ZERO = ig.ImVec2(0, 0)

local show_frames = ig.bool(false)

local STATUS = {
	idle     = { "waiting for the game to start", WAITING },
	running  = { "running", VAL },
	finished = { "finished", OK_COL },
}

local function draw_header(run)
	local frames = smb_run.elapsed()
	local status = STATUS[run.status] or STATUS.idle

	-- The total, big. Everything else in here is a detail of it.
	ig.PushFont(nil, 32)
	ig.TextColored(status[2], smb_run.format_time(frames))
	ig.PopFont()

	ig.TextColored(DIM, status[1])

	ig.SameLine(0, 14)
	ig.TextColored(DIM, "frames")
	ig.SameLine(0, 5)
	ig.TextColored(VAL, string.format("%d", frames))

	ig.SameLine(0, 14)
	ig.TextColored(DIM, "rules")
	ig.SameLine(0, 5)
	ig.TextColored(VAL, string.format("%d+%d",
		math.floor(smb_run.framerules(frames)), smb.framerule_phase()))


	if run.status == smb_run.RUNNING and run.stage then
		ig.SameLine(0, 14)
		ig.TextColored(DIM, "on")
		ig.SameLine(0, 5)
		ig.TextColored(VAL, run.stage)
	end
end

local TABLE_FLAGS = bit.bor(
	ig.lib.ImGuiTableFlags_RowBg,
	ig.lib.ImGuiTableFlags_BordersInnerH,
	ig.lib.ImGuiTableFlags_SizingStretchProp,
	ig.lib.ImGuiTableFlags_ScrollY)

local function draw_splits(run)
	if #run.splits == 0 then
		ig.TextDisabled("No levels finished yet.")
		return
	end

	-- The host lets the window auto-fit, so a table sized to what is left over
	-- gets no height at all until someone drags the window bigger. Asking for
	-- room for a few rows makes the window fit around the table instead, while
	-- still filling the space once it has been resized.
	local rows   = math.max(math.min(#run.splits + 1, 12), 6)
	local height = math.max(ig.GetContentRegionAvail().y,
		ig.GetTextLineHeightWithSpacing() * rows)

	local columns = show_frames[0] and 6 or 5
	if not ig.BeginTable("smb_splits", columns, TABLE_FLAGS, ig.ImVec2(0, height), 0) then
		return
	end

	ig.TableSetupScrollFreeze(0, 1)
	ig.TableSetupColumn("Level", ig.lib.ImGuiTableColumnFlags_WidthFixed, 56, 0)
	ig.TableSetupColumn("Segment", 0, 0, 0)
	ig.TableSetupColumn("Finished at", 0, 0, 0)
	ig.TableSetupColumn("Rules", 0, 0, 0)
	if show_frames[0] then
		ig.TableSetupColumn("Frames", 0, 0, 0)
	end
	ig.TableSetupColumn("Total rules", 0, 0, 0)
	ig.TableHeadersRow()

	-- The best segment is worth pointing at, and on a 32-level game it is not
	-- something you find by reading down the column.
	local best, best_index = nil, nil
	for index, split in ipairs(run.splits) do
		if best == nil or split.segment_frames < best then
			best, best_index = split.segment_frames, index
		end
	end

	for index, split in ipairs(run.splits) do
		ig.TableNextRow(0, 0)

		ig.TableSetColumnIndex(0)
		ig.TextColored(VAL, split.stage)

		ig.TableSetColumnIndex(1)
		if index == best_index then
			ig.TextColored(OK_COL, smb_run.format_time(split.segment_frames))
			if ig.IsItemHovered(0) then
				ig.SetTooltip("fastest level of the run so far")
			end
		else
			ig.TextUnformatted(smb_run.format_time(split.segment_frames), nil)
		end

		ig.TableSetColumnIndex(2)
		ig.TextUnformatted(smb_run.format_time(split.total_frames), nil)

		-- Counted against the game's rule boundaries, so this is the number a
		-- level actually cost: levels only load on a boundary, and two runs
		-- that differ by a frame but not by a rule take the same time.
		ig.TableSetColumnIndex(3)
		ig.TextColored(DIM, string.format("%d", split.segment_rules))

		local column = 4
		if show_frames[0] then
			ig.TableSetColumnIndex(column)
			ig.TextColored(DIM, string.format("%d", split.segment_frames))
			column = column + 1
		end

		-- The cumulative rule count rather than the game's own clock: a level
		-- that ends at a flagpole has had its timer counted down into the score
		-- by the time the next one loads, so that column would read zero for
		-- most of a run and mean nothing for the rest.
		ig.TableSetColumnIndex(column)
		ig.TextColored(DIM, string.format("%d", split.total_rules))
	end

	ig.EndTable()
end

ui.panel {
	id    = "cnes.smb_run",
	title = "cNES SMB Run",
	group = "cNES",
	size  = { 620, 460 },
	draw  = function()
		if not dbg.available then
			ig.TextDisabled("cNES debug API unavailable")
			return
		end

		if not smb.detected() then
			ig.TextDisabled("Super Mario Bros. is not running.")
			ig.TextDisabled("This panel reads that game's own state; there is nothing generic to show.")
			return
		end

		local run = smb_run.poll()

		draw_header(run)

		ig.Separator()

		if ig.Button("Reset", ZERO) then
			smb_run.reset()
		end
		if ig.IsItemHovered(0) then
			ig.SetTooltip("Throws this run away and waits for the next one.\n"
				.. "The run starts and stops itself; this is only for discarding one.")
		end

		ig.SameLine(0, 12)
		ig.Checkbox("Frames", show_frames)
		if ig.IsItemHovered(0) then
			ig.SetTooltip("Show the raw frame count per level as well as frame rules")
		end

		ig.SameLine(0, 12)
		ig.TextColored(DIM, string.format("%d level%s",
			#run.splits, #run.splits == 1 and "" or "s"))

		ig.Separator()

		draw_splits(run)
	end,
}
