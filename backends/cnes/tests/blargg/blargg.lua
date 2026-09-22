-- blargg's test ROMs.
--
-- These ROMs report in two places at once:
--
--   * A status block in PRG RAM: $6001-$6003 hold the marker $DE $B0 $61 once
--     the ROM has started, $6000 is $80 while running and the result code when
--     finished, and $6004 onwards is a NUL-terminated ASCII report.
--   * The same report drawn on screen, in an ASCII-ordered font.
--
-- The status block is the better signal - it distinguishes "still running" from
-- "finished" without guessing - but it only exists when the mapper provides RAM
-- at $6000. So this tries the protocol first and falls back to reading the
-- screen, saying which one it used.
--
--   --arg name=<label>    what to call this ROM in the results and baseline
--   --arg mode=auto|status|screen   which report to read (default auto)
--   --arg frames=<n>      frame budget before the ROM is declared stuck

package.path = "./?.lua;" .. package.path

local harness = require("lib.harness")
local report  = require("lib.report")
local screen  = require("lib.screen")

local STATUS_ADDR = 0x6000
local MARKER_ADDR = 0x6001
local TEXT_ADDR   = 0x6004
local MARKER      = { 0xDE, 0xB0, 0x61 }

local STATUS_RUNNING     = 0x80
local STATUS_NEEDS_RESET = 0x81

local DEFAULT_FRAMES = 5400
local POLL_INTERVAL  = 10
local MAX_TEXT       = 1024

-- How long to wait for the marker before deciding this ROM has no status block.
local MARKER_FRAMES = 120

-- The ROM requires at least 100ms to pass before the reset it asks for.
local RESET_DELAY_FRAMES = 12

local function trim(text)
	return (text:gsub("^%s+", ""):gsub("%s+$", ""))
end

-- The reports are laid out for a 32-column screen, with blank lines between
-- sections. Flatten them onto one line for the result detail.
local function summarise(text)
	return (trim(text):gsub("%s*\n+%s*", "; "))
end

local function marker_present()
	for offset, expected in ipairs(MARKER) do
		if mem_read(MARKER_ADDR + offset - 1) ~= expected then
			return false
		end
	end
	return true
end

local function read_status_text()
	local chars = {}
	for offset = 0, MAX_TEXT - 1 do
		local byte = mem_read(TEXT_ADDR + offset)
		if byte == 0 then
			break
		end
		if byte == 0x0A or (byte >= 0x20 and byte <= 0x7E) then
			chars[#chars + 1] = string.char(byte)
		end
	end
	return table.concat(chars)
end

-- Two font conventions appear across these ROMs: some store a character's
-- ASCII code in the tile index, others store it minus $20. Neither is
-- detectable from the tiles alone, so both are decoded and whichever produces a
-- verdict wins. A wrong offset yields noise, never a false verdict.
local SCREEN_FONT_OFFSETS = { 0x00, 0x20 }

local function read_screen_text(offset)
	screen.set_charset(function(tile)
		local value = tile + offset
		if value >= 0x20 and value <= 0x7E then
			return string.char(value)
		end
		return " "
	end)

	local lines = {}
	for _, line in ipairs(screen.text(0)) do
		local text = trim(line)
		if text ~= "" then
			lines[#lines + 1] = text
		end
	end
	return table.concat(lines, "\n")
end

-- A blargg report always ends in a verdict line. Matching on those keeps the
-- run as short as the ROM needs instead of a fixed wait. Case varies: the
-- uppercase-only ROMs print "PASSED".
local function verdict_of(text)
	for line in (text .. "\n"):gmatch("(.-)\n") do
		local clean = trim(line):upper()
		if clean == "PASSED" or clean:match("^ALL %d+ TESTS PASSED$") then
			return "pass"
		end
		if clean:match("^FAILED") or clean:match("^ERROR")
			or clean:match("^%d+/%d+ TESTS FAILED") then
			return "fail"
		end
	end
	return nil
end

local function print_output(label, text)
	if trim(text) == "" then
		return
	end
	print("--- " .. label .. " ---")
	for line in (text .. "\n"):gmatch("(.-)\n") do
		if trim(line) ~= "" then
			print("  " .. trim(line))
		end
	end
	print("--- end " .. label .. " ---")
end

-- Returns status, text, frames. status is nil if the ROM never finished.
local function run_status_protocol(budget, frames)
	while frames < budget do
		local status = mem_read(STATUS_ADDR)

		if status == STATUS_NEEDS_RESET then
			run_frames(RESET_DELAY_FRAMES)
			frames = frames + RESET_DELAY_FRAMES
			reset()
		elseif status < STATUS_RUNNING then
			return status, read_status_text(), frames
		end

		run_frames(POLL_INTERVAL)
		frames = frames + POLL_INTERVAL
	end

	return nil, read_status_text(), frames
end

-- Returns verdict, text, frames. verdict is nil if the ROM never reported.
local function run_screen_report(budget, frames)
	while frames < budget do
		run_frames(POLL_INTERVAL)
		frames = frames + POLL_INTERVAL

		for _, offset in ipairs(SCREEN_FONT_OFFSETS) do
			local text    = read_screen_text(offset)
			local verdict = verdict_of(text)
			if verdict then
				return verdict, text, frames
			end
		end
	end

	return nil, read_screen_text(SCREEN_FONT_OFFSETS[1]), frames
end

function onrun()
	local name   = harness.arg("name", "blargg")
	local budget = harness.arg_number("frames", DEFAULT_FRAMES)
	local mode   = harness.arg("mode", "auto")

	if mode ~= "auto" and mode ~= "status" and mode ~= "screen" then
		harness.die("--arg mode expects auto, status or screen, got %q", mode)
		return
	end

	local baseline = string.format("baseline/blargg_%s.txt", name)
	local results  = report.new(name)
	local frames   = 0

	-- Give the ROM a chance to put up its marker before choosing a mode.
	if mode ~= "screen" then
		while frames < MARKER_FRAMES and not marker_present() do
			run_frames(POLL_INTERVAL)
			frames = frames + POLL_INTERVAL
		end
	end

	if mode == "status" and not marker_present() then
		harness.finish(false,
			"%s never wrote the $6001 marker - this ROM's mapper provides no RAM at $6000",
			name)
		return
	end

	if mode ~= "screen" and marker_present() then
		local status, text, total = run_status_protocol(budget, frames)
		harness.info("%s: status protocol, %d frames", name, total)
		print_output("ROM report", text)

		if status == nil then
			harness.finish(false, "%s was still running after %d frames (status $%02X)",
				name, budget, mem_read(STATUS_ADDR))
			return
		end

		if status == 0 then
			results:add(name, report.PASS, summarise(text))
		else
			results:add(name, report.FAIL,
				string.format("status $%02X - %s", status, summarise(text)))
		end

		results:print_results(true)
		results:verdict(baseline)
		return
	end

	if mode == "auto" then
		harness.info("%s: no status block at $6000, reading the report off screen", name)
	end

	local verdict, text, total = run_screen_report(budget, frames)
	harness.info("%s: screen report, %d frames", name, total)
	print_output("ROM screen", text)

	if verdict == nil then
		harness.finish(false,
			"%s drew no verdict within %d frames - it is stuck, or its report is not recognised",
			name, budget)
		return
	end

	if verdict == "pass" then
		results:add(name, report.PASS, summarise(text))
	else
		results:add(name, report.FAIL, summarise(text))
	end

	results:print_results(true)
	results:verdict(baseline)
end
