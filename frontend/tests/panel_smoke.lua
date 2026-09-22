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

local PANELS = {
	"cnes/data/lua/ui/cheats.lua",
	"cnes/data/lua/ui/cpu.lua",
	"cnes/data/lua/ui/disasm.lua",
	"cnes/data/lua/ui/memory.lua",
	"cnes/data/lua/ui/ppu.lua",
}

local FRAMES = 5

local ffi = require("ffi")
local ig  = require("mse.imgui")
local ui  = require("mse.ui")

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

for _, script in ipairs(PANELS) do
	local chunk, err = loadfile(script)
	assert(chunk, "could not load " .. script .. ": " .. tostring(err))
	local ok, run_err = pcall(chunk)
	assert(ok, "could not run " .. script .. ": " .. tostring(run_err))
end

local expected = #PANELS
assert(ui.count() == expected,
	string.format("expected %d panels, got %d", expected, ui.count()))

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

local audit_paths = { "data/lua/mse/ui.lua" }
for _, script in ipairs(PANELS) do
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

for _ = 1, FRAMES do
	ig.NewFrame()
	ui.draw()
	ig.Render()
end

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

-- And exercise the real path: hostile bytes in work RAM, which is what the CPU
-- view shows at the top, so they are on screen without scrolling.
local dbg = require("ui.debug")
local payload = "%s%n%s%n%s%n%s%n"
for i = 1, #payload do
	dbg.write(dbg.SPACE.RAM, 0x20 + i - 1, payload:byte(i))
end

for _ = 1, FRAMES do
	ig.NewFrame()
	ui.draw()
	ig.Render()
end
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

for _ = 1, FRAMES do
	ig.NewFrame()
	ui.draw()
	ig.Render()
end

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

local tex = texture.new(dbg.PATTERN_DIM, dbg.PATTERN_DIM)
tex:upload(pattern)

local stored = ffi.cast("uint32_t*", ig.lib.ImTextureData_GetPixels(tex.data))
local mismatched = 0
for i = 0, dbg.PATTERN_DIM * dbg.PATTERN_DIM - 1 do
	if stored[i] ~= pattern[i] then mismatched = mismatched + 1 end
end
assert(mismatched == 0, mismatched .. " pixels did not reach the texture")
print("[PASS] pixels reach the ImGui texture")

-- mse.ui catches a panel that throws, disables it and records why.
local failed = 0
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
