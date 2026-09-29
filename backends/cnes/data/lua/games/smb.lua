-- Shared Super Mario Bros. knowledge for the overlays that need it.
--
-- Addresses are from the community disassembly. Everything here is specific to
-- one game, which is the nature of the thing: an overlay that draws hitboxes
-- has to know where that game keeps them, and nothing on the NES agrees about
-- where anything is.
--
-- Work RAM is pulled once per UI frame and shared, because taking the emulator
-- lock makes the emulation thread stand aside: two overlays reading a byte at a
-- time would do that hundreds of times a frame.

local ig  = require("mse.imgui")
local dbg = require("ui.debug")

local M = {}

M.OPER_MODE     = 0x0770 -- 0 title, 1 game, 2 victory, 3 game over
M.PLAYER_STATE  = 0x001D -- 0 on ground, 1 jumping, 2 falling, 3 climbing
M.ENGINE_SUB    = 0x000E -- GameRoutines index; 8 is normal control, 11 death
M.PLAYER_X      = 0x0086
M.PLAYER_PAGE   = 0x006D
M.PLAYER_Y      = 0x00CE
M.PLAYER_STATUS = 0x0756 -- 0 small, 1 big, 2 fire
M.SCREEN_PAGE   = 0x071A
M.SCREEN_X      = 0x071C
M.LIVES         = 0x075A
M.WORLD         = 0x075F
M.LEVEL         = 0x075C
M.GAME_TIMER    = 0x07F8 -- three digits, hundreds first
M.COINS         = 0x075E
M.INTERVAL_TIMER = 0x077F -- counts 20 down to 0, once per frame

-- The frame rule. SMB's interval timer runs on a 21-frame cycle, and the level
-- loading that hangs off it is what makes a run's time quantise: a level that
-- finishes a frame late costs a whole rule, not a frame. Speedrunners count in
-- these, so the timers here do too.
M.FRAMERULE = 21

M.MODE_TITLE   = 0
M.MODE_GAME    = 1
M.MODE_VICTORY = 2
M.MODE_OVER    = 3

M.ENGINE_DEATH = 0x0B

-- Bounding boxes, four bytes each: upper-left x, upper-left y, lower-right x,
-- lower-right y, all in screen pixels. Nine objects, arrived at by watching
-- which slots ever hold a plausible box across a level: 0 is the player, 1-5
-- are the enemy slots, and 6-8 are power-ups, fireballs and the rest. Slot 9
-- and past it never held one, so nine is where the array ends.
M.BOX_BASE  = 0x04AC
M.BOX_COUNT = 9

M.ENEMY_FLAG = 0x000F -- five slots
M.ENEMY_ID   = 0x0016

-- Enemy ids worth naming. Anything else is shown as its number.
M.ENEMY_NAMES = {
	[0x00] = "Koopa",
	[0x01] = "Koopa",
	[0x02] = "Buzzy Beetle",
	[0x03] = "Koopa",
	[0x04] = "Koopa",
	[0x05] = "Hammer Bro",
	[0x06] = "Goomba",
	[0x07] = "Blooper",
	[0x08] = "Bullet Bill",
	[0x0A] = "Cheep Cheep",
	[0x0B] = "Cheep Cheep",
	[0x0C] = "Podoboo",
	[0x0D] = "Piranha Plant",
	[0x0E] = "Paratroopa",
	[0x0F] = "Paratroopa",
	[0x10] = "Koopa",
	[0x11] = "Lakitu",
	[0x12] = "Spiny",
	[0x14] = "Cheep Cheep",
	[0x15] = "Bowser flame",
	[0x16] = "Fireworks",
	[0x1B] = "Bowser flame",
	[0x2D] = "Bowser",
	[0x2E] = "Power-up",
	[0x30] = "Vine",
	[0x32] = "Flagpole",
	[0x35] = "Star",
}

-- ---------------------------------------------------------------- RAM access

