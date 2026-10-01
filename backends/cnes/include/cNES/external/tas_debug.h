#ifndef CNES_TAS_DEBUG_H
#define CNES_TAS_DEBUG_H

#include <stdint.h>
#include <stddef.h>

#include "cNES/external/cpu_debug.h"

#ifdef __cplusplus
extern "C" {
#endif

// Movie playback, as a TAS tool sees it.
//
// The player itself is core (cNES/tas.h). This is the part that crosses the
// thread boundary: every call takes the emulator lock, and seeking is handed to
// the emulation thread rather than done here.

// Button bits, in the order the controller shifts them out. The same byte the
// FM2 input log decodes to, so a movie row and a live pad read alike.
#define CNES_TAS_BTN_A		0x01u
#define CNES_TAS_BTN_B		0x02u
#define CNES_TAS_BTN_SELECT 0x04u
#define CNES_TAS_BTN_START	0x08u
#define CNES_TAS_BTN_UP		0x10u
#define CNES_TAS_BTN_DOWN	0x20u
#define CNES_TAS_BTN_LEFT	0x40u
#define CNES_TAS_BTN_RIGHT	0x80u

#define CNES_TAS_PATH_MAX 260
#define CNES_TAS_TEXT_MAX 128

typedef struct cnes_tas_state_s {
	uint32_t loaded;	 // 0 when no movie is in the player
	uint32_t playing;	 // 1 while frames are still being fed
	uint32_t seeking;	 // 1 while the emulation thread is fast-forwarding
	uint32_t ports;		 // controllers the movie declares, 1 or 2
	uint32_t pal;		 // the movie was recorded on a PAL console
	uint32_t _pad;
	uint64_t frame;		 // playback position, as an FM2 row number
	uint64_t total;		 // rows in the movie
	uint64_t seek_target;
	char	 path[CNES_TAS_PATH_MAX];
	// What the movie says it was recorded against. There is no MD5 here to
	// check it with, and a movie played against the wrong dump of a game
	// desyncs without ever looking broken, so it is shown rather than trusted.
	char rom_filename[CNES_TAS_TEXT_MAX];
	char rom_checksum[CNES_TAS_TEXT_MAX];
} cnes_tas_state_t;

// Fills *out with a consistent snapshot. Returns non-zero on success.
CNES_DEBUG_API int cnes_tas_get_state(cnes_tas_state_t *out);

// Copies `count` rows starting at `first` into `out`, two bytes per row:
// controller 1 then controller 2. Returns the number of rows copied, which is
// short at the end of the movie.
CNES_DEBUG_API size_t cnes_tas_read_frames(uint64_t first, uint8_t *out, size_t count);

// Names the .fm2 files in the working directory. Writes up to `max` paths into
// `out`, each at `stride` bytes, and returns how many were written. Enough to
// populate a picker without a file dialog crossing the ABI.
CNES_DEBUG_API size_t cnes_tas_list_movies(char *out, size_t stride, size_t max);

// Loads a movie and rewinds the console to power-on, which is the state an .fm2
// assumes. Returns non-zero on success.
CNES_DEBUG_API int cnes_tas_load(const char *path);

// Winds playback to the end, releasing the controllers. The console keeps
// running from wherever the movie left it.
CNES_DEBUG_API int cnes_tas_stop(void);

// Resets the console and plays the loaded movie again from its first row.
CNES_DEBUG_API int cnes_tas_restart(void);

// Replays from power-on up to `frame`. The work happens on the emulation
// thread in batches, so the UI keeps drawing and can show it arriving; poll
// cnes_tas_get_state to follow it.
//
// Seeking backwards is the same operation as seeking forwards: a movie is only
// addressable from its start.
CNES_DEBUG_API int cnes_tas_seek(uint64_t frame);

// Abandons a seek in progress, leaving playback wherever it got to.
CNES_DEBUG_API int cnes_tas_cancel_seek(void);

// Runs `frames` frames and then pauses. Pausing and stepping a frame at a time
// is the other half of a TAS tool, and the console's transport (backend_pause,
// backend_resume) has no way to ask for a bounded run.
CNES_DEBUG_API void cnes_tas_frame_advance(uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif // CNES_TAS_DEBUG_H
