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

#include "cNES/ppu.h"

#if defined(__SSE2__) || defined(_M_AMD64) || defined(_M_X64) || (_M_IX86_FP == 2)
#define PPU_USE_SIMD_COLOR_EMPHASIS
#endif

#ifdef PPU_USE_SIMD_COLOR_EMPHASIS
#include <emmintrin.h> // SSE2 intrinsics
#endif

#undef PPU_USE_SIMD_COLOR_EMPHASIS

#define PPU_OPEN_BUS_DECAY_MS 750ULL

#ifndef PPU_USE_SIMD_COLOR_EMPHASIS
static inline uint32_t apply_color_emphasis(const uint32_t* base_color, uint8_t ppu_mask)
{
    // Check bits 5, 6, and 7
    if (!(ppu_mask & 0xE0)) return *base_color;

    uint32_t c = *base_color;
    
    // Extract: RGBA layout assumes Red is at LSB
    float r = (float)(c >> 24 & 0xFF);
    float g = (float)((c >> 16) & 0xFF);
    float b = (float)((c >> 8) & 0xFF);
    uint32_t a = c & 0x000000FF; // Preserve Alpha

    const float f = 0.75f;
    float r_mult = 1.0f, g_mult = 1.0f, b_mult = 1.0f;

    // Apply attenuation to the channels NOT emphasized
    if (ppu_mask & PPUMASK_EMPHASIZE_RED)   { g_mult = f; b_mult = f; }
    if (ppu_mask & PPUMASK_EMPHASIZE_GREEN) { r_mult = f; b_mult = f; }
    if (ppu_mask & PPUMASK_EMPHASIZE_BLUE)  { r_mult = f; g_mult = f; }

    uint32_t R = (uint32_t)(r * r_mult);
    uint32_t G = (uint32_t)(g * g_mult);
    uint32_t B = (uint32_t)(b * b_mult);

    // Reconstruct RGBA
    return a | (B << 8) | (G << 16) | (R << 24);
}
#else  
static inline uint32_t apply_color_emphasis(const uint32_t* base_color, uint8_t ppu_mask)
{
    if (!(ppu_mask & 0xE0)) return *base_color;

    __m128i color_vec = _mm_cvtsi32_si128(*base_color);
    // Unpack bytes to 16-bit words. Result: [0, Alpha, 0, Blue, 0, Green, 0, Red]
    color_vec = _mm_unpacklo_epi8(color_vec, _mm_setzero_si128()); 

    const uint16_t atten = 192; // 0.75 * 256
    uint16_t r_f = 256, g_f = 256, b_f = 256;

    if (ppu_mask & PPUMASK_EMPHASIZE_RED)   { g_f = atten; b_f = atten; }
    if (ppu_mask & PPUMASK_EMPHASIZE_GREEN) { r_f = atten; b_f = atten; }
    if (ppu_mask & PPUMASK_EMPHASIZE_BLUE)  { r_f = atten; g_f = atten; }

    // Map factors to match [Alpha, Blue, Green, Red] order
    // _mm_set_epi16 parameters are (w7, w6, w5, w4, w3, w2, w1, w0)
    // w0 corresponds to Red, w1 to Green, w2 to Blue, w3 to Alpha
    __m128i factors = _mm_set_epi16(0, 0, 0, 0, 256, b_f, g_f, r_f);

    color_vec = _mm_mullo_epi16(color_vec, factors);
    color_vec = _mm_srli_epi16(color_vec, 8); // Shift back to 8-bit range
    
    // Pack 16-bit words back to 8-bit bytes (RGBA)
    color_vec = _mm_packus_epi16(color_vec, _mm_setzero_si128());

    return (uint32_t)_mm_cvtsi128_si32(color_vec);
}
#endif // PPU_USE_SIMD_COLOR_EMPHASIS

static inline void ppu_update_active_palette(PPU *ppu)
{
    for (int i = 0; i < 64; ++i) {
        const uint32_t *base_color = &ppu->nes->settings.video.palette[i];
        ppu->active_palette[i] = apply_color_emphasis(base_color, ppu->mask);
    }
}

