#include "cNES/tas.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libmse/libmse_debug.h"

TAS *TAS_Create(void)
{
	TAS *tas = (TAS *)malloc(sizeof(TAS));
	if (!tas) {
		DEBUG_ERROR("Failed to allocate TAS");
		return NULL;
	}
	memset(tas, 0, sizeof(TAS));
	return tas;
}

void TAS_Destroy(TAS *tas)
{
	if (tas) {
		if (tas->frames) {
			free(tas->frames);
		}
		free(tas);
	}
}

// Parses the standard 8-character FCEUX button string
static uint8_t ParseButtonString(const char *str)
{
	// FM2 input log format is: RLDUTSBA (Right, Left, Down, Up, sTart, Select, B, A)
	// Any character other than '.' or ' ' signifies the button is pressed.
	uint8_t state = 0;

	if (str[0] != '.' && str[0] != ' ') state |= (1 << 7); // Right
	if (str[1] != '.' && str[1] != ' ') state |= (1 << 6); // Left
	if (str[2] != '.' && str[2] != ' ') state |= (1 << 5); // Down
	if (str[3] != '.' && str[3] != ' ') state |= (1 << 4); // Up
	if (str[4] != '.' && str[4] != ' ') state |= (1 << 3); // Start
	if (str[5] != '.' && str[5] != ' ') state |= (1 << 2); // Select
	if (str[6] != '.' && str[6] != ' ') state |= (1 << 1); // B
	if (str[7] != '.' && str[7] != ' ') state |= (1 << 0); // A
	
	return state;
}

bool TAS_Load(TAS *tas, const char *filepath)
{
	if (!tas || !filepath) return false;

	FILE *file = fopen(filepath, "r");
	if (!file) {
		DEBUG_ERROR("Failed to open TAS file: %s", filepath);
		return false;
	}
	
	// Reset existing frames if we are re-loading
	tas->frame_count = 0;
	if (tas->frames == NULL) {
		tas->frame_capacity = 16384; // Allocate ~4.5 minutes worth of frames at 60fps initially
		tas->frames = (TAS_Frame *)malloc(sizeof(TAS_Frame) * tas->frame_capacity);
		if (!tas->frames) {
			fclose(file);
			return false;
		}
	}

	char line[512];
	while (fgets(line, sizeof(line), file)) {
		// Strip trailing newlines and carriage returns
		line[strcspn(line, "\r\n")] = '\0';
		
		if (line[0] == '|') {
			// Parse the input log line (Example: |0|R.......|........||)
			char *p = line + 1; // Skip the initial '|'
			char *pipes[5] = {0};
			
			// Tokenize between the pipes
			for (int i = 0; i < 5; i++) {
				pipes[i] = p;
				p = strchr(p, '|');
				if (!p) break;
				*p = '\0'; // Replace pipe with null terminator
				p++;       // Move pointer to the next section
			}
			
			TAS_Frame frame;
			frame.command	   = pipes[0] ? (uint8_t)atoi(pipes[0]) : 0;
			frame.controller_1 = (pipes[1] && strlen(pipes[1]) >= 8) ? ParseButtonString(pipes[1]) : 0;
			frame.controller_2 = (pipes[2] && strlen(pipes[2]) >= 8) ? ParseButtonString(pipes[2]) : 0;
			
			// Reallocate if we exceed capacity
			if (tas->frame_count >= tas->frame_capacity) {
				tas->frame_capacity *= 2;
				TAS_Frame *new_frames = (TAS_Frame *)realloc(tas->frames, sizeof(TAS_Frame) * tas->frame_capacity);
				if (!new_frames) {
					DEBUG_ERROR("Failed to allocate memory for TAS frames");
					fclose(file);
					return false;
				}
				tas->frames = new_frames;
			}
			
			tas->frames[tas->frame_count++] = frame;
		} else {
			// Basic Key-Value header parsing for standard .fm2 files
			if (strncmp(line, "version", 7) == 0) {
				sscanf(line, "version %d", &tas->version);
			} else if (strncmp(line, "emuVersion", 10) == 0) {
				sscanf(line, "emuVersion %d", &tas->emuVersion);
			} else if (strncmp(line, "palFlag", 7) == 0) {
				int pal = 0;
				sscanf(line, "palFlag %d", &pal);
				tas->palFlag = (pal != 0);
			}
		}
	}
	
	fclose(file);
	DEBUG_INFO("Loaded TAS file '%s' with %zu frames.", filepath, tas->frame_count);
	return true;
}

void TAS_ApplyFrame(TAS *tas, NES *nes)
{
	if (!tas || !nes) return;
	if (tas->playback_frame < tas->frame_count) {
		TAS_Frame *frame = &tas->frames[tas->playback_frame];
		
		// Command byte check: Bit 0 represents Soft Reset, Bit 1 represents Hard Reset
		if ((frame->command & 0x01) || (frame->command & 0x02)) {
			NES_Reset(nes);
		}
		
		// Push input state over to cNES subsystem
		NES_SetController(nes, 0, frame->controller_1);
		NES_SetController(nes, 1, frame->controller_2);

		tas->playback_frame++;
	} else {
		// Stop feeding input once the TAS terminates
		NES_SetController(nes, 0, 0);
		NES_SetController(nes, 1, 0);
	}
}

bool TAS_IsFinished(TAS *tas, NES *nes)
{
	if (!tas) return true;
	return tas->playback_frame >= tas->frame_count;
}

size_t TAS_GetTotalFrames(TAS *tas)
{
	return tas ? tas->frame_count : 0;
}