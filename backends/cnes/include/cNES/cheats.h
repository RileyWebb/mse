#ifndef CNES_CHEATS_H
#define CNES_CHEATS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "cNES/bus.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NES NES;

#define CNES_CHEAT_MAX      BUS_CHEAT_MAX
#define CNES_CHEAT_CODE_MAX 16
#define CNES_CHEAT_NAME_MAX 48

typedef struct Cheat {
    char     code[CNES_CHEAT_CODE_MAX];
    char     name[CNES_CHEAT_NAME_MAX];
    uint16_t address;
    uint8_t  value;
    uint8_t  compare;
    bool     has_compare;
    bool     enabled;
} Cheat;

typedef struct CheatSet {
    Cheat  items[CNES_CHEAT_MAX];
    size_t count;
} CheatSet;

// Parses a Game Genie code (6 or 8 letters), a Pro Action Replay code (8 hex
// digits, AAAAVVxx) or a raw "AAAA:VV" / "AAAA?CC:VV". Case-insensitive, and
// dashes and spaces are ignored so a code can be pasted as it was printed.
//
// Returns false if the text is not a code in any of those forms.
bool CHEAT_Parse(const char *code, Cheat *out);

// Adds a parsed code. Returns its index, or -1 if it does not parse or the set
// is full. A cheat is enabled when added.
int CHEAT_Add(CheatSet *set, const char *code, const char *name);

bool CHEAT_Remove(CheatSet *set, size_t index);
bool CHEAT_SetEnabled(CheatSet *set, size_t index, bool enabled);
void CHEAT_Clear(CheatSet *set);

// Rebuilds the bus's lookup so reads start returning the cheated bytes.
//
// Call after any change to the set, and after a ROM load. Allocates the index
// on first use and frees it again once the last cheat is gone, so a session
// without cheats never carries the 64KB.
void CHEAT_Rebuild(NES *nes, const CheatSet *set);

// Releases the bus's lookup. Called when the bus is torn down.
void CHEAT_ReleaseBus(BUS *bus);

#ifdef __cplusplus
}
#endif

#endif // CNES_CHEATS_H
