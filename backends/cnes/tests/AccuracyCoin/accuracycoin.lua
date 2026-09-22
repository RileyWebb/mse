-- AccuracyCoin, driven through its own menus.
--
-- The ROM draws a verdict next to every test name: "PASS", "FAIL <code>", or
-- "DRAW" for the entries that only display information. Reading the menu gives
-- the test name and the failure code together, and accuracycoin_codes.lua turns
-- the code into the explanation from the ROM's README.
--
-- One script covers every page:
--   --arg page=<1-20>   run that page (cursor at the page index, A runs the page)
--   --arg page=all      run all 20 pages in one session
--   --arg dump=1        dump the screen for each page as it finishes

package.path = "./?.lua;" .. package.path

local harness = require("lib.harness")
local report  = require("lib.report")
local screen  = require("lib.screen")
local input   = require("lib.input")

local codes = require("AccuracyCoin.accuracycoin_codes")
screen.set_charset(require("AccuracyCoin.accuracycoin_font"))

local BUTTON = harness.BUTTON

local PAGE_COUNT  = 20
local BOOT_FRAMES = 120   -- for the first menu to be drawn
local FIRST_ROW   = 7     -- first menu row; entries are on every other row
local ROW_STRIDE  = 2

-- Column layout of a menu row, verified against the ROM:
--   " PASS   ROM IS NOT WRITABLE"
--   " FAIL 4 DUMMY READ CYCLES"
local STATUS_FIRST = 2    -- 1-based, into the decoded row text
local STATUS_LAST  = 5
local CODE_COL     = 7
local NAME_FIRST   = 9

local STATUS_PENDING = "TEST"
local STATUS_RUNNING = "...."  -- the test executing right now, drawn highlighted
local STATUS_PASS    = "PASS"
local STATUS_FAIL    = "FAIL"
local STATUS_DRAW    = "DRAW"

local KNOWN_STATUS = {
	[STATUS_PENDING] = true,
	[STATUS_RUNNING] = true,
	[STATUS_PASS]    = true,
	[STATUS_FAIL]    = true,
	[STATUS_DRAW]    = true,
}

local POLL_INTERVAL = 30

-- A test that wedges the console would otherwise burn the whole CTest timeout,
-- so a page is abandoned once no test has finished for this long. The slowest
-- page in the ROM completes all of its tests inside ~1200 frames, so a minute
-- of emulated time with nothing finishing means something is stuck.
local STALL_FRAMES = 3600

-- Backstop for a page that keeps finishing tests but never runs out of them.
local PAGE_FRAME_BUDGET = 20000

-- Shortest shared prefix that is allowed to identify a test by name.
local MIN_PREFIX_MATCH = 10

-- Must match normalise() in tools/gen_accuracycoin_tests.lua.
local function normalise(name)
	return (name:upper():gsub("[^%w]+", " "):gsub("^%s+", ""):gsub("%s+$", ""))
end

local function page_header()
	return (screen.row_text(0, 3):gsub("^%s+", ""):gsub("%s+$", ""))
end

-- The ROM draws "PAGE  7 / 20" as its second header line.
local function current_page()
	return tonumber(screen.row_text(0, 5):match("PAGE%s+(%d+)%s*/"))
end

local function menu_rows()
	local rows = {}
	for row = FIRST_ROW, screen.ROWS - 1, ROW_STRIDE do
		local text   = screen.row_text(0, row)
		local status = text:sub(STATUS_FIRST, STATUS_LAST)
		local name   = text:sub(NAME_FIRST):gsub("%s+$", "")

		if name ~= "" and KNOWN_STATUS[status] then
			rows[#rows + 1] = {
				row    = row,
				status = status,
				code   = text:sub(CODE_COL, CODE_COL),
				name   = name,
			}
		end
	end
	return rows
end

