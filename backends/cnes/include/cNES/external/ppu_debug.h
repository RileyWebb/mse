#ifndef CNES_PPU_DEBUG_H
#define CNES_PPU_DEBUG_H

#include <stdint.h>
#include <stddef.h>

#include "cNES/external/cpu_debug.h"

#ifdef __cplusplus
extern "C" {
#endif

// A pattern table is 16x16 tiles of 8x8 pixels.
#define CNES_DEBUG_PATTERN_DIM 128

// Every OAM entry, laid out as 8 columns of 8 rows. Cells are 8x16 so that a
// tall sprite fits; an 8x8 sprite leaves the bottom half transparent.
#define CNES_DEBUG_SPRITE_COLUMNS 8
#define CNES_DEBUG_SPRITE_WIDTH   (CNES_DEBUG_SPRITE_COLUMNS * 8)
#define CNES_DEBUG_SPRITE_HEIGHT  (8 * 16)

// Renders pattern table `table` (0 for $0000, 1 for $1000) through palette
// `palette` (0-3 background, 4-7 sprite) into `out`, which must hold at least
// CNES_DEBUG_PATTERN_DIM * CNES_DEBUG_PATTERN_DIM pixels.
//
// Colour 0 is drawn as the universal backdrop rather than left transparent:
// in a pattern table you are looking for tile shapes, and holes in them are
// harder to read than a flat background.
//
// Returns the number of pixels written, or 0 if there is nothing to draw.
CNES_DEBUG_API size_t cnes_debug_render_pattern_table(uint32_t table, uint32_t palette,
                                                      uint32_t *out, size_t out_pixels);

// A nametable is 32x30 tiles of 8x8 pixels: one screen.
#define CNES_DEBUG_NAMETABLE_WIDTH  256
#define CNES_DEBUG_NAMETABLE_HEIGHT 240

// Renders nametable `index` (0-3, before mirroring is applied) into `out`,
// which must hold at least CNES_DEBUG_NAMETABLE_WIDTH *
// CNES_DEBUG_NAMETABLE_HEIGHT pixels.
//
// Drawn the way the PPU would draw it: the background pattern table PPUCTRL
// currently selects, and the palette each tile's attribute byte picks. Scroll
// is not applied -- this is the map, not the view onto it.
//
// The index is the raw one, so with horizontal or vertical mirroring two of
// the four come back identical. That is worth seeing rather than hiding.
//
// Returns the number of pixels written, or 0 if there is nothing to draw.
CNES_DEBUG_API size_t cnes_debug_render_nametable(uint32_t index, uint32_t *out, size_t out_pixels);

// Renders all 64 OAM entries into `out`, which must hold at least
// CNES_DEBUG_SPRITE_WIDTH * CNES_DEBUG_SPRITE_HEIGHT pixels.
//
// Each sprite is drawn the way the PPU would draw it: its own palette, its own
// flips, and 8x16 pairing when PPUCTRL says so. Colour 0 is left fully
// transparent here, because for a sprite that is what it means.
//
// Returns the number of pixels written, or 0 if there is nothing to draw.
CNES_DEBUG_API size_t cnes_debug_render_sprites(uint32_t *out, size_t out_pixels);

#ifdef __cplusplus
}
#endif

#endif // CNES_PPU_DEBUG_H
