-- Controller input display, in the style of FCEUX's.
--
-- A small pad drawn in the corner at the game's own pixel scale: a dark navy
-- backing, the buttons as flat blocks, lit blue while held. Reads the console's
-- controller latch, so it shows whatever is actually driving the game -- a
-- keyboard, a gamepad, or a movie being played back -- rather than the
-- frontend's idea of what was pressed.

local ig      = require("mse.imgui")
local overlay = require("mse.overlay")
local dbg     = require("ui.debug")

local BTN = {
	A      = 0x01,
	B      = 0x02,
	SELECT = 0x04,
	START  = 0x08,
	UP     = 0x10,
	DOWN   = 0x20,
	LEFT   = 0x40,
	RIGHT  = 0x80,
}

-- The pad, in game pixels, laid out from its own top-left corner. Sized the way
-- FCEUX draws it: small enough to sit in a corner of a 256x240 picture without
-- covering anything, big enough that a single held frame is visible.
local PAD_W, PAD_H = 44, 16

local SHAPES = {
	{ mask = BTN.UP,    x = 5,  y = 1,  w = 4, h = 4 },
	{ mask = BTN.LEFT,  x = 1,  y = 5,  w = 4, h = 4 },
	{ mask = 0,         x = 5,  y = 5,  w = 4, h = 4 }, -- the hub, never lit
	{ mask = BTN.RIGHT, x = 9,  y = 5,  w = 4, h = 4 },
	{ mask = BTN.DOWN,  x = 5,  y = 9,  w = 4, h = 4 },

	{ mask = BTN.SELECT, x = 16, y = 7, w = 5, h = 3 },
	{ mask = BTN.START,  x = 23, y = 7, w = 5, h = 3 },

	{ mask = BTN.B, x = 31, y = 6, w = 5, h = 5 },
	{ mask = BTN.A, x = 38, y = 6, w = 5, h = 5 },
}

local BACKING  = ig.U32(0.04, 0.05, 0.16, 0.85)
local RELEASED = ig.U32(0.11, 0.13, 0.42, 1.00)
local PRESSED  = ig.U32(0.33, 0.60, 1.00, 1.00)

-- Latched rather than recomputed every frame: a second pad that appears and
-- vanishes as buttons are pressed is worse than one that is always there, and
-- an always-drawn empty player 2 is clutter on the games that never read it.
local two_players = false

local function draw_pad(view, ox, oy, pad)
	view:filled(ox - 1, oy - 1, ox + PAD_W + 1, oy + PAD_H + 1, BACKING)

	for _, shape in ipairs(SHAPES) do
		local held = shape.mask ~= 0 and bit.band(pad, shape.mask) ~= 0
		view:filled(ox + shape.x, oy + shape.y,
			ox + shape.x + shape.w, oy + shape.y + shape.h,
			held and PRESSED or RELEASED)
	end
end

overlay.register {
	id    = "cnes.inputs",
	title = "Controller inputs",
	group = "cNES",
	draw  = function(view)
		if not dbg.available then
			return
		end

		local pads = { dbg.controller(0), dbg.controller(1) }
		if pads[2] ~= 0 then
			two_players = true
		end

		-- Bottom-left, a second pad stacked above the first so the one everyone
		-- watches stays in the same place whether or not there is a second.
		local count = two_players and 2 or 1
		for index = 1, count do
			draw_pad(view, 4, view.pixels_y - 4 - index * (PAD_H + 3), pads[index])
		end
	end,
}
