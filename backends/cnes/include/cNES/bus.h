#ifndef BUS_H
#define BUS_H

#include <stdint.h>
#include <stddef.h>

#include "cNES/mapper.h"

typedef struct NES NES;
typedef struct PPU PPU;
typedef struct APU APU;

#define BUS_RAM_SIZE     0x0800 // 2KB of internal work RAM, mirrored to $1FFF
#define BUS_PRG_RAM_SIZE 0x2000 // 8KB cartridge RAM window at $6000-$7FFF

// How many cheats can be live at once. The index below stores slot+1 in a
// byte, so 255 is the ceiling whatever this says.
#define BUS_CHEAT_MAX 64

// What a read returns once a cheat claims the address.
//
// `compare` is the Game Genie's eight-letter form: substitute only when the
// cartridge returns this byte. It is what lets one code apply to a single
// bank of a banked ROM.
typedef struct BUS_CheatPatch {
    uint8_t value;
    uint8_t compare;
    uint8_t has_compare;
    uint8_t _pad;
} BUS_CheatPatch;

typedef struct BUS {
    // Internal work RAM. This was a 64KB array indexed `& 0x07FF`, which both
    // wasted 62KB per instance and invited the next reader to treat it as a flat
    // address map -- it never was one; everything outside $0000-$1FFF is routed
    // to the PPU, APU or mapper.
    uint8_t ram[BUS_RAM_SIZE];

    // Cartridge RAM at $6000-$7FFF. Used to live inside the MMC1 state struct,
    // so no other mapper could have save RAM at all.
    uint8_t prgRam[BUS_PRG_RAM_SIZE];
    bool    prgRamPresent;   // Cartridge wires up $6000-$7FFF
    bool    prgRamBattery;   // ...and it is battery backed, so worth persisting
    bool    prgRamDirty;     // Written since the last time the host saved it

    uint8_t chrRam[0x2000];  // 8KB CHR RAM fallback
    PPU *ppu;                // Owning PPU for runtime mirroring updates
    uint8_t *prgRomData;     // Full PRG ROM data from the cartridge
    uint8_t *chrRomData;     // Full CHR ROM data from the cartridge
    size_t prgRomDataSize;   // PRG ROM size in bytes
    size_t chrRomDataSize;   // CHR ROM size in bytes
    uint16_t mapper;         // Mapper number (NES 2.0 allows up to 4095)
    const NES_MapperInfo *mapper_info; // Resolved once at load; never NULL once the bus is live
    uint8_t mirroring;       // Mirroring type
    uint8_t prgRomSize;      // PRG ROM size in 16KB units
    uint8_t chrRomSize;      // CHR ROM size in 8KB units
    uint8_t prgBankSelect;   // Current mapper-selected PRG bank
    uint8_t chrBankSelect;   // Current mapper-selected CHR bank

    // Asserted by mappers with their own IRQ source (MMC3 and friends). The CPU
    // samples this alongside the APU's IRQ lines.
    bool irq_asserted;

    // === Cheats ===
    //
    // A Game Genie sat between the cartridge and the console and answered
    // reads, so that is where cheats are applied: on the way out of a read,
    // not by rewriting memory. That also makes a banked address behave
    // correctly without guessing which bank a patch belongs in.
    //
    // cheat_index is NULL until the first cheat exists, which is what keeps
    // this off the hot path: one pointer test per read, predicted not-taken
    // for every session where nobody is cheating. When it is allocated it is
    // 64KB indexed by CPU address, holding slot+1 into cheat_patches.
    uint8_t         *cheat_index;
    BUS_CheatPatch   cheat_patches[BUS_CHEAT_MAX];

    void *mapper_data;
} BUS;

// IO functions
// OPTIMIZATION: BUS_Read inlined for hot path (called millions of times per frame)
static inline uint8_t BUS_Read(NES* nes, uint16_t address);
static inline uint16_t BUS_Read16(NES* nes, uint16_t address);
void BUS_Write(NES* nes, uint16_t address, uint8_t value);
void BUS_Write16(NES* nes, uint16_t address, uint16_t value);

// PPU bus mapping for CHR ROM/RAM.
// There is deliberately no BUS-side nametable or palette accessor: the PPU owns
// that memory (ppu->vram via nametable_ptrs, and ppu->palette). The BUS used to
// carry a second, unreachable copy of both.
uint8_t BUS_PPU_ReadCHR(struct BUS* bus, uint16_t address);
void BUS_PPU_WriteCHR(struct BUS* bus, uint16_t address, uint8_t value);

