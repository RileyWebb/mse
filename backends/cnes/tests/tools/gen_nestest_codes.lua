-- Generates nestest/nestest_codes.lua from Kevtris's nestest.txt.
--
-- nestest reports only a failure code in $02/$03; the documentation that says
-- what each code means ships with the ROM. Generating the lookup keeps the two
-- in step instead of transcribing ~500 entries by hand.
--
-- Usage: luajit tools/gen_nestest_codes.lua [nestest.txt] [out.lua]

local source_path = arg[1] or "nestest/nestest.txt"
local output_path = arg[2] or "nestest/nestest_codes.lua"

local source = assert(io.open(source_path, "r"), "cannot open " .. source_path)

local codes = { [0x02] = {}, [0x03] = {} }
local order = { [0x02] = {}, [0x03] = {} }
local target = nil
local in_invalid_section = false

for line in source:lines() do
	local trimmed = line:match("^%s*(.-)%s*$")

	if trimmed:match("^%(byte 02h only%)$") then
		target = 0x02
	elseif trimmed:match("^%(error byte location 03h starts here%)$") then
		target = 0x03
	elseif trimmed:match("^Invalid opcode tests") then
		-- "all errors are reported in byte 03h unless specified"
		in_invalid_section = true
		target = 0x03
	elseif trimmed:match("%(error byte 02h%)") then
		target = 0x02
	elseif in_invalid_section and trimmed:match('^[%u%d]+ %- "invalid" opcode tests?$') then
		target = 0x03
	elseif target then
		-- e.g. "001h - BCS failed to branch"
		local hex, text = trimmed:match("^(%x%x%x)h %- (.+)$")
		if hex then
			local value = tonumber(hex, 16)
			local existing = codes[target][value]
			if existing then
				if existing ~= text then
					-- The invalid-opcode tables reuse low codes already taken by
					-- the legal-opcode tables, so a code can be genuinely
					-- ambiguous. Keep both readings rather than picking one.
					codes[target][value] = existing .. "  -OR-  " .. text
				end
			else
				codes[target][value] = text
				order[target][#order[target] + 1] = value
			end
		end
	end
end

source:close()

local total = #order[0x02] + #order[0x03]
if total < 400 then
	error(string.format("only parsed %d codes from %s, expected ~500 - format changed?",
		total, source_path))
end

local out = assert(io.open(output_path, "w"), "cannot write " .. output_path)

out:write("-- GENERATED FILE - do not edit.\n")
out:write("-- Source: ", source_path, "\n")
out:write("-- Regenerate: luajit tools/gen_nestest_codes.lua\n")
out:write("--\n")
out:write("-- nestest failure codes, keyed by the value left in $02 and $03.\n\n")
out:write("return {\n")

for _, address in ipairs({ 0x02, 0x03 }) do
	out:write(string.format("\t[0x%02X] = {\n", address))
	table.sort(order[address])
	for _, value in ipairs(order[address]) do
		local text = codes[address][value]:gsub("\\", "\\\\"):gsub('"', '\\"')
		out:write(string.format("\t\t[0x%02X] = \"%s\",\n", value, text))
	end
	out:write("\t},\n")
end

out:write("}\n")
out:close()

print(string.format("wrote %s: %d codes for $02, %d codes for $03",
	output_path, #order[0x02], #order[0x03]))
