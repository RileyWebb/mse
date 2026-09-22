#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <malloc.h>
#include <time.h>
#include <math.h>

#include "libmse/libmse_debug.h"
#include "cNES/nes.h"
#include "cNES/bus.h"
#include "cNES/util.h"
#include "cNES/cpu.h"

#include <limits.h>

#include "cNES/ppu.h"

#if defined(__SSE2__) || defined(_M_AMD64) || defined(_M_X64) || (_M_IX86_FP == 2)
#define PPU_USE_SIMD_COLOR_EMPHASIS
#endif

// Build with -DPPU_NO_SIMD_COLOR_EMPHASIS to force the scalar path. The two are
// meant to produce identical output; having a supported way to select one is
// what makes that checkable.
#ifdef PPU_NO_SIMD_COLOR_EMPHASIS
#undef PPU_USE_SIMD_COLOR_EMPHASIS
#endif

#ifdef PPU_USE_SIMD_COLOR_EMPHASIS
#include <emmintrin.h> // SSE2 intrinsics
#endif

#define PPU_OPEN_BUS_DECAY_MS 750ULL

// Palette entries are 0xAABBGGRR: alpha in bits 24-31, then blue, green, red.
// Red is the low byte, so in memory on a little-endian host the bytes run
// R,G,B,A -- plain RGBA8. That is the layout PALETTE_default holds; checking a
// few entries against the canonical NES colours is what pins it down (index 1
// is 0x0000FC blue, stored as 0xFFFC0000).
//
// Both paths below used to disagree about this, so an SSE2 build and a scalar
// build tinted emphasised scenes differently. They now agree, and share the
// same 8.8 fixed-point attenuation so their output is bit-identical.
#define PPU_EMPHASIS_UNITY 256u
#define PPU_EMPHASIS_ATTEN 192u // 0.75 in 8.8 fixed point

#ifndef PPU_USE_SIMD_COLOR_EMPHASIS
static inline uint32_t apply_color_emphasis(const uint32_t* base_color, uint8_t ppu_mask)
{
    // Check bits 5, 6, and 7
    if (!(ppu_mask & 0xE0)) return *base_color;

    uint32_t c = *base_color;

    uint32_t a = (c >> 24) & 0xFF;
    uint32_t b = (c >> 16) & 0xFF;
    uint32_t g = (c >> 8) & 0xFF;
    uint32_t r = c & 0xFF;

    uint32_t r_mult = PPU_EMPHASIS_UNITY, g_mult = PPU_EMPHASIS_UNITY, b_mult = PPU_EMPHASIS_UNITY;

    // Emphasising a channel attenuates the other two
    if (ppu_mask & PPUMASK_EMPHASIZE_RED)   { g_mult = PPU_EMPHASIS_ATTEN; b_mult = PPU_EMPHASIS_ATTEN; }
    if (ppu_mask & PPUMASK_EMPHASIZE_GREEN) { r_mult = PPU_EMPHASIS_ATTEN; b_mult = PPU_EMPHASIS_ATTEN; }
    if (ppu_mask & PPUMASK_EMPHASIZE_BLUE)  { r_mult = PPU_EMPHASIS_ATTEN; g_mult = PPU_EMPHASIS_ATTEN; }

    r = (r * r_mult) >> 8;
    g = (g * g_mult) >> 8;
    b = (b * b_mult) >> 8;

    return (a << 24) | (b << 16) | (g << 8) | r;
}
#else
static inline uint32_t apply_color_emphasis(const uint32_t* base_color, uint8_t ppu_mask)
{
    if (!(ppu_mask & 0xE0)) return *base_color;

    __m128i color_vec = _mm_cvtsi32_si128(*base_color);
    // Unpack bytes to 16-bit words, low byte first. For 0xAABBGGRR on a
    // little-endian host that gives w0 = Red, w1 = Green, w2 = Blue, w3 = Alpha.
    color_vec = _mm_unpacklo_epi8(color_vec, _mm_setzero_si128());

    uint16_t r_f = PPU_EMPHASIS_UNITY, g_f = PPU_EMPHASIS_UNITY, b_f = PPU_EMPHASIS_UNITY;

    if (ppu_mask & PPUMASK_EMPHASIZE_RED)   { g_f = PPU_EMPHASIS_ATTEN; b_f = PPU_EMPHASIS_ATTEN; }
    if (ppu_mask & PPUMASK_EMPHASIZE_GREEN) { r_f = PPU_EMPHASIS_ATTEN; b_f = PPU_EMPHASIS_ATTEN; }
    if (ppu_mask & PPUMASK_EMPHASIZE_BLUE)  { r_f = PPU_EMPHASIS_ATTEN; g_f = PPU_EMPHASIS_ATTEN; }

    // _mm_set_epi16 takes (w7 ... w0), so the factors run Alpha, Blue, Green, Red
    // to line up with the unpacked order above.
    __m128i factors = _mm_set_epi16(0, 0, 0, 0, (short)PPU_EMPHASIS_UNITY, (short)b_f, (short)g_f, (short)r_f);

    color_vec = _mm_mullo_epi16(color_vec, factors);
    color_vec = _mm_srli_epi16(color_vec, 8); // Shift back to 8-bit range

    color_vec = _mm_packus_epi16(color_vec, _mm_setzero_si128());

    return (uint32_t)_mm_cvtsi128_si32(color_vec);
}
#endif // PPU_USE_SIMD_COLOR_EMPHASIS

// Palette RAM mirroring: $3F10/$14/$18/$1C alias $3F00/$04/$08/$0C.
static const uint8_t pal_indices[32] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    0, 17, 18, 19, 4, 21, 22, 23, 8, 25, 26, 27, 12, 29, 30, 31
};

// Cheap: 32 entries, no emphasis maths. Everything the pixel loop needs about
// palette RAM, resolved once here instead of per pixel.
static void ppu_rebuild_render_palette(PPU *ppu)
{
    // Grayscale forces the low four bits of the colour index to zero, selecting
    // the grey column of the NES palette. It belongs here rather than in the
    // pixel loop, where it was simply missing.
    const uint8_t mask = (ppu->mask & PPUMASK_GRAYSCALE) ? 0x30u : 0x3Fu;

    for (int i = 0; i < 32; ++i) {
        uint8_t index = (uint8_t)(ppu->palette[pal_indices[i]] & mask);
        ppu->final_index[i] = index;
        ppu->final_color[i] = ppu->active_palette[index];
    }
}

// Expensive: 64 emphasis computations. Only the emphasis bits or the base
// palette can invalidate this, so palette RAM writes no longer trigger it --
// they used to, redoing all 64 for a change that cannot affect them.
static void ppu_update_active_palette(PPU *ppu)
{
    for (int i = 0; i < 64; ++i) {
        const uint32_t *base_color = &ppu->nes->settings.video.palette[i];
        ppu->active_palette[i] = apply_color_emphasis(base_color, ppu->mask);
    }
    ppu_rebuild_render_palette(ppu);
}

uint64_t PPU_GetTotalCycles(PPU *ppu) {
    if (!ppu) return 0;

    return ppu->total_cycles;
}

