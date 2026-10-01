# MSE

A multi-system emulator frontend. Backends are plugins loaded at runtime; the
frontend owns the window, the GPU and the UI.

## Code style

**Read [AGENTS.md](AGENTS.md) before writing code.** It covers naming (in
particular: everything in `libmse/` is prefixed `libmse_`, `mse_*` belongs to
the frontend), when to comment, and the shape of a libmse public header.
Formatting is in `.clang-format`.

## Layout

| Path | What it is |
| --- | --- |
| `libmse/` | The library backends and the frontend share. **A git submodule** — changes here need their own commit. |
| `frontend/` | SDL3 + cimgui application. Owns the window, GPU and UI. |
| `backends/cnes/` | NES backend: emulator core plus the plugin glue in `src/backend.c`. |
| `bin/` | Build output and the runtime working directory. |
| `packaging/windows/` | The NSIS installer script. |

## Build and test

```sh
cmake --build build --target mse      # frontend, and everything it depends on
cmake --build build --target libmse
cmake --build build --target cnes_backend

ctest --test-dir build                # all tests
ctest --test-dir build -R mse_lua_panels -V
```

The app runs from `bin/`, which is where it looks for backends and data.

## Things worth knowing

- **Backends run on their own thread behind a lock.** UI code must not walk
  emulator structs; it goes through a plain C debug ABI instead (see
  `backends/cnes/include/cNES/external/cpu_debug.h`). No `lua_State` crosses a DLL
  boundary.
- **Backends can ship their own Lua UI panels.** See `frontend/lua/README.md`.
  A new panel must be added to `frontend/tests/panel_smoke.lua`, which draws
  every panel headlessly against a real ROM and catches bindings that have
  drifted.
- **ImGui text functions take a printf format.** Pass runtime text as an
  argument (`igText("%s", name)`), never as the format itself.
- **Cvars are declared next to what reads them**, with `LIBMSE_CVAR_DEFINE_INT`
  and friends; the cvar system owns the value and you read through the pointer
  the declaration gives you. Bind to your own memory only when the value has to
  live somewhere specific, and then through `LIBMSE_CVAR_BIND_*`, which makes a
  wrong-typed pointer a compile error. See AGENTS.md.
- **The theme and the ImGui style are cvars**, and a theme is a `themes/*.cfg`
  of `set` lines: `exec themes/ember.cfg`. Themes are found next to the
  executable and under the app data directory; there is no list in the code.
- **`exec` looks in the working directory, then the app data directory**, which
  is where `autoexec.cfg` and a user's own `themes/` live.
- **`autoexec.cfg` in the app data directory runs at startup**, after
  `config.cfg` so it has the last word. It is created empty if missing and
  nothing ever writes to it, which is what makes it the place to keep a setting.
- **`mse_font` picks the typeface** and rebuilds the atlas when it changes; a
  file that will not load falls back rather than leaving no glyphs.
- **`imgui.ini` lives in the app data directory**, not the install folder.
- **The window draws its own title bar** (`mse_window_decoration`: 0 system,
  1 custom; Settings > Appearance). The main menu bar doubles as the title
  bar, and an SDL hit-test callback in `frontend_chrome.c` tells the platform
  what drags and what resizes; on Windows the drag area is a real caption, so
  snapping and double-click-to-maximise are the system's own. Under a tiling
  window manager (dwm, i3, sway, Hyprland, komorebi, GlazeWM and so on) custom
  decoration is skipped whatever the setting says. Windows' own `dwm.exe` is
  the compositor, not suckless dwm: keep the tiler lists per platform.
- **So does the game library database.** `mse_library_db` overrides the path for
  a portable install; empty means `<appdata>/mse_library.db`. A database left in
  the working directory by an older build is moved across on first run.
- **`ui_reload` re-runs every Lua panel and overlay script** from disk, keeping
  whatever was open. Edit a panel, type it, see the change.
- **HTTP goes through libcurl**, as `require("cURL")` (Lua-cURLv3) from Lua.
  TLS is the same LibreSSL the rest of the build uses, and certificates verify
  against `data/cert.pem`, which is LibreSSL's own bundle. luasocket and luasec
  are still there for what already uses them. See `libmse/cmake/Curl.cmake`.
- **The logo is generated, not drawn by hand.** `frontend/tools/gen_logo.py`
  writes the SVGs and PNGs in `data/logo/` and `packaging/windows/mse.ico` from
  one set of shapes; the outputs are checked in. `frontend_logo.c` draws the
  same shapes live in the sidebar and has to be kept in step by hand. Below
  48px both switch to a simplified drawing, because the full one's teeth and
  stripes are thinner than a pixel there.
- **Packaging is `cmake --install --component runtime`.** `bin/` is a working
  directory as much as a build output, so the install rules take the runtime out
  of it and leave the movies, databases and configs behind; the `runtime`
  component is what keeps the fetched dependencies' own install rules (headers,
  .pc files, static libs) out of the package. `cmake --build build --target
  installer` then wraps that in an NSIS installer. See `cmake/Install.cmake`.
- **A tag starting with `v` cuts a release.** The CI workflow builds Release,
  packages an installer on Windows and a tarball on Linux, and opens a *draft*
  GitHub release with both attached.
- **Profiling is instrumented and always on**, at roughly 1.3µs per frame.
  Wrap spans with `LIBMSE_PROFILE_START("name")` / `LIBMSE_PROFILE_END()` and
  call `libmse_profiler_frame()` at each thread's frame boundary. F9 opens the
  viewer.