-- Looks up the README text for a code. Names on screen are abbreviated versions
-- of the README headings ("ABSOLUTE INDEXED" for "Absolute Indexed
-- Wraparound"), so fall back to a containment match, which is unambiguous
-- within a single page.
local function describe(page_number, test_name, code)
	local page = codes[page_number]
	if not page then
		return nil
	end

	local key   = normalise(test_name)
	local match = nil

	for _, entry in ipairs(page.tests) do
		if entry.key == key then
			match = entry
			break
		end
	end

	if not match then
		local candidates = {}
		for _, entry in ipairs(page.tests) do
			if entry.key:find(key, 1, true) or key:find(entry.key, 1, true) then
				candidates[#candidates + 1] = entry
			end
		end
		if #candidates == 1 then
			match = candidates[1]
		end
	end

	-- The menu abbreviates where a name would not fit ("STALE SPRITE SHIFT
	-- REGS" for "Stale Sprite Shift Registers"), which containment misses. Fall
	-- back to the longest shared prefix, as long as one entry wins outright.
	if not match then
		local best, best_length, runner_up = nil, 0, 0
		for _, entry in ipairs(page.tests) do
			local shared = 0
			while shared < #key and shared < #entry.key
				and key:byte(shared + 1) == entry.key:byte(shared + 1) do
				shared = shared + 1
			end
			if shared > best_length then
				best, runner_up, best_length = entry, best_length, shared
			elseif shared > runner_up then
				runner_up = shared
			end
		end
		if best_length >= MIN_PREFIX_MATCH and best_length > runner_up then
			match = best
		end
	end

	-- Pages 3-11 share one code table covering every unofficial instruction.
	if not match and #page.tests == 1 then
		match = page.tests[1]
	end

	return match and match.codes[code] or nil
end

local function boot()
	run_frames(BOOT_FRAMES)
	return current_page() ~= nil
end

-- Walks to a page with the cursor on the page index, where left/right change
-- pages. Each press is verified, because a page that has just finished running
-- can still be busy and drop the first input.
local function navigate_to_page(target)
	for _ = 1, PAGE_COUNT * 2 do
		local page = current_page()

		if page == target then
			return true
		end

		if not page then
			return false
		end

		input.sequence(0):tap(BUTTON.RIGHT, 4, 12):run()
	end

	return false
end

-- Runs every test on the current page. Returns the final menu rows and one of
-- "done", "stalled" (no test finished for a long time) or "budget".
local function run_page()
	input.sequence(0):tap(BUTTON.A, 4, 4):run()

	local frames        = 0
	local completed     = 0
	local last_progress = 0

	while frames < PAGE_FRAME_BUDGET do
		run_frames(POLL_INTERVAL)
		frames = frames + POLL_INTERVAL

		local rows    = menu_rows()
		local pending = 0
		local done    = 0

		for _, entry in ipairs(rows) do
			if entry.status == STATUS_PENDING or entry.status == STATUS_RUNNING then
				pending = pending + 1
			else
				done = done + 1
			end
		end

		if pending == 0 then
			-- Let the last verdict settle before reading it back.
			run_frames(POLL_INTERVAL)
			return menu_rows(), "done", frames
		end

		if done > completed then
			completed     = done
			last_progress = frames
		elseif frames - last_progress >= STALL_FRAMES then
			return rows, "stalled", frames
		end
	end

	return menu_rows(), "budget", frames
end

local function record(results, page_number, entry)
	local name = string.format("page %02d/%s", page_number, entry.name)

	if entry.status == STATUS_PASS then
		return results:add(name, report.PASS)
	elseif entry.status == STATUS_DRAW then
		-- "DRAW" entries only display information; there is nothing to pass.
		return results:add(name, report.SKIP, "informational, not a test")
	elseif entry.status == STATUS_RUNNING then
		return results:add(name, report.FAIL,
			"the console stopped responding while this test was running")
	elseif entry.status == STATUS_PENDING then
		return results:add(name, report.SKIP, "never ran - an earlier test on the page hung")
	elseif entry.status == STATUS_FAIL then
		local text = describe(page_number, entry.name, entry.code)
		return results:add(name, report.FAIL,
			string.format("code %s - %s", entry.code, text or "see AccuracyCoin/README.md"))
	end

	return results:add(name, report.FAIL,
		string.format("unrecognised status %q", entry.status))
end

-- Returns false if the console was left unusable and needs a reset.
local function collect_page(results, page_number, dump, show_tally)
	if not navigate_to_page(page_number) then
		results:add(string.format("page %02d", page_number), report.FAIL,
			"could not reach this page - the menu stopped responding")
		return false
	end

	local header = page_header()
	local rows, outcome, frames = run_page()

	harness.info("page %2d %-32s %2d tests, %5d frames%s",
		page_number, header, #rows, frames,
		outcome == "done" and "" or ("  <- " .. outcome:upper()))

	if dump then
		screen.dump(0, string.format("(page %d after running)", page_number))
	end

	if #rows == 0 then
		results:print_entry(results:add(string.format("page %02d/%s", page_number, header),
			report.FAIL, "the page drew no test entries at all"))
		return false
	end

	-- Printed as the page finishes rather than collected into one block at the
	-- end, so every result sits under the page heading it belongs to.
	local counts = { [report.PASS] = 0, [report.FAIL] = 0, [report.SKIP] = 0 }

	for _, entry in ipairs(rows) do
		local result = record(results, page_number, entry)
		counts[result.status] = counts[result.status] + 1
		results:print_entry(result)
	end

	if show_tally then
		harness.info("       page %2d: %d passed, %d failed, %d skipped",
			page_number, counts[report.PASS], counts[report.FAIL], counts[report.SKIP])
	end

	return outcome == "done"
end

function onrun()
	local page_arg = harness.arg("page", "all")
	local dump     = harness.arg_flag("dump", false)

	if not boot() then
		screen.dump(0, "(boot)")
		harness.finish(false, "AccuracyCoin drew no menu within %d frames", BOOT_FRAMES)
		return
	end

	local first, last, title, baseline
	if page_arg == "all" then
		first, last = 1, PAGE_COUNT
		title       = "AccuracyCoin (all pages)"
		baseline    = "baseline/accuracycoin_all.txt"
	else
		local number = tonumber(page_arg)
		if not number or number < 1 or number > PAGE_COUNT then
			harness.die("--arg page expects 1-%d or %q, got %q", PAGE_COUNT, "all", page_arg)
			return
		end
		first, last = number, number
		title       = string.format("AccuracyCoin page %d", number)
		baseline    = string.format("baseline/accuracycoin_page_%02d.txt", number)
	end

	local results = report.new(title)

	for page_number = first, last do
		local healthy = collect_page(results, page_number, dump, first ~= last)

		if not healthy and page_number < last then
			-- A hung test leaves the ROM unable to take input. Power-cycle so the
			-- remaining pages still get run and reported.
			harness.info("       recovering: resetting the console before page %d",
				page_number + 1)
			reset()
			if not boot() then
				harness.info("       reset did not bring the menu back; stopping here")
				break
			end
		end
	end

	-- Results were printed under their page heading as each page finished.
	results:verdict(baseline)
end
