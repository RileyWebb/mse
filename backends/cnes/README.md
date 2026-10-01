# cNES

A Nintendo Entertainment System emulator core, written in C11, and the MSE
backend plugin that wraps it. The frontend loads it at runtime like any other
[MSE](../../README.md) backend.

## Status

### Emulation

- [x] 6502 CPU, including the illegal opcodes
- [x] PPU, including sprite 0 hit, sprite overflow and open bus
- [x] APU: both pulse channels, triangle, noise and DMC
- [x] NTSC and PAL region presets
- [x] iNES and NES 2.0 headers
- [x] Battery-backed cartridge RAM (through the console, see [Commands](#console-commands))
- [ ] Automatic battery-save loading and saving
- [ ] Save states
- [ ] Four-screen mirroring
- [ ] Full AccuracyCoin pass. The current record is in [`tests/baseline/`](tests/baseline).

### Mappers

Any mapper not listed as implemented currently runs as NROM, with a warning in
the log.

- [x] 0 NROM
- [x] 1 MMC1
- [x] 2 UxROM
- [x] 3 CNROM
- [x] 4 MMC3
- [x] 7 AxROM
- [ ] 5 MMC5
- [ ] 9 MMC2
- [ ] 10 MMC4
- [ ] 11 Color Dreams
- [ ] 66 GxROM
- [ ] 69 FME-7
- [ ] VRC family (21–26)

### Tools

- [x] CPU, PPU, memory and disassembly panels
- [x] Breakpoints, instruction and frame stepping
- [x] Cheats: Game Genie, Pro Action Replay and raw `AAAA:VV` / `AAAA?CC:VV` codes
- [x] FCEUX `.fm2` movie playback, with seeking
- [x] In-game overlays: input display, plus Super Mario Bros. hitboxes and timer

## Building

cNES builds as part of MSE. From the repository root:

```sh
cmake --build build --target cnes_backend    # the plugin
cmake --build build --target cNES_headless   # the headless test runner
```

The plugin is written to `bin/cnes/lib/<platform>/`, and its Lua data to
`bin/cnes/data/`. The frontend looks for backends there.

## Console commands

Open the MSE console with F10 or `` ` ``.

| Command | What it does |
| --- | --- |
| `cnes_reset` | Resets the console |
| `cnes_pause` / `cnes_resume` / `cnes_stop` | Pauses, resumes or stops emulation (stopping also resets) |
| `cnes_step [count]` | Runs N instructions, then pauses |
| `cnes_step_frame [count]` | Runs N frames, then pauses |
| `cnes_break <hex address>` | Pauses when the PC reaches an address |
| `cnes_break_clear` | Removes every breakpoint |
| `cnes_regs` | Prints the CPU registers, flags and the instruction at the PC |
| `cnes_disasm [address] [count]` | Disassembles from an address (defaults to the PC) |
| `cnes_peek <space> <address> [length]` | Hex-dumps memory |
| `cnes_poke <space> <address> <byte>` | Writes a byte |
| `cnes_sram_save <file>` / `cnes_sram_load <file>` | Saves or loads cartridge RAM |
| `cnes_tas_play <movie.fm2>` | Plays a movie from power-on |
| `cnes_tas_stop` | Stops playback and hands the controllers back |
| `cnes_cheat_add <code> [description]` | Adds a cheat |
| `cnes_cheat_list` | Lists the cheats |
| `cnes_cheat_toggle <index>` / `cnes_cheat_remove <index>` | Enables or disables a cheat, or removes it |
| `cnes_cheat_clear` | Removes every cheat |
| `cnes_load_palette <file.pal>` | Loads a `.pal` palette |

`<space>` is one of `cpu`, `ram`, `ppu`, `oam`, `palette`, `chr` or `prg`.

## Folder structure

```
backends/cnes/
├── include/cNES/
│   ├── external/        the plain C debug ABI (cpu_debug.h, ppu_debug.h,
│   │                    tas_debug.h, cheat_api.h) that Lua panels call
│   ├── loaders/         ROM file loaders
│   └── scripting/       Lua API for the headless runner
├── src/
│   ├── cpu.c            6502 core (opcode tables in cpu_opcodes.inc / cpu_dispatch.inc)
│   ├── ppu.c            picture processing unit
│   ├── apu.c            audio processing unit
│   ├── bus.c            CPU memory map
│   ├── mapper.c         cartridge mappers
│   ├── nes.c            ties the parts together; frame stepping
│   ├── rom.c            ROM handling (iNES / NES 2.0 parsing in loaders/ines.c)
│   ├── cheats.c         cheat code decoding and patching
│   ├── tas.c            .fm2 movie playback
│   ├── palette.c        default palette
│   ├── backend.c        MSE plugin glue: threading, input, console commands
│   └── external/        implementations of the debug ABI
├── data/lua/
│   ├── ui/              ImGui panels (cpu, ppu, memory, disasm, cheats, tas, ...)
│   ├── overlays/        in-game overlays drawn over the picture
│   └── games/           per-game helpers (Super Mario Bros.)
└── tests/               test ROM suites, headless runner and baselines
```

## Architecture notes

- **The core runs on its own thread, behind a lock.** The frontend never reads
  emulator structs directly. Panels and overlays go through the C functions in
  [`include/cNES/external/`](include/cNES/external), which Lua calls through
  the FFI. No `lua_State` crosses the DLL boundary.
- **A new Lua panel** goes in `data/lua/ui/` and must also be added to
  [`frontend/tests/panel_smoke.lua`](../../frontend/tests/panel_smoke.lua).
  That test draws every panel headlessly against a real ROM, so it catches
  bindings that have drifted. See [`frontend/lua/README.md`](../../frontend/lua/README.md)
  for the panel API.

## Tests

```sh
ctest --test-dir build -L cnes              # the whole cNES suite
ctest --test-dir build -L cnes -LE slow     # without the long AccuracyCoin run
```

The suite runs nestest, blargg's CPU tests and AccuracyCoin through the
headless runner. Each test compares its results with a recorded baseline and
fails only on a regression, so known bugs don't hide new ones.
[`tests/README.md`](tests/README.md) covers labels, reports and how to
re-record a baseline.

## Credits

The test ROMs and the libraries used only by cNES are credited in
[CREDITS](CREDITS.md).