uint64_t PPU_GetTotalCycles(PPU *ppu) {
    if (!ppu || !ppu->nes) return 0;
    
    uint64_t cycles_per_scanline = ppu->nes->settings.timing.cycles_per_scanline;
    uint64_t cycles_per_frame = (ppu->nes->settings.timing.scanline_prerender + 1) * cycles_per_scanline;
    
    int scanline = ppu->scanline;
    if (scanline < 0) scanline = ppu->nes->settings.timing.scanline_prerender;

    // Calculate absolute total cycles elapsed since boot/reset
    uint64_t current_absolute = ppu->frame_count * cycles_per_frame + (uint64_t)scanline * cycles_per_scanline + ppu->cycle;
    
    // Offset by the PPU's starting scanline to normalize starting cycles to 0
    uint64_t start_absolute = ppu->nes->settings.timing.scanline_prerender * cycles_per_scanline;
    
    if (current_absolute < start_absolute) return 0;
    return current_absolute - start_absolute;
}

void PPU_CatchUp(PPU *ppu) {
    if (!ppu || !ppu->nes || !ppu->nes->cpu) return;
    
    uint64_t target_ppu_cycles = ppu->nes->cpu->total_cycles * 3;
    while (PPU_GetTotalCycles(ppu) < target_ppu_cycles) {
        PPU_Step(ppu);
    }
}

static inline uint64_t ppu_now_ms(PPU *ppu)
{
    // NTSC PPU clock is ~5.369 MHz, so ~5369 cycles per millisecond
    return PPU_GetTotalCycles(ppu) / 5369ULL;
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
    uint16_t pal_addr = addr & 0x1F;
    if ((pal_addr & 0x03) == 0) {
        pal_addr &= 0x0F;
    }
    return ppu->palette[pal_addr];
}

