-- Shared plumbing for cNES test scripts.
--
-- Every test reports its verdict through the process exit code so CTest never
-- has to match on stdout. Output is for humans reading a failure.

local harness = {}

harness.EXIT_PASS  = 0
harness.EXIT_FAIL  = 1
harness.EXIT_ERROR = 2

-- Button bits as the NES controller shift register reports them.
harness.BUTTON = {
	A      = 0x01,
	B      = 0x02,
	SELECT = 0x04,
	START  = 0x08,
	UP     = 0x10,
	DOWN   = 0x20,
	LEFT   = 0x40,
	RIGHT  = 0x80,
}

local function fmt(format, ...)
	if select("#", ...) > 0 then
		return string.format(format, ...)
	end
	return format
end

function harness.info(format, ...)
	print(fmt(format, ...))
end

function harness.detail(format, ...)
	print("       " .. fmt(format, ...))
end

-- Values passed with --arg key=value arrive as strings in the global ARGS.

function harness.arg(name, default)
	local args = rawget(_G, "ARGS")
	local value = args and args[name]
	if value == nil or value == "" then
		return default
	end
	return value
end

function harness.arg_number(name, default)
	local value = harness.arg(name, nil)
	if value == nil then
		return default
	end
	local number = tonumber(value)
	if number == nil then
		harness.die("--arg %s expects a number, got '%s'", name, tostring(value))
	end
	return number
end

function harness.arg_flag(name, default)
	local value = harness.arg(name, nil)
	if value == nil then
		return default and true or false
	end
	value = tostring(value):lower()
	return value == "1" or value == "true" or value == "yes" or value == "on"
end

-- Verdicts. Each of these ends the run, so callers should return straight after.

function harness.finish(ok, format, ...)
	local message = fmt(format, ...)
	if ok then
		print("RESULT: PASS - " .. message)
		exit_with_code(harness.EXIT_PASS)
	else
		print("RESULT: FAIL - " .. message)
		exit_with_code(harness.EXIT_FAIL)
	end
end

-- A fault in the test itself rather than a verdict about the emulator.
function harness.die(format, ...)
	local message = fmt(format, ...)
	print("RESULT: ERROR - " .. message)
	exit_with_code(harness.EXIT_ERROR)
	error(message, 0)
end

-- Runs the console until predicate() is true or the frame budget runs out.
-- Returns true if the predicate fired, plus the number of frames consumed.
function harness.run_until(predicate, max_frames)
	max_frames = max_frames or 3600
	for frame = 1, max_frames do
		run_frames(1)
		if predicate(frame) then
			return true, frame
		end
	end
	return false, max_frames
end

-- Steps single instructions until predicate(pc, cycles) is true. Used by tests
-- that need to see the CPU between instructions rather than between frames.
function harness.step_until(predicate, max_steps)
	max_steps = max_steps or 10000000
	for count = 1, max_steps do
		step()
		if predicate(get_pc(), get_cycles(), count) then
			return true, count
		end
	end
	return false, max_steps
end

-- True once the CPU is spinning in a branch-to-self, which is how the blargg
-- and Kevtris ROMs signal "finished, nothing more will change".
function harness.detect_halt(window, max_steps)
	window = window or 64
	local seen = {}
	local distinct = 0
	local last_reset = 0

	for count = 1, (max_steps or 2000000) do
		step()
		local pc = get_pc()
		if not seen[pc] then
			seen[pc] = true
			distinct = distinct + 1
		end
		if count - last_reset >= window then
			if distinct <= 3 then
				return true, count
			end
			seen = {}
			distinct = 0
			last_reset = count
		end
	end

	return false, max_steps
end

function harness.hex8(value)
	return string.format("$%02X", value)
end

function harness.hex16(value)
	return string.format("$%04X", value)
end

function harness.cpu_state()
	return string.format("PC:%04X A:%02X X:%02X Y:%02X P:%02X SP:%02X CYC:%d",
		get_pc(), get_a(), get_x(), get_y(), get_status(), get_sp(), get_cycles())
end

return harness