// Runs at least one PPU tick and at most `budget`, returning how many it ran.
// Defined below PPU_Step, which it falls back to.
static uint32_t ppu_run_span(PPU *ppu, uint64_t budget);

void PPU_CatchUp(PPU *ppu) {
    if (!ppu || !ppu->nes || !ppu->nes->cpu) return;

    // The PPU is only ever observed from an instruction boundary or from a
    // register access, and both call this first. That is what makes it safe to
    // run a whole span of ticks with the state that cannot change during it --
    // the mask, the scroll, the palette -- hoisted out of the loop, and to skip
    // outright over the stretches where a tick provably does nothing at all.
    uint64_t target_ppu_cycles = ppu->nes->cpu->total_cycles * 3;
    while (ppu->total_cycles < target_ppu_cycles) {
        ppu_run_span(ppu, target_ppu_cycles - ppu->total_cycles);
    }
}

static inline uint64_t ppu_now_ms(PPU *ppu)
{
    // NTSC PPU clock is ~5.369 MHz, so ~5369 cycles per millisecond
    return ppu->total_cycles / 5369ULL;
}

static inline void ppu_decay_open_bus(PPU *ppu)
{
    if (!ppu) return;

    if (ppu->open_bus != 0 && ppu->open_bus_last_update_ms != 0) {
        uint64_t now = ppu_now_ms(ppu);
        if ((now - ppu->open_bus_last_update_ms) >= PPU_OPEN_BUS_DECAY_MS) {
            ppu->open_bus = 0;
        }
    }
}

static inline void ppu_drive_open_bus(PPU *ppu, uint8_t value)
{
    ppu->open_bus                = value;
    ppu->open_bus_last_update_ms = ppu_now_ms(ppu);
}

static inline uint8_t ppu_palette_read(PPU *ppu, uint16_t addr)
{
    return ppu->palette[pal_indices[addr & 0x1F]];
}

static inline void ppu_palette_write(PPU *ppu, uint16_t addr, uint8_t value)
{
    ppu->palette[pal_indices[addr & 0x1F]] = value;
    ppu_rebuild_render_palette(ppu);
}

static inline void ppu_update_nmi_line(PPU *ppu)
{
    bool nmi_condition = ppu->nmi_output && (ppu->status & PPUSTATUS_VBLANK);

    if (!nmi_condition) {
        ppu->previous_nmi_output = false;
        ppu->nmi_interrupt_line  = false;
        return;
    }

    if (!ppu->previous_nmi_output) {
        ppu->nmi_interrupt_line = true;
    }

    ppu->previous_nmi_output = true;
}

// Mappers that time an IRQ off the PPU's A12 line need to see every address the
// PPU drives, not just the pattern-table ones -- the nametable fetches are what
// hold A12 low between rises.
static inline void ppu_notify_mapper_addr(PPU *ppu, uint16_t addr)
{
    BUS *bus = ppu->nes->bus;
    if (bus->mapper_info->ppu_addr) {
        bus->mapper_info->ppu_addr(bus, addr);
    }
}

static inline uint8_t ppu_read_vram(PPU *ppu, uint16_t addr)
{
    addr &= 0x3FFF;
    ppu_notify_mapper_addr(ppu, addr);

    if (addr < 0x2000) { // CHR ROM/RAM ($0000 - $1FFF)
        return BUS_PPU_ReadCHR(ppu->nes->bus, addr);
    } else if (addr < 0x3F00) { // Nametable RAM ($2000 - $3EFF)
        uint16_t nt_addr = addr & 0x0FFF;
        return ppu->nametable_ptrs[nt_addr >> 10][nt_addr & 0x03FF];
    } else { // Palette RAM ($3F00 - $3FFF)
        return ppu_palette_read(ppu, addr);
    }

    return 0;
}

static inline void ppu_write_vram(PPU *ppu, uint16_t addr, uint8_t value)
{
    addr &= 0x3FFF;
    ppu_notify_mapper_addr(ppu, addr);

    if (addr < 0x2000) { // CHR RAM ($0000 - $1FFF)
        BUS_PPU_WriteCHR(ppu->nes->bus, addr, value);
    } else if (addr < 0x3F00) { // Nametable RAM ($2000 - $3EFF)
        uint16_t nt_addr = addr & 0x0FFF;
        ppu->nametable_ptrs[nt_addr >> 10][nt_addr & 0x03FF] = value;
    } else if (addr < 0x4000) { // Palette RAM ($3F00 - $3FFF)
        ppu_palette_write(ppu, addr, value);
    }
}

// --- VRAM Address Update Helpers (Scrolling) ---
static inline void increment_coarse_x(PPU *ppu)
{
    if ((ppu->vram_addr & 0x001F) == 31) {
        ppu->vram_addr &= ~0x001Fu;
        ppu->vram_addr ^= 0x0400;
    } else {
        ppu->vram_addr += 1;
    }
}

static inline void increment_fine_y(PPU *ppu)
{
    if ((ppu->vram_addr & 0x7000) != 0x7000) {
        ppu->vram_addr += 0x1000;
    } else {
        ppu->vram_addr &= ~0x7000u;
        uint16_t y = (ppu->vram_addr & 0x03E0) >> 5;
        if (y == 29) {
            y = 0;
            ppu->vram_addr ^= 0x0800;
        } else if (y == 31) {
            y = 0;
        } else {
            y += 1;
        }
        ppu->vram_addr = (ppu->vram_addr & ~0x03E0u) | (y << 5);
    }
}

static inline void copy_horizontal_bits(PPU *ppu)
{
    ppu->vram_addr = (ppu->vram_addr & ~0x041Fu) | (ppu->temp_addr & 0x041F);
}

static inline void copy_vertical_bits(PPU *ppu)
{
    ppu->vram_addr = (ppu->vram_addr & ~0x7BE0u) | (ppu->temp_addr & 0x7BE0);
}

// --- Background Rendering Pipeline Helpers ---
static void load_background_tile_data(PPU *ppu)
{
    uint16_t nt_addr = 0x2000 | (ppu->vram_addr & 0x0FFF);
    ppu->bg_nt_latch = ppu_read_vram(ppu, nt_addr);

    uint16_t at_addr =
        0x23C0 | (ppu->vram_addr & 0x0C00) | ((ppu->vram_addr >> 4) & 0x38) | ((ppu->vram_addr >> 2) & 0x07);
    uint8_t at_byte = ppu_read_vram(ppu, at_addr);

    uint8_t shift = (uint8_t)(((ppu->vram_addr >> 4) & 0x04) | (ppu->vram_addr & 0x02));
    uint8_t palette_bits = (at_byte >> shift) & 0x03;

    ppu->bg_at_latch_low  = -((palette_bits & 0x01) != 0);
    ppu->bg_at_latch_high = -((palette_bits & 0x02) != 0);

    uint16_t fine_y      = (ppu->vram_addr >> 12) & 7;
    uint16_t pt_base     = (ppu->ctrl & PPUCTRL_BG_TABLE_ADDR) ? 0x1000 : 0;
    uint16_t tile_offset = ppu->bg_nt_latch * 16;

    uint16_t pt_addr_low  = pt_base + tile_offset + fine_y;
    ppu->bg_pt_low_latch  = ppu_read_vram(ppu, pt_addr_low);
    ppu->bg_pt_high_latch = ppu_read_vram(ppu, pt_addr_low + 8);
}

