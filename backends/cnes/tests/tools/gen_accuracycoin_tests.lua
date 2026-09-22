-- Generates AccuracyCoin/accuracycoin_codes.lua from AccuracyCoin's README.md.
--
-- The ROM prints a bare failure code such as "FAIL 4"; the README says what
-- each code means, per test. Generating the lookup means a failing CTest run
-- explains itself instead of leaving you to go and read the README.
--
-- Usage: luajit tools/gen_accuracycoin_tests.lua [README.md] [out.lua]

local source_path = arg[1] or "AccuracyCoin/README.md"
local output_path = arg[2] or "AccuracyCoin/accuracycoin_codes.lua"

-- Must match normalise() in accuracycoin.lua: the ROM draws names in its own
-- uppercase font with its own punctuation, so both sides are reduced to
-- uppercase words before they are compared.
local function normalise(name)
	return (name:upper():gsub("[^%w]+", " "):gsub("^%s+", ""):gsub("%s+$", ""))
end

local source = assert(io.open(source_path, "r"), "cannot open " .. source_path)

local pages      = {}   -- page number -> { name = , tests = { {name, order, codes} } }
local page_order = {}
local current    = nil  -- list of page tables the current section applies to
local test       = nil

local function page_for(number, name)
	if not pages[number] then
		pages[number] = { name = name, tests = {}, index = {} }
		page_order[#page_order + 1] = number
	elseif name then
		pages[number].name = name
	end
	return pages[number]
end

for line in source:lines() do
	local heading = line:match("^##%s+(.-)%s*$")
	local subhead = line:match("^###%s+(.-)%s*$")

	if heading then
		test = nil
		current = {}

		-- "Page 12: CPU Interrupts"
		local number, name = heading:match("^Page%s+(%d+):%s*(.+)$")
		if number then
			current[#current + 1] = page_for(tonumber(number), name)
		else
			-- "Pages 3, 4, 5, ... and 11: Unofficial Instructions"
			local numbers, shared_name = heading:match("^Pages%s+([%d,%s and]+):%s*(.+)$")
			if numbers then
				for each in numbers:gmatch("%d+") do
					current[#current + 1] = page_for(tonumber(each), shared_name)
				end
			else
				current = nil
			end
		end
	elseif subhead and current then
		test = {}
		for _, page in ipairs(current) do
			local key = normalise(subhead)
			local entry = page.index[key]
			if not entry then
				entry = { name = subhead, key = key, codes = {}, order = {} }
				page.index[key] = entry
				page.tests[#page.tests + 1] = entry
			end
			test[#test + 1] = entry
		end
	elseif test then
		-- "  4: The Y register was not the correct value after the test."
		local code, text = line:match("^%s%s([%w]):%s+(.-)%s*$")
		if code then
			for _, entry in ipairs(test) do
				local existing = entry.codes[code]
				if existing then
					if existing ~= text then
						-- Some codes have two documented readings (for example a
						-- composite-PPU and an RGB-PPU variant). Keep both.
						entry.codes[code] = existing .. "  -OR-  " .. text
					end
				else
					entry.codes[code] = text
					entry.order[#entry.order + 1] = code
				end
			end
		end
	end
end

source:close()

table.sort(page_order)

local total_tests, total_codes = 0, 0
for _, number in ipairs(page_order) do
	for _, entry in ipairs(pages[number].tests) do
		total_tests = total_tests + 1
		total_codes = total_codes + #entry.order
	end
end

if total_tests < 80 then
	error(string.format("only parsed %d tests from %s - format changed?", total_tests, source_path))
end

local function quote(text)
	return '"' .. text:gsub("\\", "\\\\"):gsub('"', '\\"') .. '"'
end

local out = assert(io.open(output_path, "w"), "cannot write " .. output_path)

out:write("-- GENERATED FILE - do not edit.\n")
out:write("-- Source: ", source_path, "\n")
out:write("-- Regenerate: luajit tools/gen_accuracycoin_tests.lua\n")
out:write("--\n")
out:write("-- AccuracyCoin failure codes per page and test, as documented in the README.\n")
out:write("-- Names are normalised (uppercase, punctuation collapsed to spaces) so they\n")
out:write("-- can be matched against the names the ROM draws in its own font.\n\n")
out:write("return {\n")

for _, number in ipairs(page_order) do
	local page = pages[number]
	out:write(string.format("\t[%d] = {\n", number))
	out:write(string.format("\t\tname = %s,\n", quote(page.name)))
	out:write("\t\ttests = {\n")
	for _, entry in ipairs(page.tests) do
		out:write("\t\t\t{\n")
		out:write(string.format("\t\t\t\tname = %s,\n", quote(entry.name)))
		out:write(string.format("\t\t\t\tkey = %s,\n", quote(entry.key)))
		out:write("\t\t\t\tcodes = {\n")
		for _, code in ipairs(entry.order) do
			out:write(string.format("\t\t\t\t\t[%s] = %s,\n", quote(code), quote(entry.codes[code])))
		end
		out:write("\t\t\t\t},\n")
		out:write("\t\t\t},\n")
	end
	out:write("\t\t},\n")
	out:write("\t},\n")
end

out:write("}\n")
out:close()

print(string.format("wrote %s: %d pages, %d documented tests, %d codes",
	output_path, #page_order, total_tests, total_codes))
