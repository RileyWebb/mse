-- nestest against the golden CPU trace.
--
-- nestest.log is Nintendulator's instruction-by-instruction trace of the same
-- automation run. Comparing against it pinpoints the exact instruction where
-- this core first diverges from correct behaviour, which the $02/$03 result
-- codes cannot do -- they only say which group failed, thousands of
-- instructions later.
--
--   --arg log=<path>      trace to compare against
--   --arg cycles=1        also compare the cycle count (timing, not just logic)
--   --arg ppu=1           also compare PPU scanline/dot. Off by default: the log
--                         numbers the power-on PPU position by Nintendulator's
--                         convention (0,21 at CYC:7) while this core reports the
--                         pre-render line as 261,0, so the columns disagree from
--                         the first instruction for reasons unrelated to accuracy.
--   --arg context=<n>     instructions of history to print around a mismatch

package.path = "./?.lua;" .. package.path

local harness = require("lib.harness")

-- The log's CYC column starts at 7: the reset sequence the trace does not show.
local DEFAULT_CYCLE_OFFSET = 7

local DEFAULT_LOG      = "nestest/nestest.log"
-- Each comparison mode measures something different, so each keeps its own
-- record of how far it got.
local function baseline_path(compare_cycles, compare_ppu)
	local suffix = ""
	if compare_cycles then suffix = suffix .. "_cycles" end
	if compare_ppu then suffix = suffix .. "_ppu" end
	return "baseline/nestest_trace" .. suffix .. ".txt"
end
local DEFAULT_CONTEXT  = 8