static void feed_background_shifters(PPU *ppu)
{
    ppu->bg_pattern_shift_low  = (ppu->bg_pattern_shift_low & 0xFF00) | ppu->bg_pt_low_latch;
    ppu->bg_pattern_shift_high = (ppu->bg_pattern_shift_high & 0xFF00) | ppu->bg_pt_high_latch;
    ppu->bg_attrib_shift_low   = (ppu->bg_attrib_shift_low & 0xFF00) | ppu->bg_at_latch_low;
    ppu->bg_attrib_shift_high  = (ppu->bg_attrib_shift_high & 0xFF00) | ppu->bg_at_latch_high;
}

// --- Sprite Evaluation and Rendering Helpers ---
static void evaluate_sprites(PPU *ppu)
{
    ppu->sprite_count_current_scanline = 0;

    memset(ppu->secondary_oam, 0xFF, PPU_SECONDARY_OAM_SIZE); 

    uint8_t secondary_oam_idx = 0;
    uint8_t sprite_height     = (ppu->ctrl & PPUCTRL_SPRITE_SIZE) ? 16 : 8;

    int oam_start = ppu->oam_addr;
    int n = oam_start / 4;
    int m = oam_start & 3;
    int sprites_checked = 0;

    while (sprites_checked < 64) {
        uint8_t sprite_y  = ppu->oam[(n * 4 + m) % 256];
        int next_scanline = (ppu->scanline == ppu->scanline_prerender) ? 0 : (ppu->scanline + 1);
        int row_on_scanline = next_scanline - sprite_y - 1;

        if (row_on_scanline >= 0 && row_on_scanline < sprite_height) {
            for (int b = 0; b < 4; ++b) {
                ppu->secondary_oam[secondary_oam_idx * 4 + b] = ppu->oam[(n * 4 + m + b) % 256];
            }
            ppu->secondary_oam_original_indices[secondary_oam_idx] = n; 
            secondary_oam_idx++;
            n = (n + 1) & 63;
            sprites_checked++;
            if (secondary_oam_idx == 8) {
                break;
            }
        } else {
            n = (n + 1) & 63;
            sprites_checked++;
        }
        m = 0;
    }

    if (secondary_oam_idx == 8) {
        m = 0;
        while (sprites_checked < 64) {
            uint8_t sprite_y = ppu->oam[(n * 4 + m) % 256];
            int next_scanline = (ppu->scanline == ppu->scanline_prerender) ? 0 : (ppu->scanline + 1);
            int row_on_scanline = next_scanline - sprite_y - 1;
            
            if (row_on_scanline >= 0 && row_on_scanline < sprite_height) {
                ppu->status |= PPUSTATUS_SPRITE_OVERFLOW;
                break;
            } else {
                n = (n + 1) & 63;
                m = (m + 1) & 3;
                sprites_checked++;
            }
        }
    }

    ppu->sprite_count_current_scanline = secondary_oam_idx;
    ppu->oam_addr = 0; // Reset OAMADDR at cycle 257
}

static const uint8_t bit_reverse_table[256] = {
    0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0, 0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0,
    0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8, 0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8,
    0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4, 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4,
    0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC, 0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC,
    0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2, 0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2,
    0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA, 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
    0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6, 0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6,
    0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE, 0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE,
    0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1, 0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
    0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9, 0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9,
    0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5, 0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5,
    0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED, 0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
    0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3, 0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3,
    0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB, 0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,
    0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7, 0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7,
    0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF, 0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF
};

static void fetch_sprite_patterns(PPU *ppu)
{
    uint8_t sprite_height = (ppu->ctrl & PPUCTRL_SPRITE_SIZE) ? 16 : 8;

    // Clear only what the previous fetch actually wrote. A full wipe zeroed 16
    // cache lines a scanline so the pixel loop could read them once; at most 8
    // sprites x 8 pixels of that was ever real data. The length is a constant 8
    // entries so this stays inline stores rather than a memset call.
    for (uint8_t i = 0; i < ppu->sprite_span_count; ++i) {
        memset(&ppu->scanline_sprite_buffer[ppu->sprite_span_x[i]], 0,
               8 * sizeof(ppu->scanline_sprite_buffer[0]));
    }
    ppu->sprite_span_count = 0;

    // Evaluate sprites from back to front (7 down to 0) so that lower-index (higher priority) sprites overwrite higher-index ones.
    for (int i = ppu->sprite_count_current_scanline - 1; i >= 0; --i) {
        uint8_t sprite_y_oam = ppu->secondary_oam[i * 4 + 0];
        uint8_t tile_id      = ppu->secondary_oam[i * 4 + 1];
        uint8_t attributes   = ppu->secondary_oam[i * 4 + 2];
        uint8_t sprite_x     = ppu->secondary_oam[i * 4 + 3];
        uint8_t original_oam_index = ppu->secondary_oam_original_indices[i];

        int next_scanline = (ppu->scanline == ppu->scanline_prerender) ? 0 : (ppu->scanline + 1);
        int row_in_sprite = next_scanline - sprite_y_oam - 1;

        if (attributes & 0x80) { 
            row_in_sprite = (sprite_height - 1) - row_in_sprite;
        }

        uint16_t pattern_addr_base;
        if (sprite_height == 16) {
            pattern_addr_base = ((tile_id & 0x01) ? 0x1000 : 0x0000) + ((tile_id & 0xFE) * 16);
            if (row_in_sprite >= 8) {
                pattern_addr_base += 16;
                row_in_sprite -= 8;
            }
        } else { 
            pattern_addr_base = ((ppu->ctrl & PPUCTRL_SPRITE_TABLE_ADDR) ? 0x1000 : 0x0000) + (tile_id * 16);
        }

        uint16_t pattern_addr = pattern_addr_base + row_in_sprite;
        uint8_t pat_low       = ppu_read_vram(ppu, pattern_addr);
        uint8_t pat_high      = ppu_read_vram(ppu, pattern_addr + 8);

        if (attributes & 0x40) { 
            pat_low  = bit_reverse_table[pat_low];
            pat_high = bit_reverse_table[pat_high];
        }

        bool is_foreground = !(attributes & 0x20);
        // Sprite zero is OAM entry 0, not whichever sprite happened to land first
        // in secondary OAM. Those coincide only when sprite 0 is on this
        // scanline; otherwise treating slot 0 as sprite zero fires the hit for an
        // unrelated sprite and moves every split that depends on it.
        bool is_sprite_0 = (original_oam_index == 0);
        uint8_t palette_base = attributes & 0x03;

        // Recorded whether or not any pixel turns out opaque: the span is what
        // the next fetch has to clear, and a transparent pixel here may still be
        // sitting on top of one a later (lower priority) sprite wrote.
        ppu->sprite_span_x[ppu->sprite_span_count++] = sprite_x;

        for (int px = 0; px < 8; ++px) {
            int screen_x = sprite_x + px;
            if (screen_x > 255) break;

            uint8_t pt_bit0 = (pat_low >> (7 - px)) & 1;
            uint8_t pt_bit1 = (pat_high >> (7 - px)) & 1;
            uint8_t current_spr_val = (pt_bit1 << 1) | pt_bit0;

            if (current_spr_val != 0) {
                ppu->scanline_sprite_buffer[screen_x].palette_idx = (palette_base << 2) + current_spr_val;
                ppu->scanline_sprite_buffer[screen_x].is_opaque = true;
                ppu->scanline_sprite_buffer[screen_x].is_foreground = is_foreground;
                ppu->scanline_sprite_buffer[screen_x].is_sprite_0 = is_sprite_0;
            }
        }
    }
}

