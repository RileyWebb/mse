-- Declarative controller scripting.
--
-- Test ROMs are driven by menu navigation, and hand-computed frame numbers were
-- the most error-prone part of the old scripts. A sequence describes the button
-- presses in order and runs them, so nothing has to be counted by hand.

local input = {}

local BUTTON_ALL = 0xFF

-- sequence(pad) returns a chainable builder. Call :run() to execute it.
function input.sequence(pad)
	pad = pad or 0

	local steps = {}
	local sequence = {}

	-- Idle for a number of frames with the current buttons held.
	function sequence:wait(frames)
		steps[#steps + 1] = { kind = "wait", frames = frames or 1 }
		return self
	end

	-- Press and release a button.
	function sequence:tap(button, frames, gap)
		steps[#steps + 1] = { kind = "tap", button = button, frames = frames or 4, gap = gap or 4 }
		return self
	end

	-- Tap the same button several times, e.g. to walk across menu pages.
	function sequence:repeat_tap(button, times, frames, gap)
		for _ = 1, times do
			sequence:tap(button, frames, gap)
		end
		return self
	end

	function sequence:hold(button, frames)
		steps[#steps + 1] = { kind = "hold", button = button, frames = frames or 1 }
		return self
	end

	function sequence:release(button)
		steps[#steps + 1] = { kind = "release", button = button }
		return self
	end

	function sequence:run()
		set_controller_state(pad, 0)

		for _, s in ipairs(steps) do
			if s.kind == "wait" then
				run_frames(s.frames)
			elseif s.kind == "hold" then
				set_controller_button(pad, s.button, true)
				run_frames(s.frames)
			elseif s.kind == "release" then
				set_controller_button(pad, s.button, false)
				run_frames(1)
			elseif s.kind == "tap" then
				set_controller_button(pad, s.button, true)
				run_frames(s.frames)
				set_controller_button(pad, s.button, false)
				run_frames(s.gap)
			end
		end

		set_controller_state(pad, 0)
		return self
	end

	-- Frames the sequence will consume, for sizing a frame budget.
	function sequence:length()
		local total = 0
		for _, s in ipairs(steps) do
			if s.kind == "tap" then
				total = total + s.frames + s.gap
			elseif s.kind == "release" then
				total = total + 1
			else
				total = total + s.frames
			end
		end
		return total
	end

	return sequence
end

function input.release_all(pad)
	set_controller_state(pad or 0, 0)
end

input.ALL = BUTTON_ALL

return input
