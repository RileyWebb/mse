-- Super Mario Bros. speedrun timer, on the picture.
--
-- The running total only -- time, frame, frame rule -- drawn in the game's own
-- HUD font so it reads as part of the picture rather than as something stuck on
-- top of it. The splits are a table, and a table belongs in a window: see the
-- cNES SMB Run panel, which reads the same run out of games.smb_run.

local ig      = require("mse.imgui")
local overlay = require("mse.overlay")
local dbg     = require("ui.debug")
local smb     = require("games.smb")
local smb_run = require("games.smb_run")
local font    = require("games.smb_font")

local IDLE    = ig.U32(0.70, 0.70, 0.75, 0.95)
local RUNNING = ig.U32(1.00, 1.00, 1.00, 0.98)
local DONE    = ig.U32(0.45, 0.95, 0.50, 0.98)

-- Placed on the game's own text grid: SMB draws its status bar out of 8x8
-- tiles, so anything that does not start at a multiple of eight reads as
-- slightly crooked next to it. Row 1 is the blank line directly above the
-- status bar, column 3 is where MARIO and the score start, and column 29 is
-- one past where TIME ends.
local COLUMN = 3 * 8
local ROW    = 1 * 8
local RIGHT  = 29 * 8

overlay.register {
	id    = "cnes.smb.timer",
	title = "SMB speedrun timer",
	group = "cNES",
	draw  = function(view)
		if not dbg.available or not smb.detected() then
			return
		end

		local run    = smb_run.poll()
		local frames = smb_run.elapsed()

		local colour = (run.status == smb_run.FINISHED and DONE)
			or (run.status == smb_run.RUNNING and RUNNING)
			or IDLE

		-- One line, in the blank strip the game leaves above its own status
		-- bar. Reads as an extra HUD row rather than as a box sitting on the
		-- level, and it is the only place on a 240-line picture that is free
		-- without covering something.
		--
		-- No stage here: the game's own HUD is directly underneath and already
		-- says WORLD 4-1.
		--
		-- Every field is padded to a fixed width, so nothing shifts sideways as
		-- the numbers grow. A clock that moves every time a digit rolls over is
		-- unreadable at a glance, which is the only way anyone reads it.
		if run.status == smb_run.IDLE then
			font.text(view, COLUMN, ROW, colour, "WAITING FOR START")
			return
		end

		-- The clock on the left, where the game's own text starts.
		font.text(view, COLUMN, ROW, colour, smb_run.format_time(frames))

		-- The frame rule on the right, hung off the column TIME ends at, so it
		-- stays put as the counts grow rather than creeping across the screen.
		-- Whole rules elapsed and how far into the current one the game is; the
		-- phase is the game's own, so a boundary here is a level load there.
		-- The phase padded to two, so only the rule count moves the left edge
		-- and the '+' does not shuffle back and forth every frame.
		local rule = string.format("%d+%2d",
			math.floor(smb_run.framerules(frames)), smb.framerule_phase())
		font.text(view, RIGHT - font.width(rule), ROW, colour, rule)
	end,
}
