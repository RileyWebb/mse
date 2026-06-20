#ifndef PPU_H
#define PPU_H

#include <stdint.h>
#include <stdbool.h> // For bool type

// Forward declaration to avoid circular dependency with nes.h
typedef struct NES NES;

// --- PPU Constants ---
#define PPU_VRAM_SIZE          0x800  // 2KB of Name Table RAM (actually 2 Nametables of 1KB each)
#define PPU_PALETTE_RAM_SIZE   32     // 256 bytes of Palette RAM (64 32-bit colors)
#define PPU_OAM_SIZE           0x100  // 256 bytes for Primary Object Attribute Memory (64 sprites * 4 bytes)
#define PPU_SECONDARY_OAM_SIZE 32     // 8 sprites * 4 bytes each for sprites on current scanline

// Define framebuffer dimensions
#define PPU_FRAMEBUFFER_WIDTH 256
#define PPU_FRAMEBUFFER_HEIGHT 240

// --- PPU Register Bit Definitions ---

// PPUCTRL ($2000) - PPU Control Register
#define PPUCTRL_NAMETABLE_X        0x01 // Nametable select X (0 = $2000, 1 = $2400)
#define PPUCTRL_NAMETABLE_Y        0x02 // Nametable select Y (0 = $2000, 1 = $2800)
#define PPUCTRL_VRAM_INCREMENT     0x04 // VRAM address increment per CPU read/write of PPUDATA (0: add 1, 1: add 32)
#define PPUCTRL_SPRITE_TABLE_ADDR  0x08 // Sprite pattern table address for 8x8 sprites (0: $0000, 1: $1000)
#define PPUCTRL_BG_TABLE_ADDR      0x10 // Background pattern table address (0: $0000, 1: $1000)
#define PPUCTRL_SPRITE_SIZE        0x20 // Sprite size (0: 8x8, 1: 8x16)
#define PPUCTRL_MASTER_SLAVE       0x40 // PPU master/slave select (0: read backdrop from EXT pins; 1: output color on EXT pins) - Rarely used
#define PPUCTRL_NMI_ENABLE         0x80 // Generate NMI at start of VBlank (0: disabled, 1: enabled)

// PPUMASK ($2001) - PPU Mask Register
#define PPUMASK_GRAYSCALE          0x01 // Grayscale (0: color, 1: grayscale)
#define PPUMASK_CLIP_BG            0x02 // Show background in leftmost 8 pixels (0: hide, 1: show)
#define PPUMASK_CLIP_SPRITES       0x04 // Show sprites in leftmost 8 pixels (0: hide, 1: show)
#define PPUMASK_SHOW_BG            0x08 // Show background (0: hide, 1: show)
#define PPUMASK_SHOW_SPRITES       0x10 // Show sprites (0: hide, 1: show)
#define PPUMASK_EMPHASIZE_RED      0x20 // Emphasize red
#define PPUMASK_EMPHASIZE_GREEN    0x40 // Emphasize green
#define PPUMASK_EMPHASIZE_BLUE     0x80 // Emphasize blue

// PPUSTATUS ($2002) - PPU Status Register (read-only)
#define PPUSTATUS_OPEN_BUS_LSB     0x1F // Lower 5 bits are open bus, often last PPU write or data buffer
#define PPUSTATUS_SPRITE_OVERFLOW  0x20 // Sprite overflow. More than 8 sprites on scanline.
#define PPUSTATUS_SPRITE_0_HIT     0x40 // Sprite 0 Hit.
#define PPUSTATUS_VBLANK           0x80 // Vertical blank has started.

// Mirroring types for the PPU Nametables
typedef enum {
    MIRROR_HORIZONTAL,
    MIRROR_VERTICAL,
    MIRROR_SINGLE_SCREEN_LOW,  // Single screen, uses first 1KB of VRAM
    MIRROR_SINGLE_SCREEN_HIGH, // Single screen, uses second 1KB of VRAM
    MIRROR_FOUR_SCREEN         // Requires 4KB VRAM on cartridge
} MirrorMode;

