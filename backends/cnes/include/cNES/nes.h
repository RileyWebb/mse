#ifndef NES_H
#define NES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct CPU CPU;
typedef struct APU APU;
typedef struct PPU PPU;
typedef struct BUS BUS;
typedef struct ROM ROM;
typedef struct TAS TAS;

typedef enum {
    NES_REGION_NTSC,    // Default 60Hz 
    NES_REGION_PAL,     // 50Hz mode
    NES_REGION_DENDY,   // 50Hz Dendy
    NES_REGION_CUSTOM   // Custom timing overrides
} NES_Region;

// Only settings the core actually reads live here. It used to also carry
// cpu_mode, ppu_mode (with JIT and ACCELERATED options), break_on_illegal,
// turbo_rate, and video saturation/hue -- none of which anything consulted.
typedef struct NES_Settings {
    NES_Region region;

    struct {
        int scanlines_visible;     // usually 240
        int scanline_vblank;       // usually 241
        int scanline_prerender;    // NTSC: 261, PAL: 311, Dendy: 311
        int cycles_per_scanline;   // 341
        float cpu_clock_rate;      // NTSC: 1.789773 MHz, PAL: 1.662607 MHz
    } timing;

    struct {
        uint32_t palette[64]; // 64 colors, 0xAABBGGRR (R,G,B,A in memory)
    } video;

    struct {
        int sample_rate;
        float volume;
    } audio;

    struct {
        float actuation_threshold; // For analog input, if implemented in the future
    } input;

    float frame_time; // Target frame time in milliseconds (e.g., 16.67ms for 60Hz)
} NES_Settings;

typedef struct NES {
    CPU* cpu; // Pointer to the CPU
    APU* apu; // Pointer to the APU
    PPU* ppu; // Pointer to the PPU
    BUS* bus; // Pointer to the BUS
    ROM* rom; // Pointer to the ROM
    TAS* tas; // Pointer to the TAS player (if any)

    uint8_t controllers[2]; // Two NES controllers
    uint8_t controller_strobe; // Strobe flag for controllers
    uint8_t controller_shift[2]; // Shift registers for controllers
    uint8_t cpu_open_bus; // Last value on the CPU data bus

    NES_Settings settings; // Emulator settings, region logic, variable clocks
} NES;

NES *NES_Create(void);
int NES_Load(NES* nes, ROM* rom);
void NES_Destroy(NES* nes);

void NES_SetRegionPreset(NES *nes, NES_Region region);
void NES_LoadPaletteRGBA(NES *nes, const uint32_t* rgba_palette);

void NES_StepFrame(NES *nes);
void NES_Step(NES *nes);
void NES_Reset(NES *nes);
uint64_t NES_GetFrameCount(NES *nes);

// Poll controller state (UI or platform layer should implement this and NES core should call it)
uint8_t NES_PollController(NES* nes, int controller);

// Set controller state (for UI or platform layer to update controller state in NES struct)
void NES_SetController(NES* nes, int controller, uint8_t state);

// Cartridge RAM, for hosts that want to persist battery-backed saves. The core
// deliberately does no file I/O of its own -- it does not get to pick where a
// save lives. Returns NULL when the cartridge has no RAM at $6000-$7FFF.
uint8_t *NES_GetCartridgeRam(NES *nes, size_t *size_out);
bool NES_CartridgeRamIsBatteryBacked(const NES *nes);
bool NES_CartridgeRamIsDirty(const NES *nes);
void NES_ClearCartridgeRamDirty(NES *nes);

// --- Controller shift registers ---
// Shared by the live bus read and by BUS_Peek so the two cannot disagree; the
// peek path used to reimplement this and dropped the open-bus bits.
static inline uint8_t NES_ControllerPeek(const NES *nes, int port)
{
    if (nes->controller_strobe) {
        return nes->controllers[port] & 0x01;
    }
    return nes->controller_shift[port] & 0x01;
}

static inline uint8_t NES_ControllerRead(NES *nes, int port)
{
    if (nes->controller_strobe) {
        return nes->controllers[port] & 0x01;
    }

    uint8_t bit = nes->controller_shift[port] & 0x01;
    // Shift ones in behind the button data so that the ninth and later reads
    // return 1, the way the hardware's open shift register does.
    nes->controller_shift[port] = (uint8_t)((nes->controller_shift[port] >> 1) | 0x80);
    return bit;
}

#endif // NES_H
