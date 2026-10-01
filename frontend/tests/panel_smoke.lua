-- Smoke test for the Lua debug panels.
--
-- Runs the panels the way the frontend does, but headless: a real ImGui context
-- with no renderer, and a real backend with a real ROM loaded. That covers the
-- parts most likely to break silently -- the generated bindings not matching
-- cimgui, a panel calling a function that does not exist, the debug API's
-- struct layout drifting from the Lua cdef -- none of which show up as a build
-- error.
--
-- Run from the runtime directory (bin), where the modules and the backend live.

package.path = "./data/lua/?.lua;./cnes/data/lua/?.lua;" .. package.path

local ROM = assert(arg[1], "usage: luajit panel_smoke.lua <rom>")

local OVERLAYS = {
	"cnes/data/lua/overlays/inputs.lua",
	"cnes/data/lua/overlays/smb_hitboxes.lua",
	"cnes/data/lua/overlays/smb_timer.lua",
}

local PANELS = {
	"cnes/data/lua/ui/cheats.lua",
	"cnes/data/lua/ui/cpu.lua",
	"cnes/data/lua/ui/disasm.lua",
	"cnes/data/lua/ui/memory.lua",
	"cnes/data/lua/ui/ppu.lua",
	"cnes/data/lua/ui/smb_run.lua",
	"cnes/data/lua/ui/tas.lua",
}

local FRAMES = 5

local ffi     = require("ffi")
local ig      = require("mse.imgui")
local ui      = require("mse.ui")
local overlay = require("mse.overlay")

print("ImGui " .. ffi.string(ig.GetVersion()))

-- Boot the backend so the panels have real state to render rather than falling
-- through their "no ROM" branch, which would test almost nothing.
ffi.cdef [[
bool init(void);
bool load_rom_from_path(const char *path);
void shutdown(void);
]]

local BACKENDS = {
	"cnes/lib/windows/libemulator.dll",
	"cnes/lib/linux/libemulator.so",
}

local backend
for _, candidate in ipairs(BACKENDS) do
	local ok, result = pcall(ffi.load, candidate)
	if ok then
		backend = result
		break
	end
end

assert(backend, "could not load the cNES backend")
assert(backend.init(), "backend init failed")
assert(backend.load_rom_from_path(ROM), "could not load ROM: " .. ROM)

ig.CreateContext(nil)

local gio = ig.GetIO()
gio.DisplaySize = ig.ImVec2(1280, 720)
gio.DeltaTime = 1.0 / 60.0
-- ImGui 1.92 builds and owns its atlas texture when the backend advertises
-- support, which is what lets this run with no renderer attached at all.
gio.BackendFlags = bit.bor(gio.BackendFlags, ig.lib.ImGuiBackendFlags_RendererHasTextures)

-- The frontend puts a backend script's own directory and its parent on
-- package.path so it can require siblings; the overlays reach the debug API and
-- the shared SMB addresses that way, so the same has to be true here.
package.path = "./cnes/data/lua/overlays/?.lua;" .. package.path

for _, script in ipairs(PANELS) do
	local chunk, err = loadfile(script)
	assert(chunk, "could not load " .. script .. ": " .. tostring(err))
	local ok, run_err = pcall(chunk)
	assert(ok, "could not run " .. script .. ": " .. tostring(run_err))
end

for _, script in ipairs(OVERLAYS) do
	local chunk, err = loadfile(script)
	assert(chunk, "could not load " .. script .. ": " .. tostring(err))
	local ok, run_err = pcall(chunk)
	assert(ok, "could not run " .. script .. ": " .. tostring(run_err))
end

local expected = #PANELS
assert(ui.count() == expected,
	string.format("expected %d panels, got %d", expected, ui.count()))