local RAM_SIZE = 0x800

local ram, ram_got, ram_frame = nil, 0, -1

--- Work RAM as it was at the start of this UI frame. Every caller in a frame
--- gets the same buffer, and it is only refilled when the frame changes.
function M.ram()
	local frame = ig.GetFrameCount()
	if frame ~= ram_frame then
		ram_frame = frame
		ram, ram_got = dbg.read(dbg.SPACE.RAM, 0, RAM_SIZE)
	end
	return ram, ram_got
end

--- One byte of work RAM, from this frame's snapshot.
function M.byte(address)
	local data, got = M.ram()
	if got < RAM_SIZE or address < 0 or address >= RAM_SIZE then
		return 0
	end
	return data[address]
end

-- ------------------------------------------------------------ identification

-- Is the running cartridge Super Mario Bros.?
--
-- Checked against the shape of the cartridge and the first bytes of its reset
-- code rather than a filename, because the ROM the user picked could be called
-- anything. It is not a hash, and it is not meant to be: it only has to keep
-- these overlays from drawing confident nonsense over some other NROM game.
local SIGNATURE = { 0x78, 0xD8, 0xA9, 0x10, 0x8D, 0x00, 0x20, 0xA2, 0xFF, 0x9A }

local identified, identified_for = false, nil

function M.detected()
	local state = dbg.state()
	if state == nil then
		identified, identified_for = false, nil
		return false
	end

	-- Re-checked only when the cartridge changes. It means reading PRG across
	-- the lock, and the answer cannot change while a ROM stays loaded.
	local key = string.format("%d/%d/%d",
		tonumber(state.mapper), tonumber(state.prg_size), tonumber(state.chr_size))
	if identified_for == key then
		return identified
	end
	identified_for = key
	identified     = false

	if tonumber(state.mapper) ~= 0 or tonumber(state.prg_size) ~= 32768 then
		return false
	end

	local prg, got = dbg.read(dbg.SPACE.PRG, 0, #SIGNATURE)
	if got < #SIGNATURE then
		return false
	end

	for i = 1, #SIGNATURE do
		if prg[i - 1] ~= SIGNATURE[i] then
			return false
		end
	end

	identified = true
	return true
end

-- ------------------------------------------------------------------ readings

function M.oper_mode()
	return M.byte(M.OPER_MODE)
end

--- True while a level is being played, as opposed to the title screen, the
--- world intro card or the end-of-game sequence.
function M.in_level()
	return M.oper_mode() == M.MODE_GAME
end

--- The game timer as a number. Zero while it is not running.
function M.game_timer()
	return M.byte(M.GAME_TIMER) * 100 + M.byte(M.GAME_TIMER + 1) * 10 + M.byte(M.GAME_TIMER + 2)
end

--- "1-1", from the world and level counters, which are both zero-based.
function M.stage()
	return string.format("%d-%d", M.byte(M.WORLD) + 1, M.byte(M.LEVEL) + 1)
end

--- How far into the current frame rule the game is, 0 to 20. Taken from the
--- game's own interval timer rather than counted here, so it is the phase the
--- game will actually load the next level on.
function M.framerule_phase()
	local remaining = M.byte(M.INTERVAL_TIMER)
	if remaining > M.FRAMERULE - 1 then
		return 0
	end
	return M.FRAMERULE - 1 - remaining
end

--- Bounding box `slot` in screen pixels, or nil when the slot holds nothing
--- drawable. Off-screen objects get their corners clamped, which collapses the
--- box, so a degenerate one means "not here" rather than "zero sized".
function M.box(slot)
	local base = M.BOX_BASE + slot * 4
	local x0, y0 = M.byte(base), M.byte(base + 1)
	local x1, y1 = M.byte(base + 2), M.byte(base + 3)

	if x1 <= x0 or y1 <= y0 then
		return nil
	end
	return x0, y0, x1, y1
end

return M
