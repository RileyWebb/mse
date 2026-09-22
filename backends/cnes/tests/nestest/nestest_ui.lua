-- nestest driven through its own menu, the way a person would run it.
--
-- Automation mode ($C000) bypasses the reset vector, the PPU and the
-- controller entirely, and collapses ~400 tests into two bytes. Running the ROM
-- normally exercises all of that and reports a result per test group, because
-- nestest draws each group's outcome over the "--" placeholder in its menu.
--
--   --arg page=normal|invalid   which menu page to run (Select switches pages)

package.path = "./?.lua;" .. package.path

local harness = require("lib.harness")
local report  = require("lib.report")
local screen  = require("lib.screen")
local input   = require("lib.input")
local codes   = require("nestest.nestest_codes")

local BUTTON = harness.BUTTON

local BOOT_FRAMES        = 60      -- for the menu to be drawn
local RESULT_POLL_FRAMES = 600     -- the whole page runs in well under this
local PLACEHOLDER        = "--"

-- Finds the menu entries by their "--" result placeholder, so nothing depends
-- on where on screen the ROM happens to draw them.
local function find_menu_entries()
	local entries = {}
	for row = 0, screen.ROWS - 1 do
		local text = screen.row_text(0, row)
		local col  = text:find(PLACEHOLDER, 1, true)
		if col then
			local name = text:sub(col + #PLACEHOLDER):match("^%s*(.-)%s*$")
			if name ~= "" then
				entries[#entries + 1] = { row = row, col = col - 1, name = name }
			end
		end
	end
	return entries
end

local function read_result(entry)
	return screen.row_text(0, entry.row):sub(entry.col + 1, entry.col + 2)
end

-- An on-screen code is the last failure in that group. Which of $02/$03 holds
-- it depends on the group, so report every documented reading of the value.
local function describe_code(value)
	local meanings = {}
	for _, address in ipairs({ 0x02, 0x03 }) do
		local text = codes[address] and codes[address][value]
		if text then
			meanings[#meanings + 1] = string.format("$%02X: %s", address, text)
		end
	end

	if #meanings == 0 then
		return "undocumented code - see nestest.txt"
	end
	return table.concat(meanings, " | ")
end

function onrun()
	local page = harness.arg("page", "normal")
	if page ~= "normal" and page ~= "invalid" then
		harness.die("--arg page expects 'normal' or 'invalid', got '%s'", page)
		return
	end

	run_frames(BOOT_FRAMES)

	if page == "invalid" then
		-- Select toggles between the official and "invalid opcode" pages.
		input.sequence(0):tap(BUTTON.SELECT, 4, 8):wait(20):run()
	end

	local entries = find_menu_entries()
	if #entries == 0 then
		screen.dump(0, "(no menu found)")
		harness.finish(false,
			"nestest drew no menu after %d frames - the ROM never reached its main loop, or nothing was rendered",
			BOOT_FRAMES)
		return
	end

	harness.info("nestest menu (%s page): %d entries, cursor on '%s'",
		page, #entries, entries[1].name)

	-- The cursor starts on the first entry, which is "Run all tests".
	input.sequence(0):tap(BUTTON.START, 4, 4):run()

	local summary = entries[1]
	local finished = harness.run_until(function()
		return read_result(summary) ~= PLACEHOLDER
	end, RESULT_POLL_FRAMES)

	if not finished then
		screen.dump(0, "(timed out waiting for results)")
		harness.finish(false,
			"no result appeared for '%s' within %d frames of pressing Start",
			summary.name, RESULT_POLL_FRAMES)
		return
	end

	-- Give the slower groups time to fill in behind the summary cell.
	run_frames(60)

	local results = report.new(string.format("nestest menu (%s page)", page))

	for index, entry in ipairs(entries) do
		local cell = read_result(entry)
		local name = string.format("%s/%s", page, entry.name)

		if index == 1 then
			-- The first entry summarises the page: "OK" or "Er".
			if cell == "OK" then
				results:add(name, report.PASS, "every test on the page passed")
			elseif cell == "Er" then
				results:add(name, report.FAIL, "at least one test on the page failed")
			else
				results:add(name, report.FAIL,
					string.format("unexpected summary cell '%s'", cell))
			end
		elseif cell == "OK" then
			results:add(name, report.PASS)
		elseif cell == PLACEHOLDER then
			results:add(name, report.SKIP, "never ran")
		else
			local value = tonumber(cell, 16)
			if value then
				results:add(name, report.FAIL,
					string.format("code 0x%02X - %s", value, describe_code(value)))
			else
				results:add(name, report.FAIL,
					string.format("unreadable result cell '%s'", cell))
			end
		end
	end

	results:print_results(true)

	if harness.arg_flag("dump", false) then
		screen.dump(0, "(final)")
	end

	results:verdict(string.format("baseline/nestest_ui_%s.txt", page))
end
