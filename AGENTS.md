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

## Cvars

**Declare a cvar next to the code that reads it**, with the cvar system holding
the value:

```c
LIBMSE_CVAR_DEFINE_INT(g_show_framecounter, "mse_framecounter", 1,
                       "Show the frame counter (0 = No, 1 = Yes)");
...
if (*g_show_framecounter) { ... }
```

The declaration gives you the pointer to read through. It queues itself from a
static initialiser and `libmse_cvar_flush()` defines it -- once from
`libmse_init`, and again when a backend is loaded, since a backend's
declarations only exist once its library is. Until then the pointer reads the
default, so there is no window where dereferencing it is unsafe.

Vectors are one value, not two or four: `LIBMSE_CVAR_DEFINE_VEC2`,
`_VEC3` and `_VEC4` declare a `float*` onto the components, and the console
takes them either way round -- `set mse_theme_accent 0.35 0.62 1 1` or
`set mse_theme_accent "0.35 0.62 1 1"`. Use one wherever the thing being
configured is a colour or a pair; four separate floats means four sets, four
change callbacks, and three frames drawn with a colour nobody asked for.

**Bind to your own memory only when the value has to live somewhere specific**
-- inside an emulator's settings struct the core reads every frame, say. Use
`LIBMSE_CVAR_BIND_INT` and friends rather than `libmse_cvar_register`: the
`_Generic` in them rejects a pointer of the wrong type, and an int cvar bound
to a `bool` writes four bytes into a one-byte field and corrupts whatever is
declared after it. If the target is an enum, assert its size and cast, rather
than casting quietly.

What you bind to must outlive the cvar. That rules out locals, and it is why
the UI state the frontend binds into is `static`.

## Frontend UI

The frontend has a design system. Use it; do not open-code the look of a
widget.

**The theme is cvars, and a theme is a file.** Every token in
`frontend_theme.h` is a `vec4` cvar (`mse_theme_accent`, `mse_theme_bg_base`,
...) and every ImGui style metric is one too (`mse_style_frame_padding`,
`mse_style_window_rounding`, ...). Metrics are in unscaled pixels; the display
scale is applied where they are read.

A theme is a `themes/*.cfg` of `set` lines and nothing else, so applying one is
`exec themes/ember.cfg`. There is no list of themes in the code:
`mse_frontend_theme_list` returns whatever `.cfg` files are in `themes/` next to
the executable and in `themes/` under the app data directory, and a user's file
of the same name replaces a shipped one. The declared defaults of the colour
cvars are Midnight, so a build with no theme files still looks right.

`mse_frontend_theme_apply` runs once a frame and only reads. Nothing writes the
colour cvars from C, which is why a colour changed from the console stays
changed.

**Colour comes from `frontend_theme.h`.** `mse_frontend_theme()` returns the
tokens for the theme in force — surfaces (`bg_base`, `bg_raised`, `bg_sunken`,
`bg_overlay`, `bg_hover`, `bg_active`), hairlines (`border`, `border_strong`),
type (`text`, `text_muted`, `text_faint`, `text_on_accent`), the accent and
its variants, and the four status colours. A literal `(ImVec4){0.62f, ...}` in
view code is a bug: it will not follow the theme, and it will not match the
widget next to it. Use `mse_frontend_theme_alpha()` or
`mse_frontend_theme_u32()` for one-off opacities.

Adding a theme means filling the token struct in `frontend_theme.c` and adding
it to the enum. Nothing else changes.

**Components come from `frontend_widgets.h`** — cards, page and section
headings, badges, stat tiles, empty states, buttons, toggles, the search field
and the segmented control. Reach for one before drawing rectangles by hand. A
`card_begin` is always matched by a `card_end`, including on the false branch:
ImGui pushed a window either way.

**Sizes go through `mse_frontend_ui_px()`**, which folds in the display scale
and the user's interface-scale setting. A raw pixel constant will be the wrong
size on a HiDPI monitor.

**Only the theme writes `ImGuiStyle`.** Metrics and every `ImGuiCol_` are set
in `mse_frontend_theme_apply`, which runs once per frame before `NewFrame`.
Push a style var or colour locally if a widget needs to differ, and pop it.

**Animate with `mse_frontend_ui_anim()`.** It keys off the ImGui ID stack, so
a hover or selection transition needs no state of its own, and it eases on
wall-clock time rather than per frame.
