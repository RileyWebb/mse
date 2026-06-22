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

typedef struct NES_Settings {
    enum {
        NES_CPU_MODE_JIT,
        NES_CPU_MODE_INTERPRETER,
        NES_CPU_MODE_DEBUG,
    } cpu_mode; // CPU emulation setting

    enum {
        NES_PPU_MODE_JIT,
        NES_PPU_MODE_INTERPRETER,
        NES_PPU_MODE_ACCELERATED,
        NES_PPU_MODE_DEBUG,
    } ppu_mode; // PPU emulation setting

    NES_Region region;

    struct {
        int scanlines_visible;     // usually 240
        int scanline_vblank;       // usually 241
        int scanline_prerender;    // NTSC: 261, PAL: 311, Dendy: 311
        int cycles_per_scanline;   // 341
        float cpu_clock_rate;      // NTSC: 1.789773 MHz, PAL: 1.662607 MHz
    } timing;

    struct {
        uint32_t palette[64]; // 64 colors, 4 bytes each (ABGR)
        float saturation;          // example factors for UI to control if needed
        float hue;
    } video;

    struct {
        int sample_rate;
        float volume;
    } audio;

    struct {
        int turbo_rate; // 0 = Disabled, 1 = Slow, 2 = Medium, 3 = Fast
        float actuation_threshold; // For analog input, if implemented in the future
    } input;

    //temp
    float frame_time; // Target frame time in milliseconds (e.g., 16.67ms for 60Hz)

    bool break_on_illegal; // CPU setting: break on illegal instructions
    bool enable_catch_up; // Enable Catch-up rendering (lazy evaluation)
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

NES *NES_Create();
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

#endif // NES_H
