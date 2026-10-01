# cNES Credits

cNES is by Riley Webb and is released under the MIT Licence (see [LICENCE](../../LICENCE)). The libraries MSE itself is built on are credited in the [MSE credits](../../CREDITS.md).

cNES also uses the projects below. Each is used under its own licence.

## Libraries

- **[xml2lua](https://github.com/manoelcampos/xml2lua)**: Manoel Campos da Silva Filho. MIT License. Downloaded by CMake (see [cmake/LuaDeps.cmake](cmake/LuaDeps.cmake)). Used by [data/lua/matcher.lua](data/lua/matcher.lua).

## Test ROMs (included in [tests/](tests))

- **nestest**: Kevin Horton (kevtris).
- **[AccuracyCoin](https://github.com/100thCoin/AccuracyCoin)**: Chris Siebert (100thCoin). MIT License.
- **instr_test-v5, instr_timing, instr_misc, cpu_interrupts, cpu_dummy_reads, cpu_timing_test6, branch_timing_tests**: Shay Green (blargg).
- **cpu_dummy_writes, cpu_flag_concurrency**: Joel Yliluoma (Bisqwit), using blargg's test framework.
