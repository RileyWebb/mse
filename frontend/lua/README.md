# Lua-defined UI

Backends can ship their own UI. A backend declares Lua scripts, the frontend
loads them into a UI Lua state, and they register panels that draw with the full
ImGui API. The frontend needs to know nothing about what a backend chooses to
show.

## Adding a panel

```lua
local ig = require("mse.imgui")
local ui = require("mse.ui")

ui.panel {
    id    = "cnes.cpu",       -- unique; identifies the panel in the menu
    title = "cNES CPU",       -- window title
    group = "cNES",           -- submenu it appears under
    draw  = function()
        ig.Text(string.format("PC $%04X", pc))
    end,
}
```

`draw` renders the window's *contents*; the host calls `Begin`/`End` around it,
so a script that throws part way through cannot unbalance the window stack.

Declare the scripts from the backend, relative to the runtime directory:

```c
LIBMSE_API const char *lua_libraries[] = {
    "cnes/data/lua/ui/cpu.lua",
};
LIBMSE_API const size_t lua_library_count = 1;
```

A script's own directory and its parent are put on `package.path` before it
runs, so `cnes/data/lua/ui/cpu.lua` can `require("ui.debug")`.

## Overlays

A panel is a window beside the game. An **overlay** draws on the picture, in the
game's own pixel coordinates:

```lua
local ig      = require("mse.imgui")
local overlay = require("mse.overlay")

overlay.register {
    id    = "cnes.inputs",
    title = "Controller inputs",
    group = "cNES",
    draw  = function(view)
        view:filled(4, view.pixels_y - 12, 12, view.pixels_y - 4, ig.U32(1, 1, 1, 1))
    end,
}
```

Overlays rasterise into an RGBA image **the same size as the framebuffer**,
which is laid over the picture and scaled with the same nearest-neighbour filter
the game gets. A hitbox is therefore made of the same pixels as the sprite it
sits on, at any window size. That is why everything takes integer game pixels,
and why `view:text` uses a 3x5 bitmap font rather than the UI one -- a
proportional anti-aliased glyph rendered at 256 pixels across and then magnified
four times is a grey smear.

`view` carries the picture's screen rect (`x`, `y`, `w`, `h`), its own
resolution (`pixels_x`, `pixels_y`) and the `scale` between them, plus
`plot`, `filled`, `box`, `line`, `text`, `text_right`, `text_width` and
`text_height`. Colours are `ig.U32(r, g, b, a)` and blend properly, so a panel
backing can be semi-transparent. `view.draw_list` and `view:pos` are the escape
hatch for anything that genuinely wants to draw at UI resolution instead.

Nothing is uploaded when every overlay is switched off, so an unused overlay
system costs one boolean per frame.

Scripts load from three places, in this order:

| Where | What it is |
| --- | --- |
| a backend's `lua_libraries` | shipped with the backend, like its panels |
| `<backend>/data/lua/overlays/` | shipped with the backend, drop-in |
| `data/lua/overlays/` | the user's own |

A script's directory and its parent go on `package.path`, so an overlay can
`require("ui.debug")` for the cNES debug API or `require("games.smb")` for the
Super Mario Bros. addresses the bundled ones use.

Turn one on from **View > Overlays**, or from the console: `overlay <id>` and
`overlays` to list them. Panels have the same: `panel <id>`.

## Reaching the emulator

Backends run on their own thread behind a lock, so panels must not walk emulator
structs. Expose a plain C API from the backend and reach it with FFI — see
`backends/cnes/include/cNES/external/cpu_debug.h` and `backends/cnes/data/lua/ui/debug.lua`.
No `lua_State` crosses the DLL boundary.

Taking the emulator's lock makes the emulation thread stand aside briefly, so
pull a whole region once per frame rather than a byte at a time. The memory
panel uses `ImGuiListClipper` to read only the rows actually on screen.

## Reloading

`ui_reload` in the console throws the UI Lua state away and runs every script
again from disk -- the backend's, and every drop-in directory. Panels and
overlays that were open are reopened, so editing a panel is: save, type
`ui_reload`, look.

It is a teardown rather than a re-run over the top, so a panel that was renamed
or is no longer registered goes away instead of lingering as a stale copy.

## When a panel breaks

Each `draw` is called inside `pcall`, between ImGui's `ErrorRecoveryStoreState`
and `ErrorRecoveryTryToRecoverState`. A panel that errors is unwound, reported
once, and disabled — the frame and the other panels carry on. The menu shows it
as `(errored)`.

FFI is not memory-safe: a bad pointer or a cdef that disagrees with the C struct
will take the process down, and no amount of `pcall` will catch that. Keep cdefs
in step with their headers, and version them as `cNES/external/cpu_debug.h` does.

## The bindings

`data/lua/imgui/base.lua` and `cdefs.lua` are **generated at build time** from
this project's own cimgui metadata, by `tools/gen_imgui_bindings.lua` using
LuaJIT-ImGui's `class_gen.lua`. They are not vendored: struct layouts and enum
values have to match the `cimgui.dll` actually loaded, or every field read lands
somewhere else. Rebuild them with:

```sh
cmake --build build --target imgui_lua_bindings
```

`imgui_prelude.lua` is an input to that generator, not a runtime module.

Everything ImGui exports is available (1233 names, including tables, tab bars,
draw lists and clippers) under the names cimgui uses minus the `ig` prefix:
`igBeginTable` is `ig.BeginTable`. Enums live on `ig.lib`, e.g.
`ig.lib.ImGuiTableFlags_Borders`. Out-parameters use `ig.bool()`, `ig.int()`,
`ig.float()` and `ig.buffer()`.

Format with `string.format` and pass the result, rather than using `ig.Text`'s
own varargs. FFI passes a Lua number to a variadic C function as a `double`, so
`ig.Text("$%04X", n)` reads garbage where `ig.Text(string.format("$%04X", n))`
is correct. The same applies to `TextColored`, `SetTooltip` and the rest — and
remember they all treat their string as a format, so text from ROM data should
go through `ig.TextUnformatted`.

## Testing

```sh
ctest -R mse_lua_panels
```

Draws every panel and overlay headlessly against a real ImGui context with a
real ROM loaded. This catches what the compiler cannot: bindings that no longer
match cimgui, a panel calling a function that does not exist, or a cdef that has
drifted from its header.

Overlays get one check the panels cannot have: the test reads the canvas back
and counts the painted pixels. An overlay that runs without throwing but draws
nothing -- a blend that came out zero, a clip that rejected everything -- looks
exactly like one that is switched off, and only the pixels tell them apart.
