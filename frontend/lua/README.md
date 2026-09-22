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

## Reaching the emulator

Backends run on their own thread behind a lock, so panels must not walk emulator
structs. Expose a plain C API from the backend and reach it with FFI — see
`backends/cnes/include/cNES/external/cpu_debug.h` and `backends/cnes/data/lua/ui/debug.lua`.
No `lua_State` crosses the DLL boundary.

Taking the emulator's lock makes the emulation thread stand aside briefly, so
pull a whole region once per frame rather than a byte at a time. The memory
panel uses `ImGuiListClipper` to read only the rows actually on screen.

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

Draws every panel headlessly against a real ImGui context with a real ROM
loaded. This catches what the compiler cannot: bindings that no longer match
cimgui, a panel calling a function that does not exist, or a cdef that has
drifted from its header.