// Structure to hold sprite data for rendering on the current scanline (after evaluation)
typedef struct {
    uint8_t x_pos;        // Current X position of the sprite
    uint8_t attributes;   // Raw attribute byte (palette, priority, flips)
    uint8_t pattern_low;  // Pattern data for the current row (low bits)
    uint8_t pattern_high; // Pattern data for the current row (high bits)
    uint8_t original_oam_index;
    // Note: Original Y from OAM isn't stored here as row_in_sprite is calculated during fetch
} SpriteShifter;

// PPU State Structure
typedef struct PPU {
    NES *nes; // Pointer to the main NES structure for bus access, callbacks, etc.

    // Memory
    uint8_t vram[PPU_VRAM_SIZE];           // Nametable RAM (2KB for 2 nametables)
    uint8_t palette[PPU_PALETTE_RAM_SIZE]; // Palette RAM (32 bytes) - Renamed from 'palette' to avoid conflict with nes_palette array
    uint8_t oam[PPU_OAM_SIZE];             // Primary OAM (Object Attribute Memory - 256 bytes)
    uint8_t secondary_oam[PPU_SECONDARY_OAM_SIZE]; // Secondary OAM (for sprites on current scanline - 32 bytes)

    // Registers
    uint8_t ctrl;        // $2000 PPUCTRL
    uint8_t mask;        // $2001 PPUMASK
    uint8_t status;      // $2002 PPUSTATUS
    uint8_t oam_addr;    // $2003 OAMADDR - OAM address for $2004 access

    // VRAM Address/Scroll Registers State (Loopy's t and v, fine_x)
    uint16_t vram_addr;  // Current VRAM address (v - 15 bits relevant for PPU bus, holds fine Y)
    uint16_t temp_addr;  // Temporary VRAM address (t - used by $2005/$2006, holds fine Y)
    uint8_t  fine_x;     // Fine X scroll (3 bits - used by pixel rendering)
    uint8_t  addr_latch; // Write toggle for $2005 (PPUSCROLL) & $2006 (PPUADDR) (0: first write, 1: second write)
    uint8_t  data_buffer;// Read buffer for PPUDATA ($2007)
    uint8_t  open_bus;   // Last value on the PPU data bus
    uint64_t open_bus_last_update_ms; // Monotonic timestamp used to decay open bus bits

    // Timing and Frame State
    int   scanline;      // Current scanline being processed (-1/261 for pre-render, 0-239 visible, 240 post, 241-260 VBlank)
    int   cycle;         // Current PPU clock cycle on the scanline (0-340)
    bool  frame_odd;     // True if the current frame is odd (for cycle skip on pre-render line)
    uint64_t frame_count; // Counts completed PPU frames

    // NMI (Non-Maskable Interrupt) State
    bool nmi_occured;         // Flag: VBlank period has started (set at SL241, C1; cleared at PreRender SL, C1)
    bool nmi_output;          // Flag: NMI generation enabled via PPUCTRL bit 7
    bool nmi_interrupt_line;  // Actual state of the NMI line to the CPU (true if NMI should be asserted)
    bool previous_nmi_output; // Previous NMI condition used to edge-trigger assertions
    bool suppress_vblank_start; // Set when $2002 is read on the VBlank-set cycle

    // Background Rendering Pipeline State (Latches and Shifters)
    uint8_t  bg_nt_latch;            // Nametable byte (tile index) latched for current/next tile
    uint8_t  bg_at_latch_low;        // Attribute palette bits (low bit, expanded to 8 pixels) latched
    uint8_t  bg_at_latch_high;       // Attribute palette bits (high bit, expanded to 8 pixels) latched
    uint8_t  bg_pt_low_latch;        // Pattern table low byte latched
    uint8_t  bg_pt_high_latch;       // Pattern table high byte latched

    uint16_t bg_pattern_shift_low;   // 16-bit shifter for low plane of 2 background tiles
    uint16_t bg_pattern_shift_high;  // 16-bit shifter for high plane of 2 background tiles
    uint16_t bg_attrib_shift_low;    // 16-bit shifter for attribute low bits (repeated for 2 tiles)
    uint16_t bg_attrib_shift_high;   // 16-bit shifter for attribute high bits (repeated for 2 tiles)

    // Sprite Rendering State
    uint8_t       sprite_count_current_scanline; // Number of sprites found for the current scanline (in secondary_oam)
    bool          sprite_zero_on_current_scanline; // True if sprite 0 is among those in secondary_oam
    SpriteShifter sprite_shifters[8]; // Holds pattern data and attributes for up to 8 sprites on the current scanline

    uint8_t secondary_oam_original_indices[8];
    bool sprite_zero_found_for_next_scanline;

    // Cartridge and System Configuration
    MirrorMode mirror_mode; // Nametable mirroring mode set by cartridge
    
    uint32_t active_palette[64]; // Active 64-color palette derived from the base NES palette and PPUMASK emphasis bits

    // Output
    uint32_t *framebuffer;
    uint8_t  *indexed_framebuffer;
    uint32_t *internal_framebuffer;
    uint8_t  *internal_indexed_framebuffer;
} PPU;

