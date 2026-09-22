// Cheat codes: parsing, and the lookup the bus reads through.
//
// Cheats are applied on the way out of a read rather than by rewriting
// memory, which is what the hardware did -- a Game Genie sat in the cartridge
// port and answered reads. Two things follow from that.
//
// A banked address needs no guesswork. Patching a PRG image means deciding
// which bank a code belongs to before the mapper has decided; intercepting the
// read means the question never comes up, and the eight-letter form's compare
// byte does what it was designed to do.
//
// And RAM cheats stop fighting the game. Poking work RAM once a frame races
// whatever the game writes; answering the read leaves the game's own value
// where it is and changes only what it sees.

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "cNES/nes.h"
#include "cNES/bus.h"
#include "cNES/cheats.h"

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

// The Game Genie alphabet. A letter's position in it is its nibble.
static const char CHEAT_GENIE_ALPHABET[] = "APZLGITYEOXUKSVN";

static int cheat_genie_nibble(char c)
{
    const char upper = (char)toupper((unsigned char)c);
    for (int i = 0; i < 16; ++i) {
        if (CHEAT_GENIE_ALPHABET[i] == upper) {
            return i;
        }
    }
    return -1;
}

static int cheat_hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static size_t cheat_compact(const char *code, char *out, size_t out_size)
{
    size_t length = 0;
    for (const char *p = code; *p != '\0' && length + 1 < out_size; ++p) {
        if (*p == '-' || *p == ' ' || *p == '\t') {
            continue;
        }
        out[length++] = *p;
    }
    out[length] = '\0';
    return length;
}

static bool cheat_parse_genie(const char *text, size_t length, Cheat *out)
{
    if (length != 6 && length != 8) {
        return false;
    }

    int n[8];
    for (size_t i = 0; i < length; ++i) {
        n[i] = cheat_genie_nibble(text[i]);
        if (n[i] < 0) {
            return false;
        }
    }

    // The standard decode. The bit shuffling is the encoding's own; there is
    // no structure in it worth naming.
    out->address = (uint16_t)(0x8000u
        | ((n[3] & 7) << 12)
        | ((n[5] & 7) << 8) | ((n[4] & 8) << 8)
        | ((n[2] & 7) << 4) | ((n[1] & 8) << 4)
        |  (n[4] & 7)       |  (n[3] & 8));

    if (length == 6) {
        out->value = (uint8_t)(((n[1] & 7) << 4) | ((n[0] & 8) << 4)
            | (n[0] & 7) | (n[5] & 8));
        out->compare     = 0;
        out->has_compare = false;
    } else {
        out->value = (uint8_t)(((n[1] & 7) << 4) | ((n[0] & 8) << 4)
            | (n[0] & 7) | (n[7] & 8));
        out->compare = (uint8_t)(((n[7] & 7) << 4) | ((n[6] & 8) << 4)
            | (n[6] & 7) | (n[5] & 8));
        out->has_compare = true;
    }

    return true;
}

// "AAAA:VV" and "AAAA?CC:VV", plus bare eight-digit Pro Action Replay.
static bool cheat_parse_hex(const char *text, size_t length, Cheat *out)
{
    unsigned address = 0, value = 0, compare = 0;
    bool     has_compare = false;

    if (strchr(text, ':') != NULL) {
        if (sscanf(text, "%4x?%2x:%2x", &address, &compare, &value) == 3) {
            has_compare = true;
        } else if (sscanf(text, "%4x:%2x", &address, &value) == 2) {
            has_compare = false;
        } else {
            return false;
        }
    } else if (length == 8) {
        // Pro Action Replay: address, value, then two digits the format keeps
        // for its own bookkeeping that an emulator has no use for.
        for (size_t i = 0; i < length; ++i) {
            if (cheat_hex_nibble(text[i]) < 0) {
                return false;
            }
        }
        if (sscanf(text, "%4x%2x", &address, &value) != 2) {
            return false;
        }
    } else {
        return false;
    }

    if (address > 0xFFFF || value > 0xFF || compare > 0xFF) {
        return false;
    }

    out->address     = (uint16_t)address;
    out->value       = (uint8_t)value;
    out->compare     = (uint8_t)compare;
    out->has_compare = has_compare;
    return true;
}

