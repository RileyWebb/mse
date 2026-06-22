#ifndef TAS_PLAYER_H
#define TAS_PLAYER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "cNES/nes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint8_t controller_1;
	uint8_t controller_2;
	uint8_t command; // bit 0: Soft Reset, bit 1: Hard Reset
} TAS_Frame;

typedef struct TAS {
	TAS_Frame *frames;
	size_t frame_count;
	size_t frame_capacity;

	size_t playback_frame; 
	
	// Basic FM2 Header Metadata
	int version;
	int emuVersion;
	bool palFlag;
} TAS;

// Create a new TAS player instance
TAS *TAS_Create(void);

// Destroy the TAS player instance and free associated memory
void TAS_Destroy(TAS *tas);

// Load an FCEUX .fm2 format TAS file. Returns true on success.
bool TAS_Load(TAS *tas, const char *filepath);

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