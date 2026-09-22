# cNES test suite

Accuracy tests for the cNES backend, run through CTest.

```sh
cmake -S . -B build
cmake --build build --target cNES_headless
cd build && ctest
```

56 tests, about 15 seconds.

## Running a subset

Tests carry labels, so you can run just the area you are working on:

```sh
ctest -L ppu              # everything touching the PPU
ctest -L cpu -LE slow     # CPU tests, skipping the slow aggregates
ctest -L nestest
ctest -R accuracycoin_17  # one AccuracyCoin page by name
ctest --output-on-failure # see the ROM's own report for anything that fails
```

## Seeing every individual result

Each test prints a `[PASS]` / `[FAIL]` line per ROM check, but CTest shows a
test's output only when it fails — and these tests pass by design while
individual checks still fail (see [Baselines](#baselines)). To see the lot:

```sh
cmake --build build --target cnes_accuracycoin_report  # all 20 pages, grouped
cmake --build build --target cnes_test_report          # the whole suite
ctest -V -L accuracycoin                               # same, through ctest
```

Or run a script directly, from this directory:

```sh
../../../bin/cNES_headless AccuracyCoin/AccuracyCoin.nes \
    --script AccuracyCoin/accuracycoin.lua --arg page=13
```

Output is grouped by page, with a tally per page and a summary at the end:

```
page  1 CPU BEHAVIOR                      9 tests,    60 frames
[PASS] page 01/ROM IS NOT WRITABLE
[FAIL] page 01/DUMMY READ CYCLES: code 4 - The STA, X instruction should have a dummy read.
...
       page  1: 7 passed, 2 failed, 0 skipped
```

Labels in use: `cnes`, `nestest`, `accuracycoin`, `blargg`, `cpu`, `ppu`, `apu`,
`timing`, `interrupts`, `sprites`, `dma`, `dummy`, `illegal`, `instr`, `branch`,
`trace`, `slow`.

## What the suite covers

**nestest** — the reference 6502 ROM, from three angles:

| Test | What it does |
| --- | --- |
| `cnes_nestest_result` | Automation mode from `$C000`; decodes the `$02`/`$03` failure codes into the descriptions from `nestest.txt`. |
| `cnes_nestest_menu`, `..._invalid_ops` | Boots the ROM normally and drives its menu, reading a result per test group off the screen. Covers the reset path, PPU and controller, which automation mode skips. |
| `cnes_nestest_trace` | Compares every instruction against `nestest.log`, Nintendulator's golden trace, and reports the exact first divergence. |
| `cnes_nestest_trace_cycles` | The same, also checking the cycle count, so instruction timing is covered. |

**AccuracyCoin** — 138 accuracy tests plus 5 informational readouts, one CTest
per page plus one whole-ROM run. The script reads the ROM's own menu, so a
failure names the test and quotes the meaning of the error code from the
AccuracyCoin README.

**blargg** — the classic ROM set (`instr_test-v5`, `instr_timing`, `instr_misc`,
`cpu_interrupts`, `cpu_dummy_reads`, `cpu_dummy_writes`, `cpu_timing_test6`,
`branch_timing_tests`), all driven by one script.

`cpu_flag_concurrency/test_cpu_flag_concurrency.nes` is deliberately not wired
up: it prints a table and asks the reader to report what they see, so it has no
pass/fail verdict to check.

## Baselines

The emulator does not pass everything yet, so a suite that fails on every known
bug would report nothing useful. Instead each test compares against a recorded
baseline in `baseline/` and fails on a **regression** — something that used to
pass and no longer does. A test that starts passing is reported as a stale
baseline, not a failure.

```sh
# see the absolute state instead of the regression check
cNES_headless AccuracyCoin/AccuracyCoin.nes --script AccuracyCoin/accuracycoin.lua \
    --arg page=13 --arg strict=1

# re-record after fixing something (run from this directory)
cNES_headless AccuracyCoin/AccuracyCoin.nes --script AccuracyCoin/accuracycoin.lua \
    --arg page=13 --arg update_baseline=1
```

`nestest_trace` records how many instructions of the golden log the core
reproduces, and fails if that number drops.

Baselines are text and worth reading in a diff: they are the current accuracy
record of the emulator.

## The runner

`cnes_headless.c` builds the cNES core with Lua scripting enabled and runs a
script against a ROM.

```
cNES_headless [options] <rom>
  --script <path>    Lua test script
  --arg key=value    passed to the script as ARGS.key (repeatable)
  --frames <n>       frame budget for onframe-style scripts (0 disables)
  --interpreter      use the interpreter CPU core instead of the JIT
```

A script provides either `onframe()`, which the runner calls once per frame, or
`onrun()`, which takes over and drives the console itself with `step()` and
`run_frames()`. All the tests here use `onrun()` and bound their own runtime;
the CTest `TIMEOUT` is the backstop.

Verdicts are process exit codes — `0` pass, `1` fail, `2` a fault in the test
itself — so a crash or a timeout can never be mistaken for a pass.

## Layout

```
lib/          harness, report/baseline, screen reader, input sequencer
tools/        generators for the code-description tables
baseline/     recorded results, one file per test
nestest/      nestest ROM, golden log, scripts, generated code table
AccuracyCoin/ ROM, README, scripts, generated code table
blargg/       the shared blargg driver script
<rom dirs>/   the ROMs themselves, with their original readmes
```

## Generated files

`nestest/nestest_codes.lua` and `AccuracyCoin/accuracycoin_codes.lua` are
generated from the documentation that ships with each ROM, so the explanations
stay in step with it. Regenerate from this directory:

```sh
luajit tools/gen_nestest_codes.lua
luajit tools/gen_accuracycoin_tests.lua
```

## Writing a new test

1. Put the ROM in a directory of its own with its readme.
2. Write a script that `require`s `lib.harness` and friends, collects results
   into a `lib.report`, and ends with `results:verdict("baseline/<name>.txt")`.
3. Register it with `cnes_add_test(NAME ... ROM ... SCRIPT ... LABELS ...)`.
4. Record the baseline with `--arg update_baseline=1`.

If the ROM is a blargg one, none of that is needed — add a `cnes_add_blargg`
line in `blargg/CMakeLists.txt`.
