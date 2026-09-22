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
- **A cvar bound with `LIBMSE_CVAR_INT` must point at an `int`.** The cvar
  system writes four bytes through the pointer it is given; binding a `bool`
  corrupts whatever is declared next to it.
- **Profiling is instrumented and always on**, at roughly 1.3µs per frame.
  Wrap spans with `LIBMSE_PROFILE_START("name")` / `LIBMSE_PROFILE_END()` and
  call `libmse_profiler_frame()` at each thread's frame boundary. F9 opens the
  viewer.
