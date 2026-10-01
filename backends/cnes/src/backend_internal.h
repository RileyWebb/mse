#ifndef CNES_BACKEND_INTERNAL_H
#define CNES_BACKEND_INTERNAL_H

// Shared between backend.c, which owns the emulator instance and the lock that
// guards it, and the other translation units in this backend that need to reach
// it. Not part of the plugin ABI.

#include <stdbool.h>

typedef struct NES NES;

// Takes the emulator lock and returns the instance, or NULL if there is none.
// Always pair with cnes_backend_unlock_nes, including on the NULL return --
// the lock is taken either way.
//
// This uses the same waiter-count protocol as the rest of the backend, so the
// emulation thread stands aside while a caller is waiting. That costs the
// emulator a moment each time, which is why callers should batch their reads.
NES *cnes_backend_lock_nes(void);

void cnes_backend_unlock_nes(void);

// Runs a batch of a movie seek, if one is pending, and returns whether it did.
// Owned by src/external/tas_debug.c but driven from the emulation loop: a seek
// replays the movie from its start, which is far too much work to do under the
// lock on the caller's thread. Call with the lock already held.
bool cnes_tas_seek_service(NES *nes);

#endif // CNES_BACKEND_INTERNAL_H
