-- AccuracyCoin's tile font.
--
-- The ROM does not use ASCII: tile 0 is '0', tile $0A is 'A', and $24 is space,
-- the layout Nintendo's own fonts use. Bit 7 marks a highlighted tile (the
-- menu cursor) and does not change the glyph.
--
-- Values below were read back off the ROM's own menus; anything unmapped shows
-- as '?' so an unexpected glyph is visible rather than silently wrong.

local PUNCTUATION = {
	[0x25] = ".",
	[0x28] = ":",
	[0x29] = ",",
	[0x30] = "+",
	[0x31] = "-",
	[0x32] = "*",  -- inferred: the ROM writes "*AX" for the SAX/LAX page
	[0x33] = "/",
	[0x35] = "$",
}

return function(tile)
	local value = tile % 0x80

	if value <= 0x09 then
		return string.char(string.byte("0") + value)
	end
	if value >= 0x0A and value <= 0x23 then
		return string.char(string.byte("A") + value - 0x0A)
	end
	if value == 0x24 then
		return " "
	end

	return PUNCTUATION[value] or "?"
end