// Side-effect-free read, for debuggers and tools.
uint8_t BUS_Peek(NES* nes, uint16_t address);

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

// A plain accessor. It used to wind cpu->total_cycles forward, run a DMC DMA and
// wind it back; that scheduling now lives on the CPU's cycle boundary where it
// belongs.
static inline uint8_t BUS_GetOpenBus(const NES *nes)
{
    return nes->cpu_open_bus;
}

// Inlined: this runs several million times a second.
//
// The tests are ordered by how often each region is actually read, not by
// address. Every opcode and operand fetch comes from cartridge ROM, which makes
// $8000+ far and away the most common case, and internal RAM (zero page, stack,
// variables) a clear second. Those two now cost one and two compares; they used
// to sit at the end of a six-compare ladder behind four single-address checks
// for registers that are read comparatively rarely.
// Substitutes a cheat's byte for the one the hardware returned.
//
// Only reached when at least one cheat exists; the caller tests cheat_index
// first so that the no-cheats case costs a single predictable branch.
static inline uint8_t BUS_ApplyCheat(const BUS *bus, uint16_t address, uint8_t value) {
    const uint8_t slot = bus->cheat_index[address];
    if (slot == 0) {
        return value;
    }

    const BUS_CheatPatch *patch = &bus->cheat_patches[slot - 1];
    if (patch->has_compare && patch->compare != value) {
        return value;
    }
    return patch->value;
}

static inline uint8_t BUS_Read(NES* nes, uint16_t address) {
    // Cartridge ROM: every instruction fetch lands here.
    if (address >= 0x8000) {
        uint8_t value = nes->bus->mapper_info->cpu_read(nes->bus, address);
        if (nes->bus->cheat_index != NULL) {
            value = BUS_ApplyCheat(nes->bus, address, value);
        }
        BUS_DriveOpenBus(nes, value);
        return value;
    }
    // Internal RAM: zero page, stack, and most data access.
    if (address < 0x2000) {
        uint8_t value = nes->bus->ram[address & (BUS_RAM_SIZE - 1)];
        if (nes->bus->cheat_index != NULL) {
            value = BUS_ApplyCheat(nes->bus, address, value);
        }
        BUS_DriveOpenBus(nes, value);
        return value;
    }
    // PPU registers: frequent, but an order of magnitude less so than the above.
    if (address < 0x4000) {
        uint8_t value = PPU_ReadRegister(nes->ppu, 0x2000 + (address & 0x0007));
        BUS_DriveOpenBus(nes, value);
        return value;
    }

    // Everything from here is $4000-$7FFF and comparatively rare.

    // Cartridge RAM at $6000-$7FFF.
    if (address >= 0x6000) {
        uint8_t value = nes->bus->mapper_info->cpu_read(nes->bus, address);
        if (nes->bus->cheat_index != NULL) {
            value = BUS_ApplyCheat(nes->bus, address, value);
        }
        BUS_DriveOpenBus(nes, value);
        return value;
    }
    // Controller ports. The shift-register behaviour itself lives in nes.h so
    // that BUS_Peek shares it instead of keeping its own divergent copy.
    if (address == 0x4016 || address == 0x4017) {
        uint8_t result = NES_ControllerRead(nes, address & 1);
        result |= (uint8_t)(nes->cpu_open_bus & 0xFE);
        BUS_DriveOpenBus(nes, result);
        return result;
    }
    if (address == 0x4015) {
        uint8_t value = APU_ReadRegister(nes->apu, address);
        BUS_DriveOpenBus(nes, value);
        return value;
    }
    // $4000-$4013 are write-only, $4014 is OAM DMA, $4018-$5FFF is unmapped:
    // all read back as open bus, and none of them drive it.
    return BUS_GetOpenBus(nes);
}

// OPTIMIZATION: Inline BUS_Read16 for address fetching
static inline uint16_t BUS_Read16(NES* nes, uint16_t address) {
    uint8_t lo = BUS_Read(nes, address);
    uint8_t hi = BUS_Read(nes, address + 1);
    return lo | (((uint16_t)hi) << 8);
}

#endif // BUS_H