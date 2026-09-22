// Rasterised views of PPU memory. See cNES/external/ppu_debug.h.

#include <string.h>

#include "cNES/nes.h"
#include "cNES/bus.h"
#include "cNES/ppu.h"
#include "cNES/external/ppu_debug.h"

#include "../backend_internal.h"

// The raw master palette, not PPU::active_palette. Emphasis bits and the
// greyscale mask are properties of what is being displayed this frame; a CHR
// viewer that dimmed itself because the game faded the screen would be hiding
// the thing it exists to show.
static uint32_t ppu_debug_colour(const NES *nes, uint8_t palette_entry)
{
	return nes->settings.video.palette[palette_entry & 0x3F];
}

// One row of a tile, as two bit planes eight bytes apart.
//
// BUS_PPU_ReadCHR is the same call the live PPU fetch uses. For the mappers
// this emulator supports that is side-effect free; a latching mapper such as
// MMC2 would need a peek that does not arm the latch.
static void ppu_debug_tile_row(BUS *bus, uint16_t tile_base, int row, uint8_t *lo, uint8_t *hi)
{
	*lo = BUS_PPU_ReadCHR(bus, (uint16_t)(tile_base + row));
	*hi = BUS_PPU_ReadCHR(bus, (uint16_t)(tile_base + row + 8));
}

static uint8_t ppu_debug_pixel(uint8_t lo, uint8_t hi, int column)
{
	const int shift = 7 - column;
	return (uint8_t)(((lo >> shift) & 1u) | (((hi >> shift) & 1u) << 1));
}

size_t cnes_debug_render_pattern_table(uint32_t table, uint32_t palette, uint32_t *out, size_t out_pixels)
{
	const size_t needed = (size_t)CNES_DEBUG_PATTERN_DIM * CNES_DEBUG_PATTERN_DIM;
	if (out == NULL || out_pixels < needed) {
		return 0;
	}

	NES *nes = cnes_backend_lock_nes();
	if (nes == NULL || nes->ppu == NULL || nes->bus == NULL) {
		cnes_backend_unlock_nes();
		return 0;
	}

	const uint16_t table_base   = (table & 1u) ? 0x1000u : 0x0000u;
	const uint8_t  palette_base = (uint8_t)((palette & 7u) * 4u);
	const uint32_t backdrop     = ppu_debug_colour(nes, nes->ppu->palette[0]);

	for (int tile = 0; tile < 256; ++tile) {
		const int      tile_x    = (tile % 16) * 8;
		const int      tile_y    = (tile / 16) * 8;
		const uint16_t tile_base = (uint16_t)(table_base + tile * 16);

		for (int row = 0; row < 8; ++row) {
			uint8_t lo, hi;
			ppu_debug_tile_row(nes->bus, tile_base, row, &lo, &hi);

			uint32_t *scanline = out + (size_t)(tile_y + row) * CNES_DEBUG_PATTERN_DIM + tile_x;

			for (int column = 0; column < 8; ++column) {
				const uint8_t index = ppu_debug_pixel(lo, hi, column);
				scanline[column] = (index == 0)
				                       ? backdrop
				                       : ppu_debug_colour(nes, nes->ppu->palette[palette_base + index]);
			}
		}
	}

	cnes_backend_unlock_nes();
	return needed;
}

size_t cnes_debug_render_sprites(uint32_t *out, size_t out_pixels)
{
	const size_t needed = (size_t)CNES_DEBUG_SPRITE_WIDTH * CNES_DEBUG_SPRITE_HEIGHT;
	if (out == NULL || out_pixels < needed) {
		return 0;
	}

	NES *nes = cnes_backend_lock_nes();
	if (nes == NULL || nes->ppu == NULL || nes->bus == NULL) {
		cnes_backend_unlock_nes();
		return 0;
	}

	PPU *ppu = nes->ppu;

	// Transparent, not black: a sprite sheet drawn on black hides every sprite
	// that is itself mostly black, and the panel puts a checkerboard behind it.
	memset(out, 0, needed * sizeof(uint32_t));

	const bool     tall       = (ppu->ctrl & 0x20u) != 0;
	const uint16_t small_base = (ppu->ctrl & 0x08u) ? 0x1000u : 0x0000u;
	const int      height     = tall ? 16 : 8;

	for (int sprite = 0; sprite < 64; ++sprite) {
		const uint8_t tile      = ppu->oam[sprite * 4 + 1];
		const uint8_t attribute = ppu->oam[sprite * 4 + 2];

		const uint8_t palette_base = (uint8_t)(0x10u + (attribute & 0x03u) * 4u);
		const bool    flip_x       = (attribute & 0x40u) != 0;
		const bool    flip_y       = (attribute & 0x80u) != 0;

		// A tall sprite picks its table from bit 0 of the tile number and uses
		// the pair starting at the even tile below it.
		const uint16_t base  = tall ? ((tile & 1u) ? 0x1000u : 0x0000u) : small_base;
		const uint8_t  first = tall ? (uint8_t)(tile & 0xFEu) : tile;

		const int cell_x = (sprite % CNES_DEBUG_SPRITE_COLUMNS) * 8;
		const int cell_y = (sprite / CNES_DEBUG_SPRITE_COLUMNS) * 16;

		for (int row = 0; row < height; ++row) {
			// Vertical flip mirrors the whole sprite, so for a tall one it also
			// swaps which of the two tiles a row comes from.
			const int source_row = flip_y ? (height - 1 - row) : row;
			const uint16_t tile_base =
				(uint16_t)(base + (first + (source_row >= 8 ? 1 : 0)) * 16);

			uint8_t lo, hi;
			ppu_debug_tile_row(nes->bus, tile_base, source_row & 7, &lo, &hi);

			uint32_t *scanline = out + (size_t)(cell_y + row) * CNES_DEBUG_SPRITE_WIDTH + cell_x;

			for (int column = 0; column < 8; ++column) {
				const uint8_t index = ppu_debug_pixel(lo, hi, flip_x ? (7 - column) : column);
				if (index == 0) {
					continue;
				}
				scanline[column] = ppu_debug_colour(nes, ppu->palette[palette_base + index]);
			}
		}
	}

	cnes_backend_unlock_nes();
	return needed;
}