// --- PPU API Implementation ---
PPU *PPU_Create(NES *nes)
{
    PPU *ppu = calloc(1, sizeof(PPU));
    if (!ppu) {
        DEBUG_ERROR("PPU_Create: Failed to allocate memory for PPU.");
        return NULL;
    }
    ppu->nes = nes;

    ppu->framebuffer                  = NULL;
    ppu->indexed_framebuffer          = NULL;
    ppu->internal_framebuffer         = NULL;
    ppu->internal_indexed_framebuffer = NULL;

    PPU_Reset(ppu);
    return ppu;
}

void PPU_Reset(PPU *ppu)
{
    memset(ppu->vram, 0, sizeof(ppu->vram));
    memset(ppu->palette, 0, sizeof(ppu->palette));
    memset(ppu->oam, 0, sizeof(ppu->oam));
    memset(ppu->secondary_oam, 0xFF, sizeof(ppu->secondary_oam));
    memset(ppu->secondary_oam_original_indices, 0, sizeof(ppu->secondary_oam_original_indices));
    // The sprite buffer is now cleared incrementally, span by span, so a reset
    // has to wipe it here rather than relying on the next fetch to do it.
    memset(ppu->scanline_sprite_buffer, 0, sizeof(ppu->scanline_sprite_buffer));
    ppu->sprite_span_count = 0;

    ppu->ctrl     = 0;
    ppu->mask     = 0;
    ppu->status   = PPUSTATUS_VBLANK;
    ppu->oam_addr = 0;

    ppu->addr_latch              = 0;
    ppu->fine_x                  = 0;
    ppu->data_buffer             = 0;
    ppu->open_bus                = 0;
    ppu->open_bus_last_update_ms = 0;

    ppu->vram_addr = 0;
    ppu->temp_addr = 0;

    // Refresh the cached timing before anything reads it. This used to happen at
    // the very end of PPU_Reset, so the first reset placed the PPU using a
    // scanline_prerender that was still zero.
    if (ppu->nes) {
        ppu->scanline_prerender  = ppu->nes->settings.timing.scanline_prerender;
        ppu->scanline_vblank     = ppu->nes->settings.timing.scanline_vblank;
        ppu->scanlines_visible   = ppu->nes->settings.timing.scanlines_visible;
        ppu->cycles_per_scanline = ppu->nes->settings.timing.cycles_per_scanline;
    }
    if (ppu->cycles_per_scanline <= 0) {
        ppu->cycles_per_scanline = 341;
    }

    ppu->scanline     = ppu->scanline_prerender;
    ppu->cycle        = 0;
    ppu->frame_odd    = false;
    ppu->frame_count  = 0;
    ppu->total_cycles = 0;

    ppu->nmi_occured           = false;
    ppu->nmi_output            = false;
    ppu->nmi_interrupt_line    = false;
    ppu->previous_nmi_output   = false;
    ppu->suppress_vblank_start = false;

    ppu->bg_nt_latch           = 0;
    ppu->bg_at_latch_low       = 0;
    ppu->bg_at_latch_high      = 0;
    ppu->bg_pt_low_latch       = 0;
    ppu->bg_pt_high_latch      = 0;
    ppu->bg_pattern_shift_low  = 0;
    ppu->bg_pattern_shift_high = 0;
    ppu->bg_attrib_shift_low   = 0;
    ppu->bg_attrib_shift_high  = 0;

    ppu->sprite_count_current_scanline = 0;

    if (!ppu->internal_framebuffer) {
        ppu->internal_framebuffer = calloc(PPU_FRAMEBUFFER_WIDTH * PPU_FRAMEBUFFER_HEIGHT, sizeof(uint32_t));
        if (!ppu->internal_framebuffer) {
            DEBUG_ERROR("PPU_Reset: Failed to allocate memory for internal framebuffer.");
        }
    }
    if (ppu->internal_framebuffer) {
        memset(ppu->internal_framebuffer, 0, PPU_FRAMEBUFFER_WIDTH * PPU_FRAMEBUFFER_HEIGHT * sizeof(uint32_t));
    }

    if (!ppu->internal_indexed_framebuffer) {
        ppu->internal_indexed_framebuffer = calloc(PPU_FRAMEBUFFER_WIDTH * PPU_FRAMEBUFFER_HEIGHT, sizeof(uint8_t));
        if (!ppu->internal_indexed_framebuffer) {
            DEBUG_ERROR("PPU_Reset: Failed to allocate memory for internal indexed framebuffer.");
        }
    }
    if (ppu->internal_indexed_framebuffer) {
        memset(ppu->internal_indexed_framebuffer, 0, PPU_FRAMEBUFFER_WIDTH * PPU_FRAMEBUFFER_HEIGHT * sizeof(uint8_t));
    }

    if (!ppu->framebuffer || ppu->framebuffer == ppu->internal_framebuffer) {
        ppu->framebuffer = ppu->internal_framebuffer;
    } else {
        memset(ppu->framebuffer, 0, PPU_FRAMEBUFFER_WIDTH * PPU_FRAMEBUFFER_HEIGHT * sizeof(uint32_t));
    }

    if (!ppu->indexed_framebuffer || ppu->indexed_framebuffer == ppu->internal_indexed_framebuffer) {
        ppu->indexed_framebuffer = ppu->internal_indexed_framebuffer;
    } else {
        memset(ppu->indexed_framebuffer, 0, PPU_FRAMEBUFFER_WIDTH * PPU_FRAMEBUFFER_HEIGHT * sizeof(uint8_t));
    }

    ppu->mirror_mode = MIRROR_HORIZONTAL;
    PPU_SetMirroring(ppu, ppu->mirror_mode);

    // Precalculate palette lookup mapping explicitly on reset.
    ppu_update_active_palette(ppu);
}