static inline void ppu_palette_write(PPU *ppu, uint16_t addr, uint8_t value)
{
    uint16_t pal_addr = addr & 0x1F;
    if ((pal_addr & 0x03) == 0) {
        pal_addr &= 0x0F;
    }
    ppu->palette[pal_addr] = value;
    ppu_update_active_palette(ppu);
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

static inline uint16_t mirror_vram_addr(PPU *ppu, uint16_t addr)
{
    addr &= 0x0FFF;
    switch (ppu->mirror_mode) {
    case MIRROR_HORIZONTAL:
        return ((addr >> 1) & 0x0400) | (addr & 0x03FF);
    case MIRROR_VERTICAL:
        return addr & 0x07FF;
    case MIRROR_SINGLE_SCREEN_LOW:
        return addr & 0x03FF;
    case MIRROR_SINGLE_SCREEN_HIGH:
        return (addr & 0x03FF) | 0x0400;
    case MIRROR_FOUR_SCREEN:
        return addr;
    default:
        return ((addr >> 1) & 0x0400) | (addr & 0x03FF);
    }
}

static inline uint8_t ppu_read_vram(PPU *ppu, uint16_t addr)
{
    addr &= 0x3FFF;

    if (addr < 0x2000) { // CHR ROM/RAM ($0000 - $1FFF)
        return BUS_PPU_ReadCHR(ppu->nes->bus, addr);
    } else if (addr < 0x3F00) { // Nametable RAM ($2000 - $3EFF)
        return ppu->vram[mirror_vram_addr(ppu, addr & 0x2FFF)];
    } else { // Palette RAM ($3F00 - $3FFF)
        return ppu_palette_read(ppu, addr);
    }

    DEBUG_ERROR("PPU: Unhandled PPU VRAM read at address 0x%04X", addr);
    return 0;
}

static inline void ppu_write_vram(PPU *ppu, uint16_t addr, uint8_t value)
{
    addr &= 0x3FFF;

    if (addr < 0x2000) { // CHR RAM ($0000 - $1FFF)
        BUS_PPU_WriteCHR(ppu->nes->bus, addr, value);
    } else if (addr < 0x3F00) { // Nametable RAM ($2000 - $3EFF)
        ppu->vram[mirror_vram_addr(ppu, addr & 0x2FFF)] = value;
    } else if (addr < 0x4000) { // Palette RAM ($3F00 - $3FFF)
        ppu_palette_write(ppu, addr, value);
    } else {
        DEBUG_ERROR("PPU: Unhandled PPU VRAM write at address 0x%04X value 0x%02X", addr, value);
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
    ppu->sprite_zero_found_for_next_scanline = false;

    memset(ppu->secondary_oam, 0xFF, PPU_SECONDARY_OAM_SIZE); 

    uint8_t secondary_oam_idx = 0;
    uint8_t sprite_height     = (ppu->ctrl & PPUCTRL_SPRITE_SIZE) ? 16 : 8;

    for (int oam_idx = 0; oam_idx < 64; ++oam_idx) {
        uint8_t sprite_y  = ppu->oam[oam_idx * 4 + 0];
        int next_scanline = (ppu->scanline == ppu->nes->settings.timing.scanline_prerender) ? 0 : (ppu->scanline + 1);
        int row_on_scanline = next_scanline - sprite_y - 1;

        if (row_on_scanline >= 0 && row_on_scanline < sprite_height) {
            if (secondary_oam_idx < 8) {
                memcpy(&ppu->secondary_oam[secondary_oam_idx * 4], &ppu->oam[oam_idx * 4], 4);
                ppu->secondary_oam_original_indices[secondary_oam_idx] = oam_idx; 

                if (oam_idx == 0) {
                    ppu->sprite_zero_found_for_next_scanline = true; 
                }
                secondary_oam_idx++;
            } else {
                ppu->status |= PPUSTATUS_SPRITE_OVERFLOW;
                continue;
            }
        }
    }
    ppu->sprite_count_current_scanline = secondary_oam_idx;
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

    for (int i = 0; i < ppu->sprite_count_current_scanline; ++i) {
        uint8_t sprite_y_oam = ppu->secondary_oam[i * 4 + 0];
        uint8_t tile_id      = ppu->secondary_oam[i * 4 + 1];
        uint8_t attributes   = ppu->secondary_oam[i * 4 + 2];
        uint8_t sprite_x     = ppu->secondary_oam[i * 4 + 3];

        ppu->sprite_shifters[i].x_pos      = sprite_x;
        ppu->sprite_shifters[i].attributes = attributes;
        ppu->sprite_shifters[i].original_oam_index =
            ppu->secondary_oam_original_indices[i]; 

        int next_scanline = (ppu->scanline == ppu->nes->settings.timing.scanline_prerender) ? 0 : (ppu->scanline + 1);
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

        // Bitwise table lookup horizontally flips patterns instantaneously. 
        if (attributes & 0x40) { 
            pat_low  = bit_reverse_table[pat_low];
            pat_high = bit_reverse_table[pat_high];
        }

        ppu->sprite_shifters[i].pattern_low  = pat_low;
        ppu->sprite_shifters[i].pattern_high = pat_high;
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
    if (ppu->vram) memset(ppu->vram, 0, sizeof(ppu->vram));
    if (ppu->palette) memset(ppu->palette, 0, sizeof(ppu->palette));
    if (ppu->oam) memset(ppu->oam, 0, sizeof(ppu->oam));
    if (ppu->secondary_oam) memset(ppu->secondary_oam, 0xFF, sizeof(ppu->secondary_oam));
    if (ppu->secondary_oam_original_indices)
        memset(ppu->secondary_oam_original_indices, 0, sizeof(ppu->secondary_oam_original_indices));
    if (ppu->sprite_shifters) memset(ppu->sprite_shifters, 0, sizeof(ppu->sprite_shifters));

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

    ppu->scanline    = ppu->nes->settings.timing.scanline_prerender;
    ppu->cycle       = 0;
    ppu->frame_odd   = false;
    ppu->frame_count = 0;

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

    ppu->sprite_count_current_scanline       = 0;
    ppu->sprite_zero_found_for_next_scanline = false;

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
        // Check exact timing alignment mapping limits for VBLANK edge suppression 
        bool read_before_vblank = 
            (ppu->scanline == ppu->nes->settings.timing.scanline_prerender && ppu->cycle >= 339) ||
            (ppu->scanline == ppu->nes->settings.timing.scanline_vblank && ppu->cycle <= 1);
            
        bool read_exactly_on_vblank = 
            (ppu->scanline == ppu->nes->settings.timing.scanline_vblank && ppu->cycle >= 2 && ppu->cycle <= 3);

        if (read_before_vblank) {
            ppu->suppress_vblank_start = true;
        }

        if (read_exactly_on_vblank) {
            ppu->status &= ~PPUSTATUS_VBLANK;
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

    case 0x0001: // PPUMASK ($2001)
        if (ppu->mask != value) {
            ppu->mask = value;
            ppu_update_active_palette(ppu);
        }
        break;

    case 0x0002: // PPUSTATUS ($2002) - Read-only
        break;

    case 0x0003: // OAMADDR ($2003)
        ppu->oam_addr = value;
        break;

    case 0x0004: // OAMDATA ($2004)
        if (!((ppu->scanline >= 0 && ppu->scanline <= (ppu->nes->settings.timing.scanlines_visible - 1)) &&
              (ppu->cycle >= 1 && ppu->cycle <= 256) && (ppu->mask & (PPUMASK_SHOW_BG | PPUMASK_SHOW_SPRITES)))) {
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

inline void PPU_DoOAMDMA(PPU *ppu, const uint8_t *dma_page_data)
{
    memcpy(ppu->oam, dma_page_data, 256);
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
}

void PPU_Step(PPU *ppu)
{
    bool rendering_enabled = (ppu->mask & PPUMASK_SHOW_BG) || (ppu->mask & PPUMASK_SHOW_SPRITES);

    if (ppu->scanline == ppu->nes->settings.timing.scanline_prerender) { // Pre-render line
        bool first_prerender_cycle = (ppu->cycle == 0) || (ppu->cycle == 1 && ppu->frame_odd && rendering_enabled &&
                                                           (ppu->mask & PPUMASK_SHOW_BG));
        if (first_prerender_cycle) {
            ppu->status &= ~(PPUSTATUS_VBLANK | PPUSTATUS_SPRITE_0_HIT | PPUSTATUS_SPRITE_OVERFLOW);
            ppu->nmi_occured = false;
            ppu_update_nmi_line(ppu);
        }
        if (rendering_enabled && ppu->cycle >= 280 && ppu->cycle <= 304) {
            copy_vertical_bits(ppu);
        }
    }

    bool is_render_scanline = (ppu->scanline <= (ppu->nes->settings.timing.scanlines_visible - 1)) ||
                              ppu->scanline == ppu->nes->settings.timing.scanline_prerender;

    // --- Pixel Rendering (Cycles 1-256 of visible scanlines 0-(ppu->nes->settings.timing.scanlines_visible - 1)) ---
    if (ppu->scanline <= (ppu->nes->settings.timing.scanlines_visible - 1) && ppu->cycle >= 1 && ppu->cycle <= 256) {
        int x = ppu->cycle - 1;
        int y = ppu->scanline;

        uint8_t bg_pixel_pattern_val = 0;
        uint8_t bg_palette_idx       = 0;

        bool bg_visible_at_pixel = (ppu->mask & PPUMASK_SHOW_BG) && (x >= 8 || (ppu->mask & PPUMASK_CLIP_BG));
        if (bg_visible_at_pixel) {
            uint16_t bit_selector = 0x8000 >> ppu->fine_x;
            uint8_t  pt_bit0      = (ppu->bg_pattern_shift_low & bit_selector) ? 1 : 0;
            uint8_t  pt_bit1      = (ppu->bg_pattern_shift_high & bit_selector) ? 1 : 0;
            bg_pixel_pattern_val  = (pt_bit1 << 1) | pt_bit0;

            uint8_t attrib_bit0 = (ppu->bg_attrib_shift_low & bit_selector) ? 1 : 0;
            uint8_t attrib_bit1 = (ppu->bg_attrib_shift_high & bit_selector) ? 1 : 0;
            bg_palette_idx      = (attrib_bit1 << 1) | attrib_bit0;
        }

        uint8_t final_bg_color_idx =
            (bg_pixel_pattern_val == 0) ? ppu_palette_read(ppu, 0x3F00) : 
                ppu_palette_read(ppu, (uint16_t)(0x3F00 + (bg_palette_idx << 2) + bg_pixel_pattern_val));
        final_bg_color_idx &= 0x3F;

        uint8_t spr_pixel_pattern_val = 0;
        uint8_t spr_final_color_idx   = 0; 
        bool    spr_is_opaque         = false;
        bool    spr_is_foreground     = true;

        bool sprites_visible_at_pixel = (ppu->mask & PPUMASK_SHOW_SPRITES) && (x >= 8 || (ppu->mask & PPUMASK_CLIP_SPRITES));
        bool sprite_0_opaque_at_pixel = false;

        // Hardware-Accurate Sprite Execution (Shifters and X Counters)
        if (ppu->mask & PPUMASK_SHOW_SPRITES) {
            for (int i = 0; i < ppu->sprite_count_current_scanline; ++i) {
                SpriteShifter *s = &ppu->sprite_shifters[i];
                
                // Once an active sprite arrives at its X counter 0, shift it over 8 pixel cycles 
                if (s->x_pos == 0) {
                    uint8_t spr_pt_bit0 = (s->pattern_low >> 7) & 1;
                    uint8_t spr_pt_bit1 = (s->pattern_high >> 7) & 1;
                    uint8_t current_spr_val = (spr_pt_bit1 << 1) | spr_pt_bit0;

                    if (s->original_oam_index == 0 && current_spr_val != 0) {
                        sprite_0_opaque_at_pixel = true;
                    }

                    if (current_spr_val != 0 && !spr_is_opaque) {
                        if (sprites_visible_at_pixel) {
                            spr_pixel_pattern_val = current_spr_val;
                            uint8_t spr_palette_idx_bits = s->attributes & 0x03;
                            spr_final_color_idx = ppu_palette_read(
                                ppu, (uint16_t)(0x3F10 + (spr_palette_idx_bits << 2) + spr_pixel_pattern_val));
                            spr_final_color_idx &= 0x3F;

                            spr_is_opaque     = true;
                            spr_is_foreground = !(s->attributes & 0x20);
                        }
                    }

                    s->pattern_low <<= 1;
                    s->pattern_high <<= 1;
                } else {
                    s->x_pos--;
                }
            }
        }

        if (sprite_0_opaque_at_pixel && bg_pixel_pattern_val != 0 && bg_visible_at_pixel &&
            sprites_visible_at_pixel && 
            x < 255 &&                  
            !(ppu->status & PPUSTATUS_SPRITE_0_HIT)) {
            ppu->status |= PPUSTATUS_SPRITE_0_HIT;
        }

        uint8_t combined_color_idx;
        if (spr_is_opaque) {
            if (bg_pixel_pattern_val == 0) { 
                combined_color_idx = spr_final_color_idx;
            } else { 
                if (spr_is_foreground) {
                    combined_color_idx = spr_final_color_idx;
                } else { 
                    combined_color_idx = final_bg_color_idx;
                }
            }
        } else { 
            combined_color_idx = final_bg_color_idx;
        }

        if (ppu->indexed_framebuffer) {
            ppu->indexed_framebuffer[y * PPU_FRAMEBUFFER_WIDTH + x] = combined_color_idx;
        }

        // Apply O(1) Precalculated Color Lookup Array
        uint32_t final_pixel_color = ppu->active_palette[combined_color_idx];

        if (ppu->framebuffer) {
            ppu->framebuffer[y * PPU_FRAMEBUFFER_WIDTH + x] = final_pixel_color;
        }
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
            if (ppu->scanline <= (ppu->nes->settings.timing.scanlines_visible - 1)) { 
                evaluate_sprites(ppu);  
            } else if (ppu->scanline == ppu->nes->settings.timing.scanline_prerender) { 
                ppu->sprite_count_current_scanline = 0;
            }
        }

        if (ppu->cycle == 321 && ppu->scanline <= (ppu->nes->settings.timing.scanlines_visible - 1)) {
            fetch_sprite_patterns(ppu); 
        }
    }

    bool first_vblank_cycle =
        (ppu->cycle == 0) || (ppu->cycle == 1 && ppu->frame_odd && rendering_enabled && (ppu->mask & PPUMASK_SHOW_BG));
    if (ppu->scanline == ppu->nes->settings.timing.scanline_vblank && first_vblank_cycle) {
        if (!ppu->suppress_vblank_start) {
            ppu->status |= PPUSTATUS_VBLANK;
            ppu->nmi_occured = true;
            PPU_TriggerNMI(ppu); 
        }
        ppu->suppress_vblank_start = false;
    }

    ppu->cycle++;
    if (ppu->cycle > 340) {
        ppu->cycle = 0;
        ppu->scanline++;

        if (ppu->scanline == ppu->nes->settings.timing.scanline_prerender && ppu->frame_odd && rendering_enabled &&
            (ppu->mask & PPUMASK_SHOW_BG)) { 
            ppu->cycle = 1;                  
        }

        if (ppu->scanline > ppu->nes->settings.timing.scanline_prerender) {
            ppu->scanline  = 0;
            ppu->frame_odd = !ppu->frame_odd;
            ppu->frame_count++;
        }
    }
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
    uint16_t logical_base_addr = 0x2000 + index * 0x0400;
    uint16_t physical_addr     = mirror_vram_addr(ppu, logical_base_addr);

    if (physical_addr < PPU_VRAM_SIZE) {
        return &ppu->vram[physical_addr];
    }

    return NULL;
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