#ifndef CNES_BACKEND_INTERNAL_H
#define CNES_BACKEND_INTERNAL_H

// Shared between backend.c, which owns the emulator instance and the lock that
// guards it, and the other translation units in this backend that need to reach
// it. Not part of the plugin ABI.

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

#endif // CNES_BACKEND_INTERNAL_H