uint8_t PPU_ReadRegister(PPU *ppu, uint16_t addr)
{
    PPU_CatchUp(ppu); // Ensure lazy runahead syncs PPU immediately before read

    ppu_decay_open_bus(ppu);
    uint8_t data = ppu->open_bus;
    switch (addr & 0x0007) {
   case 0x0002: { // PPUSTATUS ($2002)
        // Reading on the exact tick the VBlank flag would be set suppresses both
        // the flag and the NMI for that frame.
        //
        // The window used to be several cycles wide on either side, padding for
        // reads that landed early because the clock only moved between
        // instructions. Now that the read is timed at the cycle it happens, the
        // padding is not only unnecessary but harmful: the pre-render arm set the
        // suppress flag ~240 scanlines before the VBlank it would go on to
        // cancel, and the `cycle <= 1` arm fired after the flag was already set,
        // so it cancelled the *following* frame's VBlank instead.
        bool read_before_vblank = (ppu->scanline == ppu->scanline_vblank && ppu->cycle == 0);

        if (read_before_vblank) {
            ppu->suppress_vblank_start = true;
        }

        data = (ppu->status & 0xE0) | (ppu->open_bus & 0x1F);
        ppu->status &= ~PPUSTATUS_VBLANK;
        ppu->nmi_occured = false;
        ppu_update_nmi_line(ppu);
        ppu->addr_latch = 0;
        ppu_drive_open_bus(ppu, data);
        break;
    }

    case 0x0004: // OAMDATA ($2004)
        data = ppu->oam[ppu->oam_addr];
        ppu_drive_open_bus(ppu, data);
        break;

    case 0x0007: // PPUDATA ($2007)
        if (ppu->vram_addr <= 0x3EFF) {
            data             = ppu->data_buffer;
            ppu->data_buffer = ppu_read_vram(ppu, ppu->vram_addr);
        } else {
            uint8_t palette_data = ppu_read_vram(ppu, ppu->vram_addr);
            uint8_t palette_mask = (ppu->mask & PPUMASK_GRAYSCALE) ? 0x30 : 0x3F;
            data                 = (ppu->open_bus & 0xC0) | (palette_data & palette_mask);
            ppu->data_buffer = ppu_read_vram(ppu, ppu->vram_addr & 0x2FFF); // Palette reads buffer with underlying VRAM
        }

        ppu->vram_addr += (ppu->ctrl & PPUCTRL_VRAM_INCREMENT) ? 32 : 1;
        ppu->vram_addr &= 0x3FFF;
        ppu_drive_open_bus(ppu, data);
        break;

    default: // Write-only registers or unmapped reads
        data = ppu->open_bus;
        break;
    }
    return data;
}

void PPU_WriteRegister(PPU *ppu, uint16_t addr, uint8_t value)
{
    PPU_CatchUp(ppu); // Ensure lazy runahead syncs PPU immediately before write

    ppu_drive_open_bus(ppu, value);

    switch (addr & 0x0007) {
    case 0x0000: // PPUCTRL ($2000)
        ppu->ctrl       = value;
        ppu->nmi_output = (value & PPUCTRL_NMI_ENABLE) != 0;
        ppu->temp_addr  = (ppu->temp_addr & 0xF3FF) | ((uint16_t)(value & 0x03) << 10);
        ppu_update_nmi_line(ppu);
        break;

    case 0x0001: { // PPUMASK ($2001)
        uint8_t changed = (uint8_t)(ppu->mask ^ value);
        if (changed) {
            ppu->mask = value;
            if (changed & 0xE0u) {
                ppu_update_active_palette(ppu);  // emphasis moved: redo all 64
            } else if (changed & PPUMASK_GRAYSCALE) {
                ppu_rebuild_render_palette(ppu); // grayscale only: 32 entries
            }
        }
        break;
    }

    case 0x0002: // PPUSTATUS ($2002) - Read-only
        break;

    case 0x0003: // OAMADDR ($2003)
        ppu->oam_addr = value;
        break;

    case 0x0004: // OAMDATA ($2004)
        if (!((ppu->scanline >= 0 && ppu->scanline <= (ppu->scanlines_visible - 1)) &&
              (ppu->cycle >= 1 && ppu->cycle <= 256) && (ppu->mask & (PPUMASK_SHOW_BG | PPUMASK_SHOW_SPRITES)))) {
            if ((ppu->oam_addr & 0x03) == 0x02) {
                value &= 0xE3; // Unimplemented bits 2-4 read back as 0
            }
            ppu->oam[ppu->oam_addr] = value;
        }
        ppu->oam_addr++;
        break;

    case 0x0005: // PPUSCROLL ($2005)
        if (ppu->addr_latch == 0) {
            ppu->temp_addr  = (ppu->temp_addr & 0xFFE0) | (value >> 3);
            ppu->fine_x     = value & 0x07;
            ppu->addr_latch = 1;
        } else {
            ppu->temp_addr =
                (ppu->temp_addr & 0x8C1F) | ((uint16_t)(value & 0xF8) << 2) | ((uint16_t)(value & 0x07) << 12);
            ppu->addr_latch = 0;
        }
        break;

    case 0x0006: // PPUADDR ($2006)
        if (ppu->addr_latch == 0) {
            ppu->temp_addr  = (ppu->temp_addr & 0x00FF) | ((uint16_t)(value & 0x3F) << 8);
            ppu->addr_latch = 1;
        } else {
            ppu->temp_addr = (ppu->temp_addr & 0xFF00) | value;
            ppu->vram_addr = ppu->temp_addr;
            ppu->vram_addr &= 0x3FFF;
            ppu->addr_latch = 0;
        }
        break;

    case 0x0007: // PPUDATA ($2007)
        ppu_write_vram(ppu, ppu->vram_addr, value);
        ppu->vram_addr += (ppu->ctrl & PPUCTRL_VRAM_INCREMENT) ? 32 : 1;
        ppu->vram_addr &= 0x3FFF;
        break;
    }
}

void PPU_DriveOpenBus(PPU *ppu, uint8_t value)
{
    ppu_drive_open_bus(ppu, value);
}

uint8_t PPU_GetOpenBusWithDecay(PPU *ppu)
{
    ppu_decay_open_bus(ppu);
    return ppu->open_bus;
}

void PPU_TriggerNMI(PPU *ppu)
{ 
    ppu_update_nmi_line(ppu);
}

void PPU_SetMirroring(PPU *ppu, MirrorMode mode)
{
    ppu->mirror_mode = mode;
    switch (mode) {
    case MIRROR_HORIZONTAL:
        ppu->nametable_ptrs[0] = &ppu->vram[0];
        ppu->nametable_ptrs[1] = &ppu->vram[0];
        ppu->nametable_ptrs[2] = &ppu->vram[0x0400];
        ppu->nametable_ptrs[3] = &ppu->vram[0x0400];
        break;
    case MIRROR_VERTICAL:
        ppu->nametable_ptrs[0] = &ppu->vram[0];
        ppu->nametable_ptrs[1] = &ppu->vram[0x0400];
        ppu->nametable_ptrs[2] = &ppu->vram[0];
        ppu->nametable_ptrs[3] = &ppu->vram[0x0400];
        break;
    case MIRROR_SINGLE_SCREEN_LOW:
        ppu->nametable_ptrs[0] = &ppu->vram[0];
        ppu->nametable_ptrs[1] = &ppu->vram[0];
        ppu->nametable_ptrs[2] = &ppu->vram[0];
        ppu->nametable_ptrs[3] = &ppu->vram[0];
        break;
    case MIRROR_SINGLE_SCREEN_HIGH:
        ppu->nametable_ptrs[0] = &ppu->vram[0x0400];
        ppu->nametable_ptrs[1] = &ppu->vram[0x0400];
        ppu->nametable_ptrs[2] = &ppu->vram[0x0400];
        ppu->nametable_ptrs[3] = &ppu->vram[0x0400];
        break;
    case MIRROR_FOUR_SCREEN:
        // cNES doesn't fully support 4-screen external VRAM yet, map all to 0 to prevent crashes
        ppu->nametable_ptrs[0] = &ppu->vram[0];
        ppu->nametable_ptrs[1] = &ppu->vram[0];
        ppu->nametable_ptrs[2] = &ppu->vram[0];
        ppu->nametable_ptrs[3] = &ppu->vram[0];
        break;
    }
}

