#ifndef TAS_PLAYER_H
#define TAS_PLAYER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "cNES/nes.h"

#ifdef __cplusplus
extern "C" {
#endif

// FCEUX .fm2 movie playback.
//
// An .fm2 is a header of "key value" lines followed by one row per frame:
//
//     |command|RLDUTSBA|RLDUTSBA||
//
// The rows are counted from power-on, so a movie only ever plays from the
// start; there is no way to enter one part way through without replaying it.

#define TAS_PATH_MAX 260
#define TAS_TEXT_MAX 128

// An .fm2 is recorded against FCEUX, which spends two frames at power-on with
// the PPU not yet alive (its `ppudead` counter) before the console runs a real
// frame. The movie's rows advance through those two frames regardless, so its
// row N lands on the console's frame N-2. cNES has no equivalent warm-up, so
// playback starts at row 2 and the two rows before it are never applied.
//
// This is not a guess: with it, Brandon Evans' 20904-frame minimum-press Super
// Mario Bros. movie plays start to finish, 1-1 through 8-4, without a death.
// One row either side of it and Mario walks into the first Goomba.
#define TAS_POWER_ON_FRAMES 2

typedef struct {
	uint8_t controller_1;
	uint8_t controller_2;
	uint8_t command; // bit 0: soft reset, bit 1: hard reset
} TAS_Frame;

typedef struct TAS {
	TAS_Frame *frames;
	size_t	   frame_count;
	size_t	   frame_capacity;

	size_t playback_frame;
	bool   loaded;

	// FM2 header, as declared by whoever recorded it. Kept so the UI can say
	// which ROM the movie expects: cNES has no MD5 to check it against, and a
	// movie recorded on a different dump desyncs without ever looking wrong.
	int	 version;
	int	 emuVersion;
	bool palFlag;
	int	 ports; // controllers the movie declares, 1 or 2
	char path[TAS_PATH_MAX];
	char rom_filename[TAS_TEXT_MAX];
	char rom_checksum[TAS_TEXT_MAX];
} TAS;

// Create a new TAS player instance
TAS *TAS_Create(void);

// Destroy the TAS player instance and free associated memory
void TAS_Destroy(TAS *tas);

// Load an FCEUX .fm2 format TAS file. Returns true on success. The caller is
// responsible for putting the console back to power-on (NES_Reset) -- this
// only reads the file.
bool TAS_Load(TAS *tas, const char *filepath);

// Forget the movie. Playback stops and the controllers are released on the
// next TAS_ApplyFrame.
void TAS_Unload(TAS *tas);

// Move playback back to the movie's first playable row. The console has to be
// reset separately; a movie replayed against a running game is meaningless.
void TAS_Rewind(TAS *tas);

// Inject the current frame's input into the NES instance.
// This should be called exactly once per frame, right BEFORE you call NES_StepFrame(nes).
void TAS_ApplyFrame(TAS *tas, NES *nes);

// Check if the TAS playback has finished
bool TAS_IsFinished(TAS *tas, NES *nes);

// Get the total number of frames in the loaded TAS
size_t TAS_GetTotalFrames(TAS *tas);

#ifdef __cplusplus
}
#endif

#endif // TAS_H
