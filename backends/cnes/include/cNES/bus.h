#ifndef BUS_H
#define BUS_H

#include <stdint.h>
#include <stddef.h>

#include "cNES/mapper.h"

typedef struct NES NES;
typedef struct PPU PPU;
typedef struct APU APU;

typedef struct BUS {
    uint8_t memory[0x10000]; // 64KB address map
    uint8_t chrRam[0x2000];  // 8KB CHR RAM fallback
    PPU *ppu;                // Owning PPU for runtime mirroring updates
    uint8_t *prgRomData;     // Full PRG ROM data from the cartridge
    uint8_t *chrRomData;     // Full CHR ROM data from the cartridge
    size_t prgRomDataSize;   // PRG ROM size in bytes
    size_t chrRomDataSize;   // CHR ROM size in bytes
    uint8_t vram[0x1000];    // 4KB VRAM for nametables (mirrored)
    uint8_t palette[0x20];   // 32 bytes palette RAM
    uint8_t mapper;          // Mapper type
    uint8_t mirroring;       // Mirroring type
    uint8_t prgRomSize;      // PRG ROM size in 16KB units
    uint8_t chrRomSize;      // CHR ROM size in 8KB units
    uint8_t prgBankSelect;   // Current mapper-selected PRG bank
    uint8_t chrBankSelect;   // Current mapper-selected CHR bank

    void *mapper_data;
} BUS;

// IO functions
// OPTIMIZATION: BUS_Read inlined for hot path (called millions of times per frame)
static inline uint8_t BUS_Read(NES* nes, uint16_t address);
static inline uint16_t BUS_Read16(NES* nes, uint16_t address);
void BUS_Write(NES* nes, uint16_t address, uint8_t value);
void BUS_Write16(NES* nes, uint16_t address, uint16_t value);

// PPU bus mapping for CHR ROM/RAM
uint8_t BUS_PPU_ReadCHR(struct BUS* bus, uint16_t address);
void BUS_PPU_WriteCHR(struct BUS* bus, uint16_t address, uint8_t value);

// PPU bus mapping for PPU address space
uint8_t BUS_PPU_Read(struct BUS* bus, uint16_t address);
void BUS_PPU_Write(struct BUS* bus, uint16_t address, uint8_t value);

// Peak methods (no PC increment)
uint8_t BUS_Peek(NES* nes, uint16_t address);
uint16_t BUS_Peek16(NES* nes, uint16_t address);

// === OPTIMIZED INLINE IMPLEMENTATIONS (for hot paths) ===
// These are inlined here to avoid function call overhead in CPU emulation
#include "cNES/nes.h"  // For full struct definitions needed by inline functions

// Forward declarations needed for inline implementation
struct PPU;
struct APU;
uint8_t PPU_ReadRegister(struct PPU* ppu, uint16_t addr);
uint8_t PPU_GetOpenBusWithDecay(struct PPU* ppu);
uint8_t APU_ReadRegister(struct APU* apu, uint16_t addr);

static inline void BUS_DriveOpenBus(NES *nes, uint8_t value)
{
    nes->cpu_open_bus = value;
}

static inline uint8_t BUS_GetOpenBus(NES *nes)
{
    return nes->cpu_open_bus;
}

// OPTIMIZATION: Inline BUS_Read for ~5.4 million CPU calls per second
static inline uint8_t BUS_Read(NES* nes, uint16_t address) {
    // RAM path (common for zero-page, stack, etc)
    if (address < 0x2000) {
        uint8_t value = nes->bus->memory[address & 0x07FF];
        BUS_DriveOpenBus(nes, value);
        return value;
    }
    // PPU register read (less common during CPU execution, but still frequent)
    if (address < 0x4000) {
        uint8_t value = PPU_ReadRegister(nes->ppu, 0x2000 + (address & 0x0007));
        BUS_DriveOpenBus(nes, value);
        return value;
    }
    // OAM DMA register read - return PPU open bus (last driven value)
    if (address == 0x4014) {
        uint8_t value = PPU_GetOpenBusWithDecay(nes->ppu);
        BUS_DriveOpenBus(nes, value);
        return value;
    }
    if (address == 0x4015) {
        uint8_t value = APU_ReadRegister(nes->apu, address);
        BUS_DriveOpenBus(nes, value);
        return value;
    }
    // Controller read
    if (address == 0x4016) {
        uint8_t result;
        if (nes->controller_strobe) {
            result = nes->controllers[0] & 0x01;
        } else {
            result = nes->controller_shift[0] & 0x01;
            nes->controller_shift[0] >>= 1;
        }
        result |= (uint8_t)(nes->cpu_open_bus & 0xFE);
        BUS_DriveOpenBus(nes, result);
        return result;
    }
    if (address == 0x4017) {
        uint8_t result;
        if (nes->controller_strobe) {
            result = nes->controllers[1] & 0x01;
        } else {
            result = nes->controller_shift[1] & 0x01;
            nes->controller_shift[1] >>= 1;
        }
        result |= (uint8_t)(nes->cpu_open_bus & 0xFE);
        BUS_DriveOpenBus(nes, result);
        return result;
    }
    if (address >= 0x6000) {
        NES_MapperInfo mapper = NES_Mapper_Get(nes->bus->mapper);
        uint8_t value = mapper.cpu_read(nes->bus, address);
        BUS_DriveOpenBus(nes, value);
        return value;
    }
    // Other mapped areas return 0 (APU, unmapped, etc)
    return BUS_GetOpenBus(nes);
}

// OPTIMIZATION: Inline BUS_Read16 for address fetching
static inline uint16_t BUS_Read16(NES* nes, uint16_t address) {
    uint8_t lo = BUS_Read(nes, address);
    uint8_t hi = BUS_Read(nes, address + 1);
    return lo | (((uint16_t)hi) << 8);
}

#endif // BUS_H