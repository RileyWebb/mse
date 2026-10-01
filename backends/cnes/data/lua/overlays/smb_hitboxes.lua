-- Super Mario Bros. hitboxes.
--
-- SMB keeps a screen-space bounding box per object in work RAM and does its
-- collision against those, so this draws the game's own boxes rather than a
-- guess at where the sprites are. What you see is what the game tests: a stomp
-- that looks like it connected but did not shows up here as two boxes that
-- never touched.

local ig      = require("mse.imgui")
local overlay = require("mse.overlay")
local dbg     = require("ui.debug")
local smb     = require("games.smb")

local PLAYER  = ig.U32(0.40, 1.00, 0.45, 0.95)
local ENEMY   = ig.U32(1.00, 0.40, 0.38, 0.95)
local OTHER   = ig.U32(1.00, 0.82, 0.35, 0.95)
local LABEL   = ig.U32(1.00, 1.00, 1.00, 0.85)
local WARNING = ig.U32(1.00, 0.55, 0.30, 0.95)

local show_labels = true

local function slot_colour(slot)
	if slot == 0 then
		return PLAYER
	elseif slot <= 5 then
		return ENEMY
	end
	return OTHER
end

local function slot_label(slot)
	if slot == 0 then
		local status = smb.byte(smb.PLAYER_STATUS)
		return (status == 2 and "Fire Mario") or (status == 1 and "Super Mario") or "Mario"
	end

	if slot <= 5 then
		local id = smb.byte(smb.ENEMY_ID + slot - 1)
		return smb.ENEMY_NAMES[id] or string.format("enemy $%02X", id)
	end

	-- 6-8 hold whatever the game last put there: a power-up, a fireball, a
	-- bumped block. There is no id byte shared by all of them, so they are
	-- labelled by slot rather than guessed at.
	return string.format("object %d", slot)
end

overlay.register {
	id    = "cnes.smb.hitboxes",
	title = "SMB hitboxes",
	group = "cNES",
	draw  = function(view)
		if not dbg.available then
			return
		end

		if not smb.detected() then
			view:text(4, 4, WARNING, "not super mario bros")
			return
		end

		if not smb.in_level() then
			return
		end

		for slot = 0, smb.BOX_COUNT - 1 do
			local x0, y0, x1, y1 = smb.box(slot)
			if x0 then
				-- The boxes are inclusive of their lower-right pixel, so the
				-- rectangle runs one past it to enclose the same pixels the
				-- game does.
				local colour = slot_colour(slot)
				view:box(x0, y0, x1 + 1, y1 + 1, colour)

				if show_labels then
					-- Above the box, unless that would run off the top of the
					-- screen, in which case below it.
					local ty = (y0 >= 8) and (y0 - 7) or (y1 + 3)
					view:text(x0, ty, LABEL, slot_label(slot))
				end
			end
		end

		-- The player's own numbers. The box says where the game thinks Mario is;
		-- these say what it thinks he is doing, which is the other half of
		-- reading a missed jump.
		--
		-- Bottom-right: the input overlay owns the bottom-left corner and the
		-- timer the top-right, so the three of them can all be on at once
		-- without stacking up.
		local state  = smb.byte(smb.PLAYER_STATE)
		local states = { [0] = "ground", [1] = "jump", [2] = "fall", [3] = "climb" }
		local text   = string.format("%s  x %d  y %d  %s",
			smb.stage(),
			smb.byte(smb.PLAYER_PAGE) * 256 + smb.byte(smb.PLAYER_X),
			smb.byte(smb.PLAYER_Y),
			states[state] or string.format("state %d", state))

		view:text_right(view.pixels_x - 4, view.pixels_y - 7, PLAYER, text)
	end,
}
