# Code style

Rules for this repository, for humans and agents alike.

Formatting is already specified in `.clang-format` — tabs, width 4, 120
columns, function braces on their own line, control braces attached. Run it
rather than hand-aligning; nothing about whitespace belongs in this file.

## Naming

| Scope | Prefix | Example |
| --- | --- | --- |
| Everything in `libmse/` | `libmse_` / `LIBMSE_` | `libmse_profiler_begin`, `LIBMSE_PROFILE_START` |
| Frontend | `mse_frontend_`, or `mse_` for user-facing names | `mse_frontend_ui_draw`, the `mse_profiler` cvar |
| Backend plugin entry points | fixed by the plugin ABI | `init`, `start`, `load_rom`, `lua_libraries` |
| cNES backend glue | `cnes_` / `CNES_` | `cnes_debug_read`, `CNES_DEBUG_ABI_VERSION` |
| cNES emulator core | subsystem | `NES_`, `CPU_`, `PPU_`, `APU_`, `BUS_` |

Anything that lives in `libmse/` is prefixed `libmse_`, without exception —
functions, types, macros, enum members and constants alike. `mse_*` is the
frontend's namespace: do not introduce new `mse_*` names inside libmse.

Cvar and command names are user-facing strings, not C identifiers, and take
the prefix of whoever registers them: the frontend registers `mse_profiler`,
cNES registers `cnes_audio_volume`.

File-scope variables are `static` and `g_`-prefixed. Functions not in a header
are `static`.

## Comments

Comment what is hard to understand, not what the code already says. Three
things earn a comment:

- Something a reader would otherwise get wrong — a non-obvious constraint, why
  the obvious approach was rejected, a hardware quirk being reproduced on
  purpose, which thread owns a piece of state.
- Organising a block of declarations.
- `TODO:` for something that needs changing. Say what and why, not just that.

Do not restate the signature, narrate steps the code already spells out, or
leave commented-out code behind — delete it, git has it. If it is a reminder
of work to do, it is a `TODO:` with a sentence, not a commented-out call.

## libmse public headers

Every public header carries an include guard named after the file, includes
`libmse_api.h`, and wraps its declarations for C++:

```c
#ifndef LIBMSE_FOO_H
#define LIBMSE_FOO_H

#include <stdbool.h>
#include <stdint.h>

#include "libmse_api.h"

#ifdef __cplusplus
extern "C" {
#endif

LIBMSE_API void libmse_foo_do_thing(void);

#ifdef __cplusplus
}
#endif

#endif // LIBMSE_FOO_H
```

Everything callable from outside the library is marked `LIBMSE_API`.