bool CHEAT_Parse(const char *code, Cheat *out)
{
    if (code == NULL || out == NULL) {
        return false;
    }

    char         compact[CNES_CHEAT_CODE_MAX];
    const size_t length = cheat_compact(code, compact, sizeof(compact));
    if (length == 0) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    snprintf(out->code, sizeof(out->code), "%s", compact);

    // Game Genie first. Its alphabet is APZLGITYEOXUKSVN, of which only A and
    // E are also hex digits, so a real code of either kind fails the other's
    // parse on its first unrecognised character. Eight characters of nothing
    // but A and E would satisfy both; that is read as Game Genie, and no such
    // published code is known.
    if (cheat_parse_genie(compact, length, out)) {
        return true;
    }
    return cheat_parse_hex(compact, length, out);
}

// ---------------------------------------------------------------------------
// The set
// ---------------------------------------------------------------------------

int CHEAT_Add(CheatSet *set, const char *code, const char *name)
{
    if (set == NULL || set->count >= CNES_CHEAT_MAX) {
        return -1;
    }

    Cheat parsed;
    if (!CHEAT_Parse(code, &parsed)) {
        return -1;
    }

    snprintf(parsed.name, sizeof(parsed.name), "%s", (name != NULL) ? name : "");
    parsed.enabled = true;

    const size_t index = set->count++;
    set->items[index] = parsed;
    return (int)index;
}

bool CHEAT_Remove(CheatSet *set, size_t index)
{
    if (set == NULL || index >= set->count) {
        return false;
    }

    for (size_t i = index; i + 1 < set->count; ++i) {
        set->items[i] = set->items[i + 1];
    }
    set->count--;
    return true;
}

bool CHEAT_SetEnabled(CheatSet *set, size_t index, bool enabled)
{
    if (set == NULL || index >= set->count) {
        return false;
    }
    set->items[index].enabled = enabled;
    return true;
}

void CHEAT_Clear(CheatSet *set)
{
    if (set != NULL) {
        set->count = 0;
    }
}

// ---------------------------------------------------------------------------
// The bus lookup
// ---------------------------------------------------------------------------

void CHEAT_ReleaseBus(BUS *bus)
{
    if (bus != NULL) {
        free(bus->cheat_index);
        bus->cheat_index = NULL;
    }
}

// Every CPU address that reaches `address`, so a cheat on $0000 also answers
// reads through the mirrors at $0800, $1000 and $1800.
static void cheat_mark(uint8_t *index, uint16_t address, uint8_t slot)
{
    if (address < 0x2000) {
        const uint16_t base = address & (BUS_RAM_SIZE - 1);
        for (uint32_t mirror = base; mirror < 0x2000; mirror += BUS_RAM_SIZE) {
            index[mirror] = slot;
        }
        return;
    }

    index[address] = slot;
}

void CHEAT_Rebuild(NES *nes, const CheatSet *set)
{
    if (nes == NULL || nes->bus == NULL) {
        return;
    }

    BUS *bus = nes->bus;

    size_t enabled = 0;
    if (set != NULL) {
        for (size_t i = 0; i < set->count; ++i) {
            if (set->items[i].enabled) {
                enabled++;
            }
        }
    }

    // Nothing enabled: give the 64KB back, which also restores BUS_Read to a
    // single predictable branch.
    if (enabled == 0) {
        CHEAT_ReleaseBus(bus);
        return;
    }

    if (bus->cheat_index == NULL) {
        bus->cheat_index = (uint8_t *)calloc(0x10000, sizeof(uint8_t));
        if (bus->cheat_index == NULL) {
            return;
        }
    } else {
        memset(bus->cheat_index, 0, 0x10000);
    }

    // Slots are assigned in list order, so a later cheat on the same address
    // wins -- the same rule the list itself reads by.
    uint8_t slot = 0;
    for (size_t i = 0; i < set->count && slot < BUS_CHEAT_MAX; ++i) {
        const Cheat *cheat = &set->items[i];
        if (!cheat->enabled) {
            continue;
        }

        bus->cheat_patches[slot].value       = cheat->value;
        bus->cheat_patches[slot].compare     = cheat->compare;
        bus->cheat_patches[slot].has_compare = cheat->has_compare ? 1u : 0u;

        slot++;
        cheat_mark(bus->cheat_index, cheat->address, slot);
    }
}
