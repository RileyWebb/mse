-- Result collection and baseline comparison.
--
-- An emulator under development fails a lot of accuracy tests, and a suite that
-- is permanently red reports nothing. So the default verdict is a regression
-- check against a recorded baseline: a test fails when something that used to
-- pass stops passing. Newly-passing tests are reported as a stale baseline.
--
--   --arg strict=1           every failing test fails the run
--   --arg update_baseline=1  rewrite the baseline from this run and pass

local harness = require("lib.harness")

local report = {}
local Report = {}
Report.__index = Report

report.PASS = "PASS"
report.FAIL = "FAIL"
report.SKIP = "SKIP"

function report.new(title)
	return setmetatable({
		title   = title or "cNES test",
		results = {},
		index   = {},
	}, Report)
end

function Report:add(name, status, detail)
	if self.index[name] then
		-- Names key the baseline, so a duplicate would silently shadow a result.
		harness.die("duplicate result name: %s", name)
	end

	local entry = { name = name, status = status, detail = detail }
	self.results[#self.results + 1] = entry
	self.index[name] = entry
	return entry
end

function Report:counts()
	local counts = { PASS = 0, FAIL = 0, SKIP = 0 }
	for _, entry in ipairs(self.results) do
		counts[entry.status] = (counts[entry.status] or 0) + 1
	end
	return counts
end

local function format_entry(entry)
	if entry.detail then
		return string.format("[%s] %s: %s", entry.status, entry.name, entry.detail)
	end
	return string.format("[%s] %s", entry.status, entry.name)
end

-- Prints one result as it is recorded, for tests that report progressively
-- rather than in one block at the end.
function Report:print_entry(entry)
	print(format_entry(entry))
end

function Report:print_results(show_passes)
	for _, entry in ipairs(self.results) do
		if entry.status ~= report.PASS or show_passes then
			print(format_entry(entry))
		end
	end
end

-- Baseline files are "<STATUS><tab><test name>", '#' comments ignored.

local function baseline_load(path)
	local file = io.open(path, "r")
	if not file then
		return nil
	end

	local entries = {}
	for line in file:lines() do
		if not line:match("^%s*#") and not line:match("^%s*$") then
			local status, name = line:match("^(%u+)\t(.+)$")
			if status then
				entries[name] = status
			end
		end
	end
	file:close()
	return entries
end

local function baseline_save(path, results, title)
	local file, err = io.open(path, "w")
	if not file then
		return nil, err
	end

	file:write("# Recorded results for ", title, "\n")
	file:write("# Regenerate with: --arg update_baseline=1\n")
	file:write("# A test fails when an entry recorded PASS here stops passing.\n")
	for _, entry in ipairs(results) do
		file:write(entry.status, "\t", entry.name, "\n")
	end
	file:close()
	return true
end

-- Ends the run with a verdict. Returns nothing; the process exits.
function Report:verdict(baseline_path)
	local counts = self:counts()
	local total  = #self.results

	if total == 0 then
		harness.finish(false, "%s produced no results at all", self.title)
		return
	end

	print(string.format("SUMMARY %s: %d passed, %d failed, %d skipped (of %d)",
		self.title, counts.PASS, counts.FAIL, counts.SKIP, total))

	if harness.arg_flag("update_baseline", false) then
		if not baseline_path then
			harness.die("update_baseline requested but this test has no baseline file")
			return
		end
		local ok, err = baseline_save(baseline_path, self.results, self.title)
		if not ok then
			harness.die("could not write baseline '%s': %s", baseline_path, tostring(err))
			return
		end
		harness.finish(true, "baseline updated: %s (%d entries)", baseline_path, total)
		return
	end

	if harness.arg_flag("strict", false) or not baseline_path then
		if counts.FAIL > 0 then
			harness.finish(false, "%d of %d tests failed", counts.FAIL, total)
		else
			harness.finish(true, "%d tests passed", counts.PASS)
		end
		return
	end

	local baseline = baseline_load(baseline_path)
	if not baseline then
		print(string.format("NOTE: no baseline at '%s' - create one with --arg update_baseline=1",
			baseline_path))
		if counts.FAIL > 0 then
			harness.finish(false, "%d of %d tests failed and there is no baseline to compare against",
				counts.FAIL, total)
		else
			harness.finish(true, "%d tests passed", counts.PASS)
		end
		return
	end

	local regressions = {}
	local fixes       = {}
	local unrecorded  = {}

	for _, entry in ipairs(self.results) do
		local was = baseline[entry.name]
		if was == nil then
			if entry.status ~= report.PASS then
				unrecorded[#unrecorded + 1] = entry
			end
		elseif was == report.PASS and entry.status ~= report.PASS then
			regressions[#regressions + 1] = entry
		elseif was ~= report.PASS and entry.status == report.PASS then
			fixes[#fixes + 1] = entry
		end
	end

	local missing = {}
	for name in pairs(baseline) do
		if not self.index[name] then
			missing[#missing + 1] = name
		end
	end
	table.sort(missing)

	for _, entry in ipairs(regressions) do
		print(string.format("REGRESSION %s: %s", entry.name, entry.detail or "now failing"))
	end
	for _, entry in ipairs(unrecorded) do
		print(string.format("NEW FAILURE %s: %s (not in the baseline)",
			entry.name, entry.detail or "failing"))
	end
	for _, entry in ipairs(fixes) do
		print(string.format("FIXED %s (baseline says this used to fail)", entry.name))
	end
	for _, name in ipairs(missing) do
		print(string.format("MISSING %s was in the baseline but this run produced no result", name))
	end

	if #fixes > 0 and #regressions == 0 and #unrecorded == 0 then
		print(string.format("NOTE: %d test(s) now pass - refresh with --arg update_baseline=1", #fixes))
	end

	local broken = #regressions + #unrecorded
	if broken > 0 then
		harness.finish(false, "%d regression(s) against %s", broken, baseline_path)
	else
		harness.finish(true, "no regressions against %s (%d passing, %d known failing)",
			baseline_path, counts.PASS, counts.FAIL + counts.SKIP)
	end
end

return report