// --- PPU Lifecycle Functions ---
PPU* PPU_Create(NES *nes);
void PPU_Destroy(PPU *ppu);
void PPU_Reset(PPU *ppu);

// --- PPU Execution Function ---
void PPU_Step(PPU *ppu); // Advances PPU by one clock cycle
void PPU_CatchUp(PPU *ppu); // Advances PPU to catch up with CPU cycles

// --- PPU Register Access Functions (CPU interface) ---
uint8_t PPU_ReadRegister(PPU *ppu, uint16_t addr);
void PPU_WriteRegister(PPU *ppu, uint16_t addr, uint8_t value);
void PPU_DoOAMDMA(PPU *ppu, const uint8_t *dma_page_data); // Handles $4014 OAM DMA transfer

// --- PPU Open Bus Functions (CPU bus interface) ---
// These functions manage the PPU's open bus value, which decays over time
void PPU_DriveOpenBus(PPU *ppu, uint8_t value); // Update PPU open bus with a new value from CPU
uint8_t PPU_GetOpenBusWithDecay(PPU *ppu); // Get PPU open bus value with decay applied

// --- PPU Bus Access Functions (Mapper interface for CHR) ---
// These are typically called by the bus module, which in turn calls mapper functions.
// Not usually called directly from CPU emulation.
uint8_t PPU_CHR_Read(PPU *ppu, uint16_t addr);  // Placeholder, actual read via BUS_PPU_ReadCHR
void PPU_CHR_Write(PPU *ppu, uint16_t addr, uint8_t value); // Placeholder, actual write via BUS_PPU_WriteCHR

// --- PPU Configuration ---
void PPU_SetMirroring(PPU *ppu, MirrorMode mode);
void PPU_TriggerNMI(PPU *ppu);

// --- UI and Debugging Helper Functions ---
void PPU_SetOutputBuffers(PPU *ppu, uint32_t *framebuffer, uint8_t *indexed_framebuffer);
const uint32_t* PPU_GetFramebuffer(PPU* ppu);
const uint8_t* PPU_GetIndexedFramebuffer(PPU* ppu);
const uint32_t* PPU_GetPalette(PPU *ppu); // Returns pointer to the static master NES palette
const uint32_t* PPU_SetPalette(PPU *ppu, const uint32_t *palette); // Sets a custom palette for rendering (e.g. for palette hacks) and returns the new palette pointer
const uint32_t* PPU_GetPaletteRAM(PPU* ppu); // Returns pointer to PPU's internal palette RAM
const uint8_t* PPU_GetOAM(PPU* ppu);
const uint8_t* PPU_GetNametable(PPU* ppu, int index); // Gets a pointer to a nametable based on mirroring
void PPU_GetPatternTableData(PPU* ppu, int table_idx, uint8_t* buffer_128x128_indexed); // Renders a pattern table for viewing
void PPU_GetScanlineCycle(PPU* ppu, int* scanline, int* cycle);

void PPU_DumpNametable(PPU* ppu, int index, char* out_buffer, size_t buffer_size);
void PPU_DumpPaletteRAM(PPU* ppu, char* out_buffer, size_t buffer_size);
void PPU_DumpOAM(PPU* ppu, char* out_buffer, size_t buffer_size);

#endif // PPU_H