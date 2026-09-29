// The locked wrapper around the core movie player. See
// cNES/external/tas_debug.h.
//
// Loading and stopping are cheap enough to do inline under the lock. Seeking is
// not: a movie is only addressable from its start, so landing on frame 12000
// means emulating 12000 frames. That is handed to the emulation thread, which
// runs it a batch at a time so the UI keeps drawing and the seek stays
// cancellable.

#include <stdio.h>
#include <string.h>

#include "cNES/nes.h"
#include "cNES/tas.h"
#include "cNES/external/tas_debug.h"

#include "../backend_internal.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#endif

// Long enough that a seek across a whole movie finishes in a few seconds, short
// enough that the progress readout still moves and a cancel is acted on
// promptly. Measured against the emulation thread, not the UI's frame rate.
#define TAS_SEEK_BATCH 512

static volatile int		 g_tas_seeking	   = 0;
static volatile uint64_t g_tas_seek_target = 0;

int cnes_tas_get_state(cnes_tas_state_t *out)
{
	if (out == NULL) {
		return 0;
	}

	memset(out, 0, sizeof(*out));

	NES *nes = cnes_backend_lock_nes();
	if (nes == NULL || nes->tas == NULL) {
		cnes_backend_unlock_nes();
		return 0;
	}

	const TAS *tas = nes->tas;

	out->loaded		 = tas->loaded ? 1u : 0u;
	out->playing	 = (tas->loaded && tas->playback_frame < tas->frame_count) ? 1u : 0u;
	out->seeking	 = g_tas_seeking ? 1u : 0u;
	out->ports		 = (uint32_t)tas->ports;
	out->pal		 = tas->palFlag ? 1u : 0u;
	out->frame		 = (uint64_t)tas->playback_frame;
	out->total		 = (uint64_t)tas->frame_count;
	out->seek_target = g_tas_seek_target;
	snprintf(out->path, sizeof(out->path), "%s", tas->path);
	snprintf(out->rom_filename, sizeof(out->rom_filename), "%s", tas->rom_filename);
	snprintf(out->rom_checksum, sizeof(out->rom_checksum), "%s", tas->rom_checksum);

	cnes_backend_unlock_nes();
	return 1;
}

size_t cnes_tas_read_frames(uint64_t first, uint8_t *out, size_t count)
{
	if (out == NULL || count == 0) {
		return 0;
	}

	NES *nes = cnes_backend_lock_nes();
	if (nes == NULL || nes->tas == NULL || !nes->tas->loaded) {
		cnes_backend_unlock_nes();
		return 0;
	}

	const TAS *tas = nes->tas;

	size_t copied = 0;
	for (; copied < count; ++copied) {
		const uint64_t row = first + copied;
		if (row >= (uint64_t)tas->frame_count) {
			break;
		}
		out[copied * 2 + 0] = tas->frames[row].controller_1;
		out[copied * 2 + 1] = tas->frames[row].controller_2;
	}

	cnes_backend_unlock_nes();
	return copied;
}

size_t cnes_tas_list_movies(char *out, size_t stride, size_t max)
{
	if (out == NULL || stride == 0 || max == 0) {
		return 0;
	}

	size_t found = 0;

#if defined(_WIN32)
	WIN32_FIND_DATAA entry;
	HANDLE			 search = FindFirstFileA("*.fm2", &entry);
	if (search == INVALID_HANDLE_VALUE) {
		return 0;
	}
	do {
		if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			continue;
		}
		snprintf(out + found * stride, stride, "%s", entry.cFileName);
		found++;
	} while (found < max && FindNextFileA(search, &entry));
	FindClose(search);
#else
	DIR *dir = opendir(".");
	if (dir == NULL) {
		return 0;
	}
	struct dirent *entry;
	while (found < max && (entry = readdir(dir)) != NULL) {
		const size_t length = strlen(entry->d_name);
		if (length < 5 || strcmp(entry->d_name + length - 4, ".fm2") != 0) {
			continue;
		}
		snprintf(out + found * stride, stride, "%s", entry->d_name);
		found++;
	}
	closedir(dir);
#endif

	return found;
}

int cnes_tas_load(const char *path)
{
	if (path == NULL || path[0] == '\0') {
		return 0;
	}

	// Dropped before the load rather than after: a seek left running would
	// otherwise carry its target into the new movie.
	g_tas_seeking = 0;

	NES *nes = cnes_backend_lock_nes();
	if (nes == NULL || nes->tas == NULL) {
		cnes_backend_unlock_nes();
		return 0;
	}

	const int ok = TAS_Load(nes->tas, path) ? 1 : 0;
	if (ok) {
		// An .fm2 is a list of inputs from power-on and nothing else, so it
		// only means anything against a console that has just been reset.
		NES_Reset(nes);
		TAS_Rewind(nes->tas);
	}

	cnes_backend_unlock_nes();
	return ok;
}

int cnes_tas_stop(void)
{
	g_tas_seeking = 0;

	NES *nes = cnes_backend_lock_nes();
	if (nes == NULL || nes->tas == NULL) {
		cnes_backend_unlock_nes();
		return 0;
	}

	// Winding to the end is how the player reports itself finished, and it
	// makes TAS_ApplyFrame release the controllers rather than hold whatever
	// the last row was down forever.
	nes->tas->playback_frame = nes->tas->frame_count;

	cnes_backend_unlock_nes();
	return 1;
}

int cnes_tas_restart(void)
{
	g_tas_seeking = 0;

	NES *nes = cnes_backend_lock_nes();
	if (nes == NULL || nes->tas == NULL || !nes->tas->loaded) {
		cnes_backend_unlock_nes();
		return 0;
	}

	NES_Reset(nes);
	TAS_Rewind(nes->tas);

	cnes_backend_unlock_nes();
	return 1;
}

int cnes_tas_seek(uint64_t frame)
{
	NES *nes = cnes_backend_lock_nes();
	if (nes == NULL || nes->tas == NULL || !nes->tas->loaded) {
		cnes_backend_unlock_nes();
		return 0;
	}

	const uint64_t total = (uint64_t)nes->tas->frame_count;
	if (frame > total) {
		frame = total;
	}
	if (frame < TAS_POWER_ON_FRAMES) {
		frame = TAS_POWER_ON_FRAMES;
	}

	cnes_backend_unlock_nes();

	g_tas_seek_target = frame;
	g_tas_seeking	  = 1;
	return 1;
}

int cnes_tas_cancel_seek(void)
{
	g_tas_seeking = 0;
	return 1;
}

bool cnes_tas_seek_service(NES *nes)
{
	if (!g_tas_seeking) {
		return false;
	}

	if (nes == NULL || nes->tas == NULL || !nes->tas->loaded) {
		g_tas_seeking = 0;
		return false;
	}

	TAS			  *tas	  = nes->tas;
	const uint64_t target = g_tas_seek_target;

	// Backwards is the same operation as forwards, just from further away: the
	// movie is rewound and the console put back to power-on first.
	if ((uint64_t)tas->playback_frame > target) {
		NES_Reset(nes);
		TAS_Rewind(tas);
	}

	for (int i = 0; i < TAS_SEEK_BATCH && (uint64_t)tas->playback_frame < target; ++i) {
		NES_StepFrame(nes);
	}

	if ((uint64_t)tas->playback_frame >= target) {
		g_tas_seeking = 0;
	}
	return true;
}
