#ifndef CNES_CHEAT_API_H
#define CNES_CHEAT_API_H

// The cheat list, as the debug UI sees it.
//
// The engine itself is core (cNES/cheats.h) and knows nothing about threads.
// This is the part that does: every call here takes the emulator lock and
// rebuilds the bus lookup, because the emulation thread is reading through it
// while the UI edits the list.
//
// Layout is part of the ABI, like the rest of the external headers. Add fields
// at the end and bump CNES_DEBUG_ABI_VERSION.

#include <stdint.h>
#include <stddef.h>

#include "cNES/external/cpu_debug.h"

#ifdef __cplusplus
extern "C" {
#endif

// Which mechanism a code ends up using. Both are answered on read; this says
// which side of the bus the address lives on, because that is what decides
// whether a code belongs to this ROM or to the console.
typedef enum cnes_cheat_kind_e {
	CNES_CHEAT_CARTRIDGE = 0, // $6000-$FFFF, answered in place of the cartridge
	CNES_CHEAT_SYSTEM         // below $6000, answered in place of work RAM
} cnes_cheat_kind_t;

typedef struct cnes_cheat_s {
	char     code[16];
	char     name[48];
	uint32_t kind;
	uint16_t address;
	uint8_t  value;
	uint8_t  compare;
	uint8_t  has_compare;
	uint8_t  enabled;
	uint8_t  _pad[2];
} cnes_cheat_t;

// Decodes a code without adding it, for validating input as it is typed.
// Returns non-zero if the text is a code in any supported form.
CNES_DEBUG_API int cnes_cheat_parse(const char *code, cnes_cheat_t *out);

// Adds a code. Returns its index, or -1 if it does not parse or the list is
// full. `name` may be NULL.
CNES_DEBUG_API int cnes_cheat_add(const char *code, const char *name);

CNES_DEBUG_API size_t cnes_cheat_count(void);
CNES_DEBUG_API int    cnes_cheat_get(size_t index, cnes_cheat_t *out);
CNES_DEBUG_API int    cnes_cheat_set_enabled(size_t index, int enabled);
CNES_DEBUG_API int    cnes_cheat_remove(size_t index);
CNES_DEBUG_API void   cnes_cheat_clear(void);

// Rebuilds the bus lookup against the console that is loaded now. The backend
// calls this after a ROM load; the lookup lives on the bus, and a load
// replaces it.
CNES_DEBUG_API void cnes_cheat_reapply(void);

#ifdef __cplusplus
}
#endif

#endif // CNES_CHEAT_API_H