assert(overlay.count() == #OVERLAYS,
	string.format("expected %d overlays, got %d", #OVERLAYS, overlay.count()))

-- Static check: every ig.* and ig.lib.* name the scripts mention must exist.
--
-- Drawing only covers the branches that actually run, and a debug panel is
-- mostly branches. This catches a misspelled or wrongly-shaped binding
-- regardless of whether that line is ever reached.
local function audit(paths)
	local missing = 0

	for _, path in ipairs(paths) do
		local file = assert(io.open(path), "cannot read " .. path)
		local src = file:read("*a")
		file:close()

		-- Comments routinely name the binding they are warning you away from,
		-- so scanning them would report the very thing being documented.
		src = src:gsub("%-%-%[%[.-%]%]", " "):gsub("%-%-[^\n]*", " ")

		local seen = {}

		for name in src:gmatch("ig%.lib%.([%w_]+)") do
			local key = "lib." .. name
			if not seen[key] then
				seen[key] = true
				if ig.lib[name] == nil then
					print(string.format("[FAIL] %s references ig.lib.%s, which does not exist",
						path, name))
					missing = missing + 1
				end
			end
		end

		for name in src:gmatch("ig%.([%w_]+)") do
			-- lib and ffi are namespaces on the module, handled above.
			if name ~= "lib" and name ~= "ffi" and not seen[name] then
				seen[name] = true
				if ig[name] == nil then
					print(string.format("[FAIL] %s references ig.%s, which does not exist",
						path, name))
					missing = missing + 1
				end
			end
		end
	end

	return missing
end

local audit_paths = { "data/lua/mse/ui.lua", "data/lua/mse/overlay.lua" }
for _, script in ipairs(PANELS) do
	audit_paths[#audit_paths + 1] = script
end
for _, script in ipairs(OVERLAYS) do
	audit_paths[#audit_paths + 1] = script
end

local unresolved = audit(audit_paths)
if unresolved > 0 then
	print(string.format("RESULT: FAIL - %d unresolved ImGui reference(s)", unresolved))
	os.exit(1)
end
print(string.format("[PASS] all ImGui references resolve across %d files", #audit_paths))

-- Open everything: a panel that is closed is never drawn, and drawing is the
-- point.
local ids = {}
for _, script in ipairs(PANELS) do
	local id = "cnes." .. script:match("([^/]+)%.lua$")
	local panel = assert(ui.get(id), "panel not registered: " .. id)
	panel.open[0] = true
	ids[#ids + 1] = id
end

-- Give every panel a real window size.
--
-- An auto-fitting window leaves a child of size (0,0) with almost no height, so
-- ImGuiListClipper decides nothing is visible and the row-drawing code -- the
-- part worth testing -- never runs.
local real_Begin = ig.Begin
ig.Begin = function(name, open, flags)
	ig.SetNextWindowSize(ig.ImVec2(1000, 640), 0)
	return real_Begin(name, open, flags)
end

-- Overlays draw into whatever window is open, in the picture's own pixel
-- coordinates. Turned on for the test: an overlay that is off never draws, and
-- drawing is the point.
for _, entry in ipairs(overlay.list()) do
	overlay.show(entry.id, true)
end

local function draw_frames(count)
	for _ = 1, count do
		ig.NewFrame()
		ui.draw()

		-- Stands in for the emulation window the frontend draws overlays
		-- inside. They need a current window for GetWindowDrawList.
		if ig.Begin("###OVERLAY_HOST", nil, 0) then
			overlay.draw(0, 0, 1024, 960, 256, 240)
		end
		ig.End()

		ig.Render()
	end
end

draw_frames(FRAMES)

local dbg = require("ui.debug")

-- The SMB overlays against something that looks like SMB.
--
-- The ROM loaded here is nestest, so their own cartridge check says no and they
-- return before drawing anything -- which would leave the interesting half of
-- each of them untested. Forcing the check and seeding the addresses they read
-- makes the real drawing path run: boxes, labels, the timer's state machine.
local smb = require("games.smb")
smb.detected = function() return true end

local function poke(address, value)
	dbg.write(dbg.SPACE.RAM, address, value)
end

local function seed_level()
	poke(smb.OPER_MODE, 1)
	poke(smb.WORLD, 0)
	poke(smb.LEVEL, 0)
	poke(smb.PLAYER_STATUS, 0)
	poke(smb.PLAYER_STATE, 1)
	poke(smb.ENEMY_ID, 0x06)

	-- A player box and an enemy box, plus a degenerate one: an object that has
	-- scrolled off has its corners clamped together, and drawing that as a
	-- rectangle would put a dot in the corner of every frame.
	local boxes = {
		{ 100, 100, 110, 112 },
		{ 130, 104, 146, 120 },
		{ 255, 255, 255, 255 },
	}
	for slot, box in ipairs(boxes) do
		for i, value in ipairs(box) do
			poke(smb.BOX_BASE + (slot - 1) * 4 + i - 1, value)
		end
	end
end

seed_level()
draw_frames(FRAMES)

-- And the timer through a whole run: title, playing, a stage change, the end.
-- Every branch of its state machine runs, including the split it takes on the
-- way out.
poke(smb.OPER_MODE, smb.MODE_TITLE)
draw_frames(2)
seed_level()
draw_frames(2)
poke(smb.LEVEL, 1)
draw_frames(2)
poke(smb.OPER_MODE, smb.MODE_VICTORY)
draw_frames(2)
poke(smb.OPER_MODE, smb.MODE_TITLE)
draw_frames(2)
-- Overlays rasterise into an image the size of the framebuffer, which is then
-- laid over the picture. Drawing without throwing proves nothing about that
-- image, so check the pixels: a blend that produced zeroes, or a clip that
-- rejected everything, looks exactly like an overlay that is switched off.
seed_level()
draw_frames(1)

local pixels, canvas_w, canvas_h = overlay.pixels()
assert(pixels ~= nil, "overlays drew but no canvas was allocated")
assert(canvas_w == 256 and canvas_h == 240,
	string.format("canvas is %dx%d, expected the framebuffer size", canvas_w, canvas_h))

local painted = 0
for i = 0, canvas_w * canvas_h - 1 do
	if pixels[i] ~= 0 then painted = painted + 1 end
end
assert(painted > 200,
	string.format("only %d canvas pixels were painted; the overlays drew nothing", painted))
print(string.format("[PASS] SMB overlays drew %d pixels into a %dx%d canvas",
	painted, canvas_w, canvas_h))

-- Format-string safety.
--
-- ImGui's text functions take a printf format, and the generated bindings are
-- the raw variadic C functions. A panel rendering emulator data verbatim is
-- then one '%s' away from dereferencing a garbage pointer -- and '%' is an
-- ordinary byte, so scrolling a memory view onto one is routine. mse.imgui
-- wraps those functions to pass text as an argument instead.
--
-- Asserted structurally rather than by trying to provoke a crash: whether a
-- stray '%s' actually faults depends on what happens to be in the vararg
-- registers, so a crash test passes by luck most of the time.
local GUARDED = {
	"Text", "TextDisabled", "TextWrapped", "TextColored",
	"BulletText", "SetTooltip", "LabelText",
}

local unguarded = 0
for _, name in ipairs(GUARDED) do
	local raw = ig.lib["ig" .. name]
	if raw ~= nil and ig[name] == raw then
		print(string.format("[FAIL] ig.%s is the raw variadic C function; "
			.. "text built from emulator data would be treated as a printf format", name))
		unguarded = unguarded + 1
	end
end

if unguarded > 0 then
	print(string.format("RESULT: FAIL - %d unguarded text function(s)", unguarded))
	os.exit(1)
end
print(string.format("[PASS] %d text functions guarded against format-string injection", #GUARDED))

-- Put a movie in the player so the TAS panel draws its input log rather than
-- its empty state. Which ROM is loaded does not matter: an .fm2 is parsed
-- without reference to the cartridge, and the panel only reads rows out of it.
local movies = dbg.tas_list_movies()
if #movies > 0 then
	assert(dbg.tas_load(movies[1]), "could not load movie: " .. movies[1])
	dbg.tas_frame_advance(1)
	print(string.format("[PASS] movie loaded for the TAS panel: %s", movies[1]))
else
	print("[WARN] no .fm2 in the working directory; the TAS panel draws its empty state")
end

draw_frames(FRAMES)

-- And exercise the real path: hostile bytes in work RAM, which is what the CPU
-- view shows at the top, so they are on screen without scrolling.
local payload = "%s%n%s%n%s%n%s%n"
for i = 1, #payload do
	dbg.write(dbg.SPACE.RAM, 0x20 + i - 1, payload:byte(i))
end

draw_frames(FRAMES)
print("[PASS] format specifiers in memory rendered without crashing")

-- Second pass, with every collapsible section forced open.
--
-- A panel's tabs and headers only run their bodies when selected, so the pass
-- above draws whichever tab happens to be first and nothing else. That is how
-- ig.ImDrawList_AddRectFilled -- which does not exist, the binding is a method
-- on the struct -- sat undetected in the PPU palette tab.
--
-- Begin/End are stubbed out in matched pairs rather than forced through ImGui,
-- so every body draws into the window directly and the stacks stay balanced.
local real = {
	BeginTabBar                  = ig.BeginTabBar,
	EndTabBar                    = ig.EndTabBar,
	BeginTabItem                 = ig.BeginTabItem,
	EndTabItem                   = ig.EndTabItem,
	CollapsingHeader_TreeNodeFlags = ig.CollapsingHeader_TreeNodeFlags,
	CollapsingHeader_BoolPtr     = ig.CollapsingHeader_BoolPtr,
}

ig.BeginTabBar  = function() return true end
ig.EndTabBar    = function() end
ig.BeginTabItem = function() return true end
ig.EndTabItem   = function() end
ig.CollapsingHeader_TreeNodeFlags = function() return true end
ig.CollapsingHeader_BoolPtr = function() return true end

draw_frames(FRAMES)

for name, fn in pairs(real) do
	ig[name] = fn
end

ig.Begin = real_Begin

-- The rasterised PPU views.
--
-- Drawing the panel only proves nothing threw; these check that the backend
-- actually produced pixels and that they reach the ImTextureData the renderer
-- uploads from, which is the part with no visible failure mode.
local texture = require("mse.texture")

-- A ROM that has been loaded but never run has palette RAM full of zeroes, and
-- every colour index then resolves to the same entry. Seeding four distinct
-- colours is what makes "did the tile data come through" a question the pixels
-- can answer.
for i, entry in ipairs({ 0x0F, 0x30, 0x16, 0x27 }) do
	dbg.write(dbg.SPACE.PALETTE, i - 1, entry)
end

local pattern = dbg.render_pattern_table(0, 0)
assert(pattern ~= nil, "render_pattern_table returned nothing")

local first, varied = pattern[0], false
for i = 1, dbg.PATTERN_DIM * dbg.PATTERN_DIM - 1 do
	if pattern[i] ~= first then varied = true break end
end
assert(varied, "pattern table rendered as a single flat colour")
print("[PASS] pattern table rasterised")

assert(dbg.render_sprites() ~= nil, "render_sprites returned nothing")
print("[PASS] sprite sheet rasterised")

-- Same reasoning as the palette above: a ROM that has never run has nametable
-- RAM full of zeroes, so every cell is tile $00 and the render comes out
-- uniform whether or not the tile fetch works.
--
-- Which tile indices are blank is a property of the ROM, so rather than
-- guessing one, the first tile with anything in it is taken from the pattern
-- table that was just rendered.
local function first_drawn_tile(pixels)
	for tile = 0, 255 do
		local base = math.floor(tile / 16) * 8 * dbg.PATTERN_DIM + (tile % 16) * 8
		local first = pixels[base]
		for row = 0, 7 do
			for col = 0, 7 do
				if pixels[base + row * dbg.PATTERN_DIM + col] ~= first then
					return tile
				end
			end
		end
	end
	return nil
end

local drawn_tile = first_drawn_tile(pattern)
assert(drawn_tile ~= nil, "no pattern table tile has any pixels in it")

for cell = 0, 63 do
	dbg.write(dbg.SPACE.PPU, 0x2000 + cell, drawn_tile)
end

-- Every nametable, not just the first: under mirroring two of the four are the
-- same memory, and a bad index would still hand back a plausible picture.
for index = 0, 3 do
	local nametable = dbg.render_nametable(index)
	assert(nametable ~= nil,
		string.format("render_nametable(%d) returned nothing", index))
end

local nametable = dbg.render_nametable(0)
local first, varied = nametable[0], false
for i = 1, dbg.NAMETABLE_WIDTH * dbg.NAMETABLE_HEIGHT - 1 do
	if nametable[i] ~= first then varied = true break end
end
assert(varied, "nametable rendered as a single flat colour")

assert(dbg.render_nametable(4) == nil, "render_nametable accepted an out-of-range index")
print("[PASS] nametables rasterised")

local tex = texture.new(dbg.PATTERN_DIM, dbg.PATTERN_DIM)
tex:upload(pattern)

local stored = ffi.cast("uint32_t*", ig.lib.ImTextureData_GetPixels(tex.data))
local mismatched = 0
for i = 0, dbg.PATTERN_DIM * dbg.PATTERN_DIM - 1 do
	if stored[i] ~= pattern[i] then mismatched = mismatched + 1 end
end
assert(mismatched == 0, mismatched .. " pixels did not reach the texture")
print("[PASS] pixels reach the ImGui texture")

-- mse.overlay does the same for overlays: one that throws is disabled and its
-- error kept, rather than taking the frame down.
local failed_overlays = 0
for _, entry in ipairs(overlay.list()) do
	if entry.failed then
		failed_overlays = failed_overlays + 1
		print(string.format("[FAIL] %-24s %s", entry.id, entry.failed))
	else
		print(string.format("[PASS] %-24s drew", entry.id))
	end
end

-- mse.ui catches a panel that throws, disables it and records why.
local failed = failed_overlays
for _, id in ipairs(ids) do
	local panel = ui.get(id)
	if panel.failed then
		failed = failed + 1
		print(string.format("[FAIL] %-12s %s", id, panel.failed))
	else
		print(string.format("[PASS] %-12s drew %d frames", id, FRAMES))
	end
end

backend.shutdown()

if failed > 0 then
	print(string.format("RESULT: FAIL - %d of %d panels errored", failed, expected))
	os.exit(1)
end

print(string.format("RESULT: PASS - %d panels drew cleanly", expected))
