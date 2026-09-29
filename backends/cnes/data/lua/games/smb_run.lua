-- Super Mario Bros. run tracking.
--
-- Arms on the title screen, starts when the game does, splits on every stage
-- change, and stops when the run ends. Nothing to press.
--
-- The state machine lives here rather than in either of the two things that
-- show it -- the overlay on the picture and the panel beside it -- because a
-- run has to be tracked whether or not anyone is currently looking at it.
-- Turning the overlay off halfway through a run must not lose the splits.
--
-- Timed in emulated frames and in SMB's own 21-frame rules rather than in
-- wall-clock seconds. Those are the numbers a run is actually measured in, and
-- they are the only ones that survive the emulator being paused,
-- fast-forwarded or scrubbed through a movie: a timer reading a clock would
-- count all of that as run time.

local ig  = require("mse.imgui")
local dbg = require("ui.debug")
local smb = require("games.smb")

local M = {}

-- The NTSC NES does not run at 60Hz. Using 60 would gain about half a second
-- every five minutes, which over a full run is the difference between two
-- times that should compare equal.
M.NTSC_FPS = 60.0988139

M.IDLE     = "idle"
M.RUNNING  = "running"
M.FINISHED = "finished"

local run = {
	status      = M.IDLE,
	start_frame = 0,
	start_phase = 0,
	end_frame   = 0,
	last_frame  = 0,
	stage       = nil,
	stage_frame = 0,
	stage_rules = 0,
	frame       = 0,
	splits      = {},
}

local polled_at = -1

local function reset()
	run.status = M.IDLE
	run.stage  = nil
	run.splits = {}
end

--- Throws the run away and waits for the next one to start.
function M.reset()
	reset()
end

--- frames as h:mm:ss.mmm, dropping the units that are zero.
function M.format_time(frames)
	if frames < 0 then
		frames = 0
	end

	local seconds = frames / M.NTSC_FPS
	local minutes = math.floor(seconds / 60)
	seconds = seconds - minutes * 60

	if minutes >= 60 then
		local hours = math.floor(minutes / 60)
		return string.format("%d:%02d:%06.3f", hours, minutes - hours * 60, seconds)
	end
	return string.format("%d:%06.3f", minutes, seconds)
end

--- How many frame rules have passed in `frames` of this run.
---
--- Counted against the game's rule boundaries, not from the run's first frame:
--- the phase the game was already at when the run started is added in, so a
--- boundary here is a boundary there. Levels only load on boundaries, so this
--- is what makes a level's rule count come out whole instead of landing a few
--- frames either side of one.
function M.framerules(frames)
	return (run.start_phase + frames) / smb.FRAMERULE
end

local function split(frame)
	local total_frames = frame - run.start_frame
	local total_rules  = math.floor(M.framerules(total_frames))

	run.splits[#run.splits + 1] = {
		stage          = run.stage,
		segment_frames = frame - run.stage_frame,
		total_frames   = total_frames,
		segment_rules  = total_rules - run.stage_rules,
		total_rules    = total_rules,
		-- What the game's own clock had left. The number a Super Mario Bros.
		-- run is compared on outside of frame counts, and it is gone as soon as
		-- the next level loads.
		game_timer     = smb.game_timer(),
		coins          = smb.byte(smb.COINS),
	}

	run.stage_rules = total_rules
end

local function advance(frame)
	-- The frame counter restarts at a reset, and so does the run.
	if frame < run.last_frame then
		reset()
	end
	run.last_frame = frame
	run.frame      = frame

	local mode = smb.oper_mode()

	if run.status == M.IDLE then
		if mode == smb.MODE_GAME then
			run.status      = M.RUNNING
			run.start_frame = frame
			run.start_phase = smb.framerule_phase()
			run.stage       = smb.stage()
			run.stage_frame = frame
			run.stage_rules = 0
		end
		return
	end

	if run.status == M.RUNNING then
		if mode == smb.MODE_GAME then
			local stage = smb.stage()
			if stage ~= run.stage then
				split(frame)
				run.stage       = stage
				run.stage_frame = frame
			end
		elseif mode == smb.MODE_VICTORY then
			-- The princess. Anything else -- a death, a game over -- leaves the
			-- clock running, which is what a single-segment run wants.
			split(frame)
			run.status    = M.FINISHED
			run.end_frame = frame
		elseif mode == smb.MODE_TITLE then
			reset() -- abandoned
		end
	elseif run.status == M.FINISHED and mode == smb.MODE_TITLE then
		reset()
	end
end

--- Brings the run up to date. Safe to call from several places in a frame; the
--- work happens on the first call and the rest read the same answer.
---
--- A transition is noticed on the UI frame that follows it, which at normal
--- speed is within a frame. While the emulator is running ahead of the UI -- a
--- movie seek, fast-forward -- a split can land a few frames late, which is
--- why the frame numbers are shown rather than only the times.
function M.poll()
	local frame = ig.GetFrameCount()
	if frame == polled_at then
		return run
	end
	polled_at = frame

	if not dbg.available or not smb.detected() then
		return run
	end

	local state = dbg.state()
	if state == nil then
		return run
	end

	advance(tonumber(state.ppu.frame))
	return run
end

--- Elapsed frames, stopped at the finish.
function M.elapsed()
	if run.status == M.IDLE then
		return 0
	end
	if run.status == M.FINISHED then
		return run.end_frame - run.start_frame
	end
	return run.frame - run.start_frame
end

--- The run as it stands. Call poll() first.
function M.get()
	return run
end

return M