local function parse_line(line)
	local pc = tonumber(line:sub(1, 4), 16)
	if not pc then
		return nil
	end

	local a, x, y, p, sp = line:match("A:(%x%x) X:(%x%x) Y:(%x%x) P:(%x%x) SP:(%x%x)")
	if not a then
		return nil
	end

	local scanline, dot = line:match("PPU:%s*(%d+),%s*(%d+)")
	local cycles = line:match("CYC:(%d+)")

	local bytes = {}
	for byte in line:sub(7, 14):gmatch("(%x%x)") do
		bytes[#bytes + 1] = tonumber(byte, 16)
	end

	return {
		pc       = pc,
		bytes    = bytes,
		disasm   = (line:sub(17, 47):gsub("%s+$", "")),
		a        = tonumber(a, 16),
		x        = tonumber(x, 16),
		y        = tonumber(y, 16),
		p        = tonumber(p, 16),
		sp       = tonumber(sp, 16),
		scanline = scanline and tonumber(scanline),
		dot      = dot and tonumber(dot),
		cycles   = cycles and tonumber(cycles),
		text     = line,
	}
end

local function load_trace(path)
	local file = io.open(path, "r")
	if not file then
		harness.die("golden trace not found: %s", path)
		return nil
	end

	local entries = {}
	for line in file:lines() do
		local entry = parse_line(line)
		if entry then
			entries[#entries + 1] = entry
		end
	end
	file:close()

	if #entries == 0 then
		harness.die("golden trace '%s' contained no parsable lines", path)
	end

	return entries
end

-- The baseline is one number: how far the trace matched last time. A run that
-- gets further is progress; a run that gets less far is a regression.
local function load_baseline(path)
	local file = io.open(path, "r")
	if not file then
		return nil
	end
	local matched
	for line in file:lines() do
		matched = matched or tonumber(line:match("^matched%s*=%s*(%d+)"))
	end
	file:close()
	return matched
end

local function save_baseline(path, matched, total)
	local file, err = io.open(path, "w")
	if not file then
		harness.die("could not write baseline '%s': %s", path, tostring(err))
		return
	end
	file:write("# How many instructions of nestest.log this core reproduces.\n")
	file:write("# Regenerate with: --arg update_baseline=1\n")
	file:write(string.format("# Full trace is %d instructions.\n", total))
	file:write(string.format("matched = %d\n", matched))
	file:close()
end

local function actual_state()
	return {
		pc = get_pc(),
		a  = get_a(),
		x  = get_x(),
		y  = get_y(),
		p  = get_status(),
		sp = get_sp(),
	}
end

local function compare(expected, actual, compare_cycles, cycle_offset, compare_ppu)
	local diffs = {}

	local function check(label, want, got, width)
		if want ~= nil and want ~= got then
			diffs[#diffs + 1] = string.format("%s expected %0" .. width .. "X, got %0" .. width .. "X",
				label, want, got)
		end
	end

	check("PC", expected.pc, actual.pc, 4)
	check("A", expected.a, actual.a, 2)
	check("X", expected.x, actual.x, 2)
	check("Y", expected.y, actual.y, 2)
	check("P", expected.p, actual.p, 2)
	check("SP", expected.sp, actual.sp, 2)

	if compare_cycles and expected.cycles then
		local got = get_cycles() + cycle_offset
		if expected.cycles ~= got then
			diffs[#diffs + 1] = string.format("CYC expected %d, got %d (off by %d)",
				expected.cycles, got, got - expected.cycles)
		end
	end

	if compare_ppu and expected.scanline then
		local scanline, dot = get_ppu_position()
		if expected.scanline ~= scanline or expected.dot ~= dot then
			diffs[#diffs + 1] = string.format("PPU expected %d,%d got %d,%d",
				expected.scanline, expected.dot, scanline, dot)
		end
	end

	return diffs
end

function onstart()
	set_pc(0xC000)
	set_a(0x00)
	set_x(0x00)
	set_y(0x00)
	set_sp(0xFD)
	set_status(0x24)
end

function onrun()
	local log_path       = harness.arg("log", DEFAULT_LOG)
	local compare_cycles = harness.arg_flag("cycles", false)
	local compare_ppu    = harness.arg_flag("ppu", false)
	local context        = harness.arg_number("context", DEFAULT_CONTEXT)
	local baseline_file  = baseline_path(compare_cycles, compare_ppu)
	local cycle_offset   = harness.arg_number("cyc_offset", DEFAULT_CYCLE_OFFSET)

	local trace = load_trace(log_path)
	harness.info("nestest trace: %d instructions from %s (cycles=%s, ppu=%s)",
		#trace, log_path, tostring(compare_cycles), tostring(compare_ppu))

	local matched  = 0
	local mismatch = nil

	for index, expected in ipairs(trace) do
		local diffs = compare(expected, actual_state(), compare_cycles, cycle_offset, compare_ppu)
		if #diffs > 0 then
			mismatch = { index = index, expected = expected, diffs = diffs }
			break
		end
		matched = matched + 1
		step()
	end

	if mismatch then
		local first = math.max(1, mismatch.index - context)
		harness.info("")
		harness.info("First divergence at trace line %d of %d:", mismatch.index, #trace)
		harness.info("")
		harness.info("  last %d instructions that matched:", mismatch.index - first)
		for i = first, mismatch.index - 1 do
			harness.info("    %5d  %s", i, trace[i].text)
		end
		harness.info("")
		harness.info("  expected: %5d  %s", mismatch.index, mismatch.expected.text)
		-- Cycles are shown on the log's scale so the two lines line up.
		harness.info("  actual:          PC:%04X A:%02X X:%02X Y:%02X P:%02X SP:%02X CYC:%d",
			get_pc(), get_a(), get_x(), get_y(), get_status(), get_sp(),
			get_cycles() + cycle_offset)
		harness.info("")
		for _, diff in ipairs(mismatch.diffs) do
			harness.info("    %s", diff)
		end
		harness.info("")
	end

	harness.info("nestest trace: matched %d of %d instructions (%.2f%%)",
		matched, #trace, 100 * matched / #trace)

	if harness.arg_flag("update_baseline", false) then
		save_baseline(baseline_file, matched, #trace)
		harness.finish(true, "baseline updated: %s matched %d of %d",
			baseline_file, matched, #trace)
		return
	end

	if matched == #trace then
		harness.finish(true, "trace matches nestest.log exactly (%d instructions)", matched)
		return
	end

	if harness.arg_flag("strict", false) then
		harness.finish(false, "diverged from nestest.log at instruction %d of %d",
			matched + 1, #trace)
		return
	end

	local recorded = load_baseline(baseline_file)
	if not recorded then
		harness.info("NOTE: no baseline at '%s' - create one with --arg update_baseline=1",
			baseline_file)
		harness.finish(false, "diverged from nestest.log at instruction %d of %d and there is no baseline",
			matched + 1, #trace)
		return
	end

	if matched < recorded then
		harness.finish(false,
			"REGRESSION: trace used to match %d instructions, now only %d (lost %d)",
			recorded, matched, recorded - matched)
	elseif matched > recorded then
		harness.finish(true,
			"progress: %d instructions matched, up from %d - refresh with --arg update_baseline=1",
			matched, recorded)
	else
		harness.finish(true, "no regression: still matches %d of %d instructions", matched, #trace)
	end
end