// Last cycle of the current scanline. On odd frames the hardware drops the
// final tick of the pre-render line and jumps straight into the next frame.
// Both PPU_Step and the span runner need this, so it lives in one place.
static inline int ppu_last_cycle_of_line(const PPU *ppu)
{
    int last = ppu->cycles_per_scanline - 1;
    if (ppu->scanline == ppu->scanline_prerender && ppu->frame_odd && (ppu->mask & PPUMASK_SHOW_BG)) {
        last -= 1;
    }
    return last;
}

// Everything the pixel loop needs that cannot change while a span is running.
// Built once per span instead of once per pixel: the shifter tap depends only
// on fine_x, the two left-edge thresholds only on the mask, and the row
// pointers only on the scanline. All three used to be recomputed 61440 times a
// frame, along with two null checks and a multiply.
typedef struct PPU_PixelCtx {
    uint32_t *fb_row;       // Framebuffer row for this scanline, or NULL
    uint8_t  *ifb_row;      // Indexed framebuffer row, or NULL
    uint16_t  bit_selector; // 0x8000 >> fine_x: the background shifter tap
    int       bg_left;      // First x showing background; 256 means never
    int       spr_left;     // First x showing sprites; 256 means never
} PPU_PixelCtx;

static inline void ppu_build_pixel_ctx(PPU *ppu, int y, PPU_PixelCtx *ctx)
{
    const int row = y * PPU_FRAMEBUFFER_WIDTH;

    ctx->fb_row       = ppu->framebuffer ? ppu->framebuffer + row : NULL;
    ctx->ifb_row      = ppu->indexed_framebuffer ? ppu->indexed_framebuffer + row : NULL;
    ctx->bit_selector = (uint16_t)(0x8000u >> ppu->fine_x);

    // (mask & SHOW) && (x >= 8 || (mask & CLIP)), turned into one threshold.
    ctx->bg_left  = (ppu->mask & PPUMASK_SHOW_BG)
                        ? ((ppu->mask & PPUMASK_CLIP_BG) ? 0 : 8) : 256;
    ctx->spr_left = (ppu->mask & PPUMASK_SHOW_SPRITES)
                        ? ((ppu->mask & PPUMASK_CLIP_SPRITES) ? 0 : 8) : 256;
}

static inline void ppu_emit_pixel(PPU *ppu, int x, const PPU_PixelCtx *ctx)
{
    uint8_t bg_pixel_pattern_val = 0;
    uint8_t bg_palette_idx       = 0;

    const bool bg_visible_at_pixel = (x >= ctx->bg_left);
    if (bg_visible_at_pixel) {
        const uint16_t bit_selector = ctx->bit_selector;
        uint8_t pt_bit0 = (ppu->bg_pattern_shift_low & bit_selector) ? 1 : 0;
        uint8_t pt_bit1 = (ppu->bg_pattern_shift_high & bit_selector) ? 1 : 0;
        bg_pixel_pattern_val = (pt_bit1 << 1) | pt_bit0;

        uint8_t attrib_bit0 = (ppu->bg_attrib_shift_low & bit_selector) ? 1 : 0;
        uint8_t attrib_bit1 = (ppu->bg_attrib_shift_high & bit_selector) ? 1 : 0;
        bg_palette_idx      = (attrib_bit1 << 1) | attrib_bit0;
    }

    uint8_t spr_palette_idx   = 0;
    bool    spr_is_opaque     = false;
    bool    spr_is_foreground = true;

    const bool sprites_visible_at_pixel = (x >= ctx->spr_left);
    bool sprite_0_opaque_at_pixel = false;

    if (sprites_visible_at_pixel && ppu->scanline_sprite_buffer[x].is_opaque) {
        spr_is_opaque            = true;
        spr_is_foreground        = ppu->scanline_sprite_buffer[x].is_foreground;
        sprite_0_opaque_at_pixel = ppu->scanline_sprite_buffer[x].is_sprite_0;
        spr_palette_idx          = ppu->scanline_sprite_buffer[x].palette_idx;
    }

    if (sprite_0_opaque_at_pixel && bg_pixel_pattern_val != 0 && bg_visible_at_pixel &&
        sprites_visible_at_pixel &&
        x < 255 &&
        !(ppu->status & PPUSTATUS_SPRITE_0_HIT)) {
        ppu->status |= PPUSTATUS_SPRITE_0_HIT;
    }

    // Resolve priority to a palette RAM address rather than to a colour.
    // One lookup then covers mirroring, the 6-bit mask, grayscale and
    // emphasis; this used to walk two separate three-load chains and
    // discard one of them.
    uint8_t pal_addr;
    if (spr_is_opaque && (bg_pixel_pattern_val == 0 || spr_is_foreground)) {
        pal_addr = (uint8_t)(0x10u | spr_palette_idx);
    } else if (bg_pixel_pattern_val != 0) {
        pal_addr = (uint8_t)((bg_palette_idx << 2) | bg_pixel_pattern_val);
    } else {
        pal_addr = 0x00; // universal backdrop
    }

    if (ctx->ifb_row) ctx->ifb_row[x] = ppu->final_index[pal_addr];
    if (ctx->fb_row)  ctx->fb_row[x]  = ppu->final_color[pal_addr];
}

