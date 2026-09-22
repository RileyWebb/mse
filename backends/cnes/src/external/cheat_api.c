// The locked wrapper around the core cheat engine. See
// cNES/external/cheat_api.h.
//
// Everything here runs on the UI thread while the emulation thread is reading
// through the bus lookup, so every mutation takes the emulator lock and
// rebuilds that lookup before releasing it.

#include <string.h>
#include <stdio.h>

#include "cNES/nes.h"
#include "cNES/cheats.h"
#include "cNES/external/cheat_api.h"

#include "../backend_internal.h"

// The list is owned here rather than by the NES, so codes survive a ROM swap
// and can be entered before a ROM is loaded at all.
static CheatSet g_cheats;

static void cheat_api_export(const Cheat *cheat, cnes_cheat_t *out)
{
	memset(out, 0, sizeof(*out));
	snprintf(out->code, sizeof(out->code), "%s", cheat->code);
	snprintf(out->name, sizeof(out->name), "%s", cheat->name);

	out->kind        = (cheat->address >= 0x6000) ? CNES_CHEAT_CARTRIDGE : CNES_CHEAT_SYSTEM;
	out->address     = cheat->address;
	out->value       = cheat->value;
	out->compare     = cheat->compare;
	out->has_compare = cheat->has_compare ? 1u : 0u;
	out->enabled     = cheat->enabled ? 1u : 0u;
}

int cnes_cheat_parse(const char *code, cnes_cheat_t *out)
{
	Cheat parsed;
	if (out == NULL || !CHEAT_Parse(code, &parsed)) {
		return 0;
	}

	cheat_api_export(&parsed, out);
	return 1;
}

int cnes_cheat_add(const char *code, const char *name)
{
	NES *nes = cnes_backend_lock_nes();

	const int index = CHEAT_Add(&g_cheats, code, name);
	if (index >= 0) {
		CHEAT_Rebuild(nes, &g_cheats);
	}

	cnes_backend_unlock_nes();
	return index;
}

size_t cnes_cheat_count(void)
{
	return g_cheats.count;
}

int cnes_cheat_get(size_t index, cnes_cheat_t *out)
{
	if (out == NULL || index >= g_cheats.count) {
		return 0;
	}

	cheat_api_export(&g_cheats.items[index], out);
	return 1;
}

int cnes_cheat_set_enabled(size_t index, int enabled)
{
	NES *nes = cnes_backend_lock_nes();

	const int ok = CHEAT_SetEnabled(&g_cheats, index, enabled != 0) ? 1 : 0;
	if (ok) {
		CHEAT_Rebuild(nes, &g_cheats);
	}

	cnes_backend_unlock_nes();
	return ok;
}

int cnes_cheat_remove(size_t index)
{
	NES *nes = cnes_backend_lock_nes();

	const int ok = CHEAT_Remove(&g_cheats, index) ? 1 : 0;
	if (ok) {
		CHEAT_Rebuild(nes, &g_cheats);
	}

	cnes_backend_unlock_nes();
	return ok;
}

void cnes_cheat_clear(void)
{
	NES *nes = cnes_backend_lock_nes();

	CHEAT_Clear(&g_cheats);
	CHEAT_Rebuild(nes, &g_cheats);

	cnes_backend_unlock_nes();
}

void cnes_cheat_reapply(void)
{
	NES *nes = cnes_backend_lock_nes();
	CHEAT_Rebuild(nes, &g_cheats);
	cnes_backend_unlock_nes();
}
