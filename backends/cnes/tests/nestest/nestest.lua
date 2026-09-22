-- nestest in automation mode.
--
-- nestest.txt: "This test program, when run on automation (i.e. set your program
-- counter to 0C000h) will perform all tests in sequence and shove the results of
-- the tests into locations 02h and 03h." A non-zero byte is the code of the last
-- test that failed in that group; nestest_codes.lua turns it back into English.
--
-- The whole automated run is ~26,555 CPU cycles, so this finishes in a moment
-- rather than needing a wall-clock guess at how many frames to emulate.

package.path = "./?.lua;" .. package.path

local harness = require("lib.harness")
local report  = require("lib.report")
local codes   = require("nestest.nestest_codes")

-- The golden trace ends at CYC:26554; allow headroom for a core that burns
-- extra cycles on the way.
local DEFAULT_CYCLE_BUDGET = 40000

local ERROR_BYTES = { 0x02, 0x03 }

function onstart()
	-- Automation entry point and documented power-on register state.
	set_pc(0xC000)
	set_a(0x00)
	set_x(0x00)
	set_y(0x00)
	set_sp(0xFD)
	set_status(0x24)
end

local function describe(address, value)
	local table_for_byte = codes[address] or {}
	local text = table_for_byte[value]
	if text then
		return text
	end
	return "undocumented code - see nestest.txt"
end

function onrun()
	local budget = harness.arg_number("cycles", DEFAULT_CYCLE_BUDGET)

	harness.info("nestest: automation mode from $C000, %d cycle budget", budget)

	-- Run the automated sequence. A CPU that wedges shows up as a PC that stops
	-- moving, which is worth calling out separately from a test failure.
	local last_pc      = get_pc()
	local stuck_steps  = 0
	local steps        = 0

	while get_cycles() < budget do
		step()
		steps = steps + 1

		local pc = get_pc()
		if pc == last_pc then
			stuck_steps = stuck_steps + 1
			if stuck_steps > 16 then
				harness.info("nestest: CPU is spinning at %s after %d instructions",
					harness.hex16(pc), steps)
				break
			end
		else
			stuck_steps = 0
			last_pc = pc
		end
	end

	harness.info("nestest: ran %d instructions, %d cycles, ended at %s",
		steps, get_cycles(), harness.hex16(get_pc()))

	local results = report.new("nestest")

	for _, address in ipairs(ERROR_BYTES) do
		local value = mem_read(address)
		local name  = string.format("error byte %s", harness.hex8(address))

		if value == 0 then
			results:add(name, report.PASS, "no failures reported")
		else
			results:add(name, report.FAIL,
				string.format("code 0x%02X - %s", value, describe(address, value)))
		end
	end

	results:print_results(true)
	results:verdict("baseline/nestest.txt")
end