void PPU_Step(PPU *ppu)
{
    bool rendering_enabled = (ppu->mask & PPUMASK_SHOW_BG) || (ppu->mask & PPUMASK_SHOW_SPRITES);

    if (ppu->scanline == ppu->scanline_prerender) { // Pre-render line
        // The odd-frame tick is now dropped from the end of this scanline rather
        // than from its start, so cycle 0 always runs and needs no special case.
        if (ppu->cycle == 0) {
            ppu->status &= ~(PPUSTATUS_VBLANK | PPUSTATUS_SPRITE_0_HIT | PPUSTATUS_SPRITE_OVERFLOW);
            ppu->nmi_occured = false;
            ppu_update_nmi_line(ppu);
        }
        if (rendering_enabled && ppu->cycle >= 280 && ppu->cycle <= 304) {
            copy_vertical_bits(ppu);
        }
    }

    bool is_render_scanline = (ppu->scanline <= (ppu->scanlines_visible - 1)) ||
                              ppu->scanline == ppu->scanline_prerender;

    // --- Pixel Rendering (Cycles 1-256 of visible scanlines 0-(ppu->scanlines_visible - 1)) ---
    if (ppu->scanline <= (ppu->scanlines_visible - 1) && ppu->cycle >= 1 && ppu->cycle <= 256) {
        PPU_PixelCtx ctx;
        ppu_build_pixel_ctx(ppu, ppu->scanline, &ctx);
        ppu_emit_pixel(ppu, ppu->cycle - 1, &ctx);
    }

    if (is_render_scanline && rendering_enabled) {
        if ((ppu->cycle >= 1 && ppu->cycle <= 256) || (ppu->cycle >= 321 && ppu->cycle <= 336)) {
            ppu->bg_pattern_shift_low <<= 1;
            ppu->bg_pattern_shift_high <<= 1;
            ppu->bg_attrib_shift_low <<= 1;
            ppu->bg_attrib_shift_high <<= 1;
        }

        bool is_fetch_cycle_range = (ppu->cycle >= 1 && ppu->cycle <= 256) || (ppu->cycle >= 321 && ppu->cycle <= 336);
        if (is_fetch_cycle_range) {
            switch (ppu->cycle & 7) {  // Micro-optimization: Bitwise replace modulo 8 logic 
            case 1:
                load_background_tile_data(ppu);
                break;
            case 0: 
                feed_background_shifters(ppu);
                increment_coarse_x(ppu);
                break;
            }
        }

        if (ppu->cycle == 256) { 
            increment_fine_y(ppu);
        }

        if (ppu->cycle == 257) {
            copy_horizontal_bits(ppu);
            evaluate_sprites(ppu);
        }

        if (ppu->cycle == 321) {
            fetch_sprite_patterns(ppu); 
        }
    }

    if (ppu->scanline == ppu->scanline_vblank && ppu->cycle == 0) {
        if (!ppu->suppress_vblank_start) {
            ppu->status |= PPUSTATUS_VBLANK;
            ppu->nmi_occured = true;
            PPU_TriggerNMI(ppu);
        }
        ppu->suppress_vblank_start = false;
    }

    ppu->total_cycles++;

    // Dropping the odd-frame tick from the *end* of the pre-render line --
    // rather than skipping cycle 0 on arrival -- is what lets the two checks
    // above stay plain `cycle == 0` tests.
    const int last_cycle = ppu_last_cycle_of_line(ppu);

    ppu->cycle++;
    if (ppu->cycle > last_cycle) {
        ppu->cycle = 0;
        ppu->scanline++;

        if (ppu->scanline > ppu->scanline_prerender) {
            ppu->scanline  = 0;
            ppu->frame_odd = !ppu->frame_odd;
            ppu->frame_count++;
        }
    }
}

// Move the clock forward n ticks that provably do nothing. Only ever called
// with an n that stays inside the idle stretch it was measured from, so the
// scanline arithmetic never has to deal with the end-of-frame wrap or the
// odd-frame skip.
static inline void ppu_advance_idle(PPU *ppu, int n)
{
    const int cps = ppu->cycles_per_scanline;

    ppu->total_cycles += (uint64_t)n;

    // Almost every skip stays on the current scanline: the catch-up target is
    // an instruction boundary, so a span is usually under ten ticks. Worth a
    // fast path, because cycles_per_scanline is a runtime value and the
    // general form below costs two integer divisions.
    if (ppu->cycle + n < cps) {
        ppu->cycle += n;
        return;
    }

    const int pos = ppu->scanline * cps + ppu->cycle + n;
    ppu->scanline = pos / cps;
    ppu->cycle    = pos % cps;
}

// The next cycle at or after `cyc` on a render scanline at which PPU_Step does
// something. Only ever asked about cyc == 0 or cyc >= 257, because cycles
// 1-256 are the pixel pipeline and run as a span of their own.
static inline int ppu_next_render_event(int cyc, bool rendering, bool prerender)
{
    if (cyc == 0) return 0;          // pre-render clears the flags here
    if (!rendering) return INT_MAX;  // with rendering off nothing else happens

    if (cyc <= 257) return 257;      // copy_horizontal_bits + evaluate_sprites
    if (prerender) {
        if (cyc <= 280) return 280;  // copy_vertical_bits, then every tick
        if (cyc <= 304) return cyc;  // ...standing inside that window
    }
    if (cyc <= 321) return 321;      // fetch_sprite_patterns, shifting resumes
    if (cyc <= 336) return cyc;      // inside the second shift/fetch window
    return INT_MAX;                  // 337 to end of line: idle
}

