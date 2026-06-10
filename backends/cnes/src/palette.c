#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "libmse/libmse_debug.h"

#include "cNES/palette.h"

static void Palette_FillBlack(uint32_t *palette)
{
	if (!palette) {
		return;
	}

	for (int i = 0; i < 64; ++i) {
		palette[i] = 0; // R, G, B, A
	}
}

static bool Palette_ReadRawBinary(FILE *file, uint32_t *out_palette)
{
	uint8_t rgb_buffer[192];
	if (fread(rgb_buffer, 1, sizeof(rgb_buffer), file) != sizeof(rgb_buffer)) {
		return false;
	}

	for (int i = 0; i < 64; ++i) {
		out_palette[i] = (255 << 24) | ((rgb_buffer[i * 3 + 0] & 0xFF) << 16) | ((rgb_buffer[i * 3 + 1] & 0xFF) << 8) |
						 (rgb_buffer[i * 3 + 2] & 0xFF);
	}
	return true;
}

uint32_t *PALETTE_Load(const char *filename)
{
	if (!filename) {
		return NULL;
	}

	FILE *file = fopen(filename, "rb");
	if (!file) {
		DEBUG_ERROR("Failed to open palette file '%s'", filename);
		return NULL;
	}

	uint32_t *out_palette = calloc(64, sizeof(uint32_t));
	if (!out_palette) {
		DEBUG_ERROR("Memory allocation failed for palette.");
		fclose(file);
		return NULL;
	}

	Palette_FillBlack(out_palette);

	uint8_t magic[16]  = {0};
	size_t	peek_bytes = fread(magic, 1, sizeof(magic), file);

	fseek(file, 0, SEEK_SET);

	// JASC-PAL (Text Format)
	if (peek_bytes >= 8 && memcmp(magic, "JASC-PAL", 8) == 0) {
		char line[128];

		if (!fgets(line, sizeof(line), file)) {
			fclose(file);
			free(out_palette);
			return NULL;
		}
		if (!fgets(line, sizeof(line), file)) {
			fclose(file);
			free(out_palette);
			return NULL;
		}
		if (!fgets(line, sizeof(line), file)) {
			fclose(file);
			free(out_palette);
			return NULL;
		}

		int total_colors = atoi(line);
		if (total_colors <= 0) {
			fclose(file);
			free(out_palette);
			return NULL;
		}

		for (int i = 0; i < total_colors; ++i) {
			if (!fgets(line, sizeof(line), file)) {
				break;
			}

			if (i < 64) {
				int r = 0, g = 0, b = 0;
				if (sscanf(line, "%d %d %d", &r, &g, &b) == 3) {
					out_palette[i] = (255 << 24) | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
				}
			}
		}

		fclose(file);
		return out_palette;
	}

	// Microsoft RIFF PAL
	if (peek_bytes >= 12 && memcmp(magic, "RIFF", 4) == 0 && memcmp(&magic[8], "PAL ", 4) == 0) {
		fseek(file, 12, SEEK_SET);

		uint8_t	 chunk_id[4];
		uint32_t chunk_size = 0;

		while (fread(chunk_id, 1, 4, file) == 4 && fread(&chunk_size, 4, 1, file) == 1) {
			if (memcmp(chunk_id, "data", 4) == 0) {
				uint16_t pal_version = 0;
				uint16_t num_entries = 0;

				if (fread(&pal_version, 2, 1, file) != 1 || fread(&num_entries, 2, 1, file) != 1) {
					fclose(file);
					free(out_palette);
					return NULL;
				}

				for (int i = 0; i < num_entries; ++i) {
					uint8_t rgba[4];
					if (fread(rgba, 1, 4, file) != 4) {
						break;
					}

					if (i < 64) {
						out_palette[i] =
							(255 << 24) | ((rgba[0] & 0xFF) << 16) | ((rgba[1] & 0xFF) << 8) | (rgba[2] & 0xFF);
					}
				}

				fclose(file);
				return out_palette;
			}

			uint32_t skip_size = (chunk_size + 1u) & ~1u;
			fseek(file, (long)skip_size, SEEK_CUR);
		}

		fclose(file);
		free(out_palette);
		return NULL;
	}

	// Raw NES Binary Data
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	fseek(file, 0, SEEK_SET);

	if (size == 192 && Palette_ReadRawBinary(file, out_palette)) {
		fclose(file);
		return out_palette;
	}

	fclose(file);
	free(out_palette);
	return NULL;
}

void PALETTE_Destroy(uint32_t *palette)
{
	free(palette);
}