// Runs a contiguous run of ticks under one set of hoisted invariants, or skips
// a run that does nothing, and returns how many ticks it consumed. Always at
// least one, so the caller's loop cannot stall.
//
// Every branch here either advances through PPU_Step, which is the reference
// for one tick, or reproduces exactly what PPU_Step would have done for the
// ticks it covers. The three shapes are:
//
//   * post-render and VBlank, where all 21 scanlines contain a single event;
//   * cycles 1-256 of a render scanline, the pixel pipeline;
//   * the rest of a render scanline, which is mostly idle between four points.
static uint32_t ppu_run_span(PPU *ppu, uint64_t budget)
{
    const int cps = ppu->cycles_per_scanline;
    const int sl  = ppu->scanline;
    const int cyc = ppu->cycle;

    const bool visible   = (sl <= ppu->scanlines_visible - 1);
    const bool prerender = (sl == ppu->scanline_prerender);

    // --- Post-render and VBlank ------------------------------------------
    // Nothing is fetched, evaluated or drawn across these 21 scanlines. The
    // only thing that happens is the VBlank flag and the NMI, on one tick, so
    // the other ~7100 ticks a frame are pure arithmetic.
    if (!visible && !prerender) {
        int64_t to_event;
        if (sl < ppu->scanline_vblank) {
            to_event = (int64_t)(ppu->scanline_vblank - sl) * cps - cyc;
        } else if (sl == ppu->scanline_vblank && cyc == 0) {
            to_event = 0; // standing on the tick that raises VBlank
        } else {
            to_event = (int64_t)(ppu->scanline_prerender - sl) * cps - cyc;
        }

        if (to_event > 0) {
            uint32_t n = (uint64_t)to_event < budget ? (uint32_t)to_event : (uint32_t)budget;
            ppu_advance_idle(ppu, (int)n);
            return n;
        }
        PPU_Step(ppu); // the VBlank tick itself
        return 1;
    }

    const bool rendering = (ppu->mask & (PPUMASK_SHOW_BG | PPUMASK_SHOW_SPRITES)) != 0;

    // --- Cycles 1-256 of a render scanline: the pixel pipeline ------------
    if (cyc >= 1 && cyc <= 256) {
        uint32_t n = (uint32_t)(257 - cyc);
        if (budget < n) n = (uint32_t)budget;

        // Four shapes, so that neither `visible` nor `rendering` has to be
        // retested on any of the ticks inside the run.
        if (!rendering && !visible) {
            // Pre-render with rendering off fetches nothing and draws nothing,
            // so its pixel range is as dead as the VBlank lines are.
            ppu_advance_idle(ppu, (int)n);
            return n;
        }

        if (!rendering) {
            // Visible, both show bits clear: every pixel is the backdrop and
            // no shifter or fetch runs. This is the blank screen games sit on
            // while they load, and it comes down to one store per pixel.
            PPU_PixelCtx ctx;
            ppu_build_pixel_ctx(ppu, sl, &ctx);

            const uint32_t colour = ppu->final_color[0];
            const uint8_t  index  = ppu->final_index[0];
            for (uint32_t i = 0; i < n; i++) {
                const int x = cyc - 1 + (int)i;
                if (ctx.ifb_row) ctx.ifb_row[x] = index;
                if (ctx.fb_row)  ctx.fb_row[x]  = colour;
            }
        } else if (!visible) {
            // Pre-render with rendering on: the background pipeline runs to
            // prime the shifters for scanline 0, but nothing is drawn.
            int c = cyc;
            for (uint32_t i = 0; i < n; i++, c++) {
                ppu->bg_pattern_shift_low <<= 1;
                ppu->bg_pattern_shift_high <<= 1;
                ppu->bg_attrib_shift_low <<= 1;
                ppu->bg_attrib_shift_high <<= 1;

                switch (c & 7) {
                case 1: load_background_tile_data(ppu); break;
                case 0: feed_background_shifters(ppu); increment_coarse_x(ppu); break;
                }

                if (c == 256) increment_fine_y(ppu);
            }
        } else {
            // The common case: a visible scanline with rendering on.
            PPU_PixelCtx ctx;
            ppu_build_pixel_ctx(ppu, sl, &ctx);

            int c = cyc;
            for (uint32_t i = 0; i < n; i++, c++) {
                ppu_emit_pixel(ppu, c - 1, &ctx);

                ppu->bg_pattern_shift_low <<= 1;
                ppu->bg_pattern_shift_high <<= 1;
                ppu->bg_attrib_shift_low <<= 1;
                ppu->bg_attrib_shift_high <<= 1;

                switch (c & 7) {
                case 1: load_background_tile_data(ppu); break;
                case 0: feed_background_shifters(ppu); increment_coarse_x(ppu); break;
                }

                if (c == 256) increment_fine_y(ppu);
            }
        }

        // Cycle 256 is the highest this span can reach and the line is at least
        // 340 cycles long, so the end-of-line wrap is never in play here.
        ppu->cycle = cyc + (int)n;
        ppu->total_cycles += n;
        return n;
    }

    // --- The rest of a render scanline ------------------------------------
    {
        int next = ppu_next_render_event(cyc, rendering, prerender);
        const int last = ppu_last_cycle_of_line(ppu);

        // The wrap has to go through PPU_Step so the scanline advance and the
        // odd-frame skip stay in exactly one place.
        if (next > last) next = last;

        if (next > cyc) {
            uint32_t n = (uint32_t)(next - cyc);
            if (budget < n) n = (uint32_t)budget;
            ppu_advance_idle(ppu, (int)n);
            return n;
        }
    }

    PPU_Step(ppu);
    return 1;
}

inline uint8_t PPU_CHR_Read(PPU *ppu, uint16_t addr)
{
    return BUS_PPU_ReadCHR(ppu->nes->bus, addr);
}

inline void PPU_CHR_Write(PPU *ppu, uint16_t addr, uint8_t value)
{
    BUS_PPU_WriteCHR(ppu->nes->bus, addr, value);
}

void PPU_GetPatternTableData(PPU *ppu, int table_idx, uint8_t *buffer_128x128_indexed_pixels)
{
    uint16_t base_addr = (table_idx == 0) ? 0x0000 : 0x1000;

    for (int tile_y = 0; tile_y < 16; ++tile_y) {
        for (int tile_x = 0; tile_x < 16; ++tile_x) {
            uint16_t tile_offset_in_pt = (tile_y * 16 + tile_x) * 16;
            for (int row = 0; row < 8; ++row) {
                uint8_t pt_low  = BUS_PPU_ReadCHR(ppu->nes->bus, base_addr + tile_offset_in_pt + row);
                uint8_t pt_high = BUS_PPU_ReadCHR(ppu->nes->bus, base_addr + tile_offset_in_pt + row + 8);
                for (int col = 0; col < 8; ++col) {
                    uint8_t bit0              = (pt_low >> (7 - col)) & 1;
                    uint8_t bit1              = (pt_high >> (7 - col)) & 1;
                    uint8_t pixel_palette_idx = (bit1 << 1) | bit0; 

                    int buffer_x                                             = tile_x * 8 + col;
                    int buffer_y                                             = tile_y * 8 + row;
                    buffer_128x128_indexed_pixels[buffer_y * 128 + buffer_x] = pixel_palette_idx;
                }
            }
        }
    }
}

const uint8_t *PPU_GetNametable(PPU *ppu, int index)
{
    if (index < 0 || index > 3) {
        return NULL;
    }
    return ppu->nametable_ptrs[index];
}

void PPU_GetScanlineCycle(PPU *ppu, int *scanline, int *cycle)
{
    if (scanline) *scanline = ppu->scanline;
    if (cycle) *cycle = ppu->cycle;
}

void PPU_SetOutputBuffers(PPU *ppu, uint32_t *framebuffer, uint8_t *indexed_framebuffer)
{
    if (!ppu) return;
    ppu->framebuffer = framebuffer ? framebuffer : ppu->internal_framebuffer;
    ppu->indexed_framebuffer = indexed_framebuffer ? indexed_framebuffer : ppu->internal_indexed_framebuffer;
}

const uint32_t* PPU_GetFramebuffer(PPU* ppu)
{
    if (!ppu) return NULL;
    return ppu->framebuffer;
}

const uint8_t* PPU_GetIndexedFramebuffer(PPU* ppu)
{
    if (!ppu) return NULL;
    return ppu->indexed_framebuffer;
}

const uint32_t* PPU_GetPalette(PPU *ppu)
{
    if (!ppu || !ppu->nes) return NULL;
    return (const uint32_t*)ppu->nes->settings.video.palette;
}

const uint32_t* PPU_SetPalette(PPU *ppu, const uint32_t *palette)
{
    if (!ppu || !ppu->nes || !palette) return NULL;
    memcpy(ppu->nes->settings.video.palette, palette, 64 * sizeof(uint32_t));
    ppu_update_active_palette(ppu);
    return palette;
}

const uint32_t* PPU_GetPaletteRAM(PPU* ppu)
{
    if (!ppu) return NULL;
    return (const uint32_t*)ppu->palette;
}

const uint8_t* PPU_GetOAM(PPU* ppu)
{
    if (!ppu) return NULL;
    return ppu->oam;
}

void PPU_Destroy(PPU *ppu)
{
    if (!ppu) return;

    if (ppu->internal_framebuffer) {
        free(ppu->internal_framebuffer);
    }
    if (ppu->internal_indexed_framebuffer) {
        free(ppu->internal_indexed_framebuffer);
    }

    ppu->framebuffer                  = NULL;
    ppu->indexed_framebuffer          = NULL;
    ppu->internal_framebuffer         = NULL;
    ppu->internal_indexed_framebuffer = NULL;

    free(ppu);
}
