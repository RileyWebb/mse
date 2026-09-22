#include "cNES/mapper.h"

#include <stdlib.h>
#include <string.h>

#include "libmse/libmse_debug.h"

#include "cNES/bus.h"
#include "cNES/ppu.h"

// === Shared helpers ===

static size_t Mapper_GetPRGBankCount(const BUS *bus)
{
    size_t prg_banks = bus->prgRomDataSize / 0x4000;
    return prg_banks == 0 ? 1 : prg_banks;
}

static size_t Mapper_GetCHRBankCount(const BUS *bus)
{
    if (bus->chrRomDataSize == 0) {
        return 0;
    }

    size_t chr_banks = bus->chrRomDataSize / 0x2000;
    return chr_banks == 0 ? 1 : chr_banks;
}

static uint8_t Mapper_ReadPRG16(const BUS *bus, size_t bank, uint16_t offset)
{
    if (!bus->prgRomData || bus->prgRomDataSize == 0) {
        return 0;
    }

    size_t prg_banks = Mapper_GetPRGBankCount(bus);
    bank %= prg_banks;

    size_t base = bank * 0x4000u;
    size_t index = base + (offset & 0x3FFFu);
    if (index >= bus->prgRomDataSize) {
        index %= bus->prgRomDataSize;
    }

    return bus->prgRomData[index];
}

static uint8_t Mapper_ReadPRG32(const BUS *bus, size_t bank, uint16_t offset)
{
    if (!bus->prgRomData || bus->prgRomDataSize == 0) {
        return 0;
    }

    size_t prg_banks = bus->prgRomDataSize / 0x8000u;
    if (prg_banks == 0) {
        prg_banks = 1;
    }
    bank %= prg_banks;

    size_t base = bank * 0x8000u;
    size_t index = base + (offset & 0x7FFFu);
    if (index >= bus->prgRomDataSize) {
        index %= bus->prgRomDataSize;
    }

    return bus->prgRomData[index];
}

static uint8_t Mapper_ReadCHR8(const BUS *bus, size_t bank, uint16_t offset)
{
    if (!bus->chrRomData || bus->chrRomDataSize == 0) {
        return bus->chrRam[offset & 0x1FFFu];
    }

    size_t chr_banks = Mapper_GetCHRBankCount(bus);
    bank %= chr_banks;

    size_t base = bank * 0x2000u;
    size_t index = base + (offset & 0x1FFFu);
    if (index >= bus->chrRomDataSize) {
        index %= bus->chrRomDataSize;
    }

    return bus->chrRomData[index];
}

// Cartridge RAM at $6000-$7FFF, available to every mapper. This used to be an
// array buried inside the MMC1 state struct, so an NROM or UNROM cartridge with
// save RAM had nowhere to put it and read back zeroes.
static inline uint8_t Mapper_ReadPRGRam(const BUS *bus, uint16_t address)
{
    if (!bus->prgRamPresent) {
        return 0;
    }
    return bus->prgRam[(address - 0x6000u) & (BUS_PRG_RAM_SIZE - 1u)];
}

static inline void Mapper_WritePRGRam(BUS *bus, uint16_t address, uint8_t value)
{
    if (!bus->prgRamPresent) {
        return;
    }
    bus->prgRam[(address - 0x6000u) & (BUS_PRG_RAM_SIZE - 1u)] = value;
    bus->prgRamDirty = true;
}

// === Default handlers (also the fallback for unimplemented mappers) ===

static uint8_t Mapper_DefaultCPURead(BUS *bus, uint16_t address)
{
    if (address >= 0x6000 && address < 0x8000) {
        return Mapper_ReadPRGRam(bus, address);
    }

    if (address >= 0x8000) {
        if (bus->prgRomDataSize == 0 || !bus->prgRomData) {
            return 0;
        }
        return bus->prgRomData[(address - 0x8000u) % bus->prgRomDataSize];
    }

    return 0;
}

static void Mapper_DefaultCPUWrite(BUS *bus, uint16_t address, uint8_t value)
{
    if (address >= 0x6000 && address < 0x8000) {
        Mapper_WritePRGRam(bus, address, value);
    }
    // Writes to ROM are ignored.
}

static uint8_t Mapper_DefaultPPURead(BUS *bus, uint16_t address)
{
    if (bus->chrRomDataSize > 0 && bus->chrRomData) {
        return Mapper_ReadCHR8(bus, 0, address);
    }

    return bus->chrRam[address & 0x1FFFu];
}

static void Mapper_DefaultPPUWrite(BUS *bus, uint16_t address, uint8_t value)
{
    if (bus->chrRomDataSize == 0) {
        bus->chrRam[address & 0x1FFF] = value;
    }
}

// === Mapper 0: NROM ===

static uint8_t Mapper0_CPURead(BUS *bus, uint16_t address)
{
    if (address >= 0x6000 && address < 0x8000) {
        return Mapper_ReadPRGRam(bus, address);
    }

    if (address >= 0x8000) {
        size_t prg_banks = Mapper_GetPRGBankCount(bus);
        uint16_t offset = address - 0x8000;

        // Mirror 16KB PRG ROM if we only have 1 bank
        if (prg_banks == 1) {
            offset &= 0x3FFF;
        } else {
            offset &= 0x7FFF;
        }

        if (bus->prgRomData && bus->prgRomDataSize > 0) {
            return bus->prgRomData[offset % bus->prgRomDataSize];
        }
    }
    return 0;
}

static void Mapper0_CPUWrite(BUS *bus, uint16_t address, uint8_t value)
{
    // NROM has no bank switching registers, but the board may still carry RAM.
    if (address >= 0x6000 && address < 0x8000) {
        Mapper_WritePRGRam(bus, address, value);
    }
}

static uint8_t Mapper0_PPURead(BUS *bus, uint16_t address)
{
    if (bus->chrRomDataSize > 0 && bus->chrRomData) {
        return bus->chrRomData[address & 0x1FFF];
    }
    return bus->chrRam[address & 0x1FFF];
}

static void Mapper0_PPUWrite(BUS *bus, uint16_t address, uint8_t value)
{
    if (bus->chrRomDataSize == 0) {
        bus->chrRam[address & 0x1FFF] = value;
    }
}

// === Mapper 1: MMC1 ===

typedef struct {
    uint8_t shift_register;
    uint8_t control;
    uint8_t chr_bank_0;
    uint8_t chr_bank_1;
    uint8_t prg_bank;
} Mapper1_State;

static bool Mapper1_Init(BUS *bus)
{
    bus->mapper_data = calloc(1, sizeof(Mapper1_State));
    if (!bus->mapper_data) {
        DEBUG_ERROR("Failed to allocate MMC1 mapper state");
        return false;
    }
    return true;
}

static void Mapper1_Reset(BUS *bus)
{
    Mapper1_State *state = (Mapper1_State *)bus->mapper_data;
    if (!state) {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->shift_register = 0x10;
    state->control        = 0x0C;
}

static void Mapper_FreeState(BUS *bus)
{
    free(bus->mapper_data);
    bus->mapper_data = NULL;
}

static uint8_t Mapper1_CPURead(BUS *bus, uint16_t address)
{
    Mapper1_State *mmc1_state = (Mapper1_State *)bus->mapper_data;

    if (address >= 0x6000 && address < 0x8000) {
        return Mapper_ReadPRGRam(bus, address);
    }

    if (address >= 0x8000) {
        uint8_t prg_mode = (mmc1_state->control >> 2) & 0x03;
        size_t prg_banks = Mapper_GetPRGBankCount(bus);
        uint16_t offset;
        size_t bank;

        if (prg_mode <= 1) {
            // 32KB PRG mode
            bank = (mmc1_state->prg_bank & 0x0E) >> 1;
            offset = address - 0x8000;
            return Mapper_ReadPRG32(bus, bank, offset);
        } else if (prg_mode == 2) {
            // 16KB PRG mode: Fix first bank at $8000, switch 16KB bank at $C000
            if (address < 0xC000) {
                offset = address - 0x8000;
                return Mapper_ReadPRG16(bus, 0, offset);
            } else {
                bank = mmc1_state->prg_bank & 0x0F;
                offset = address - 0xC000;
                return Mapper_ReadPRG16(bus, bank, offset);
            }
        } else {
            // 16KB PRG mode: Switch 16KB bank at $8000, fix last bank at $C000
            if (address < 0xC000) {
                bank = mmc1_state->prg_bank & 0x0F;
                offset = address - 0x8000;
                return Mapper_ReadPRG16(bus, bank, offset);
            } else {
                bank = prg_banks > 0 ? prg_banks - 1 : 0;
                offset = address - 0xC000;
                return Mapper_ReadPRG16(bus, bank, offset);
            }
        }
    }
    return 0;
}

static void Mapper1_CPUWrite(BUS *bus, uint16_t address, uint8_t value)
{
    Mapper1_State *mmc1_state = (Mapper1_State *)bus->mapper_data;

    if (address >= 0x6000 && address < 0x8000) {
        Mapper_WritePRGRam(bus, address, value);
        return;
    }

    if (address >= 0x8000) {
        if (value & 0x80) {
            mmc1_state->shift_register = 0x10;
            mmc1_state->control |= 0x0C; // Reset PRG mode
        } else {
            bool complete = mmc1_state->shift_register & 0x01;
            mmc1_state->shift_register >>= 1;
            mmc1_state->shift_register |= ((value & 0x01) << 4);

            if (complete) {
                uint8_t data = mmc1_state->shift_register;
                uint16_t target = (address >> 13) & 0x03;

                switch (target) {
                    case 0: { // Control (0x8000 - 0x9FFF)
                        mmc1_state->control = data;
                        uint8_t mirror = data & 0x03;
                        if (bus->ppu) {
                            switch (mirror) {
                                case 0: PPU_SetMirroring(bus->ppu, MIRROR_SINGLE_SCREEN_LOW); break;
                                case 1: PPU_SetMirroring(bus->ppu, MIRROR_SINGLE_SCREEN_HIGH); break;
                                case 2: PPU_SetMirroring(bus->ppu, MIRROR_VERTICAL); break;
                                case 3: PPU_SetMirroring(bus->ppu, MIRROR_HORIZONTAL); break;
                            }
                        }
                        break;
                    }
                    case 1: // CHR Bank 0 (0xA000 - 0xBFFF)
                        mmc1_state->chr_bank_0 = data;
                        break;
                    case 2: // CHR Bank 1 (0xC000 - 0xDFFF)
                        mmc1_state->chr_bank_1 = data;
                        break;
                    case 3: // PRG Bank (0xE000 - 0xFFFF)
                        mmc1_state->prg_bank = data;
                        break;
                }
                mmc1_state->shift_register = 0x10;
            }
        }
    }
}

static uint8_t Mapper1_PPURead(BUS *bus, uint16_t address)
{
    if (address >= 0x2000) return 0;

    Mapper1_State *mmc1_state = (Mapper1_State *)bus->mapper_data;

    uint8_t chr_mode = (mmc1_state->control >> 4) & 0x01;

    if (chr_mode == 0) {
        // 8KB CHR mode
        size_t bank = (mmc1_state->chr_bank_0 & 0x1E) >> 1;
        return Mapper_ReadCHR8(bus, bank, address);
    } else {
        // 4KB CHR mode
        size_t bank;
        uint16_t offset = address & 0x0FFF;

        if (address < 0x1000) {
            bank = mmc1_state->chr_bank_0;
        } else {
            bank = mmc1_state->chr_bank_1;
        }

        if (bus->chrRomDataSize > 0 && bus->chrRomData) {
            size_t index = (bank * 0x1000u) + offset;
            return bus->chrRomData[index % bus->chrRomDataSize];
        } else {
            // For carts mapping RAM for CHR, bounded to typical 8KB
            size_t index = (bank * 0x1000u) + offset;
            return bus->chrRam[index & 0x1FFFu];
        }
    }
}

static void Mapper1_PPUWrite(BUS *bus, uint16_t address, uint8_t value)
{
    if (address >= 0x2000) return;

    Mapper1_State *mmc1_state = (Mapper1_State *)bus->mapper_data;

    if (bus->chrRomDataSize == 0) {
        uint8_t chr_mode = (mmc1_state->control >> 4) & 0x01;
        if (chr_mode == 0) {
            // 8KB CHR mode
            size_t bank = (mmc1_state->chr_bank_0 & 0x1E) >> 1;
            size_t index = (bank * 0x2000u) + (address & 0x1FFF);
            bus->chrRam[index & 0x1FFFu] = value;
        } else {
            // 4KB CHR mode
            size_t bank;
            uint16_t offset = address & 0x0FFF;
            if (address < 0x1000) {
                bank = mmc1_state->chr_bank_0;
            } else {
                bank = mmc1_state->chr_bank_1;
            }
            size_t index = (bank * 0x1000u) + offset;
            bus->chrRam[index & 0x1FFFu] = value;
        }
    }
}

// === Mapper 2: UNROM ===

static uint8_t Mapper2_CPURead(BUS *bus, uint16_t address)
{
    if (address >= 0x6000 && address < 0x8000) {
        return Mapper_ReadPRGRam(bus, address);
    }

    if (address < 0x8000) {
        return 0;
    }

    if (address < 0xC000) {
        uint16_t offset = (uint16_t)(address - 0x8000u);
        return Mapper_ReadPRG16(bus, bus->prgBankSelect, offset);
    }

    uint16_t offset = (uint16_t)(address - 0xC000u);
    return Mapper_ReadPRG16(bus, Mapper_GetPRGBankCount(bus) - 1u, offset);
}

static void Mapper2_CPUWrite(BUS *bus, uint16_t address, uint8_t value)
{
    if (address >= 0x6000 && address < 0x8000) {
        Mapper_WritePRGRam(bus, address, value);
        return;
    }

    if (address >= 0x8000) {
        size_t prg_banks = Mapper_GetPRGBankCount(bus);
        if (prg_banks > 0) {
            bus->prgBankSelect = (uint8_t)(value % prg_banks);
        }
    }
}

// === Mapper 3: CNROM ===

static uint8_t Mapper3_PPURead(BUS *bus, uint16_t address)
{
    if (bus->chrRomDataSize == 0 || !bus->chrRomData) {
        return bus->chrRam[address & 0x1FFFu];
    }

    return Mapper_ReadCHR8(bus, bus->chrBankSelect, address);
}

static void Mapper3_CPUWrite(BUS *bus, uint16_t address, uint8_t value)
{
    if (address >= 0x6000 && address < 0x8000) {
        Mapper_WritePRGRam(bus, address, value);
        return;
    }

    if (address >= 0x8000) {
        size_t chr_banks = Mapper_GetCHRBankCount(bus);
        if (chr_banks > 0) {
            bus->chrBankSelect = (uint8_t)(value % chr_banks);
        }
    }
}

// === Mapper 7: AOROM ===

static uint8_t Mapper7_CPURead(BUS *bus, uint16_t address)
{
    if (address >= 0x6000 && address < 0x8000) {
        return Mapper_ReadPRGRam(bus, address);
    }

    if (address < 0x8000) {
        return 0;
    }

    uint16_t offset = (uint16_t)(address - 0x8000u);
    return Mapper_ReadPRG32(bus, bus->prgBankSelect, offset);
}

static void Mapper7_CPUWrite(BUS *bus, uint16_t address, uint8_t value)
{
    if (address >= 0x6000 && address < 0x8000) {
        Mapper_WritePRGRam(bus, address, value);
        return;
    }

    if (address < 0x8000) {
        return;
    }

    size_t prg_banks = bus->prgRomDataSize / 0x8000u;
    if (prg_banks == 0) {
        prg_banks = 1;
    }

    bus->prgBankSelect = (uint8_t)(value & 0x07u);
    if (prg_banks > 0) {
        bus->prgBankSelect = (uint8_t)(bus->prgBankSelect % prg_banks);
    }

    if (bus->ppu) {
        PPU_SetMirroring(bus->ppu,
            (value & 0x10u) ? MIRROR_SINGLE_SCREEN_HIGH : MIRROR_SINGLE_SCREEN_LOW);
    }
}

// === Mapper 4: MMC3 ===
//
// This is the mapper the lifecycle and IRQ hooks above exist for. It was
// previously impossible to support at all: NES_MapperInfo had nowhere to hang an
// IRQ source, and nothing told a mapper when the PPU drove an address.

static size_t Mapper_GetPRG8kBankCount(const BUS *bus)
{
    size_t banks = bus->prgRomDataSize / 0x2000u;
    return banks == 0 ? 1 : banks;
}

static uint8_t Mapper_ReadPRG8(const BUS *bus, size_t bank, uint16_t address)
{
    if (!bus->prgRomData || bus->prgRomDataSize == 0) {
        return 0;
    }

    bank %= Mapper_GetPRG8kBankCount(bus);
    size_t index = (bank * 0x2000u) + (address & 0x1FFFu);
    return bus->prgRomData[index % bus->prgRomDataSize];
}

static uint8_t Mapper_ReadCHR1(const BUS *bus, size_t bank, uint16_t offset)
{
    offset &= 0x03FFu;

    if (bus->chrRomData && bus->chrRomDataSize > 0) {
        size_t banks = bus->chrRomDataSize / 0x400u;
        if (banks == 0) {
            banks = 1;
        }
        size_t index = ((bank % banks) * 0x400u) + offset;
        return bus->chrRomData[index % bus->chrRomDataSize];
    }

    return bus->chrRam[((bank * 0x400u) + offset) & 0x1FFFu];
}

typedef struct {
    uint8_t bank_select;     // $8000 even: CHR A12 inversion, PRG mode, register index
    uint8_t bank_regs[8];    // R0-R7
    uint8_t prg_ram_protect; // $A001

    uint8_t irq_latch;
    uint8_t irq_counter;
    bool    irq_enabled;
    bool    irq_reload;

    // A12 edge detection state. The counter is clocked by rising edges on the
    // PPU's A12 line, filtered so that the brief dips during a normal fetch
    // sequence do not each count as an edge.
    bool     a12_high;
    uint64_t a12_fell_at;
} Mapper4_State;

// The MMC3 requires A12 to have been low for roughly three CPU cycles before a
// rise counts. In PPU dots that is about nine.
#define MMC3_A12_FILTER_DOTS 9

static bool Mapper4_Init(BUS *bus)
{
    bus->mapper_data = calloc(1, sizeof(Mapper4_State));
    if (!bus->mapper_data) {
        DEBUG_ERROR("Failed to allocate MMC3 mapper state");
        return false;
    }
    return true;
}

static void Mapper4_Reset(BUS *bus)
{
    Mapper4_State *state = (Mapper4_State *)bus->mapper_data;
    if (!state) {
        return;
    }

    memset(state, 0, sizeof(*state));

    // Power on with cartridge RAM enabled and writable. The register that
    // controls this defaults to zero, but plenty of games never write it and
    // expect their save RAM to work regardless.
    state->prg_ram_protect = 0x80;

    bus->irq_asserted = false;
}

static uint8_t Mapper4_CPURead(BUS *bus, uint16_t address)
{
    Mapper4_State *state = (Mapper4_State *)bus->mapper_data;

    if (address >= 0x6000 && address < 0x8000) {
        if (!(state->prg_ram_protect & 0x80)) {
            return 0;
        }
        return Mapper_ReadPRGRam(bus, address);
    }

    if (address < 0x8000) {
        return 0;
    }

    size_t banks       = Mapper_GetPRG8kBankCount(bus);
    size_t last        = banks - 1;
    size_t second_last = banks >= 2 ? banks - 2 : 0;
    size_t bank;

    // Bit 6 of the bank-select register swaps which of the two switchable 8KB
    // windows sits at $8000 and which is fixed to the second-to-last bank.
    switch ((address - 0x8000u) >> 13) {
    case 0:  bank = (state->bank_select & 0x40) ? second_last : state->bank_regs[6]; break;
    case 1:  bank = state->bank_regs[7]; break;
    case 2:  bank = (state->bank_select & 0x40) ? state->bank_regs[6] : second_last; break;
    default: bank = last; break;
    }

    return Mapper_ReadPRG8(bus, bank, address);
}

static void Mapper4_CPUWrite(BUS *bus, uint16_t address, uint8_t value)
{
    Mapper4_State *state = (Mapper4_State *)bus->mapper_data;

    if (address >= 0x6000 && address < 0x8000) {
        // Bit 7 enables the chip, bit 6 write-protects it.
        if ((state->prg_ram_protect & 0xC0) == 0x80) {
            Mapper_WritePRGRam(bus, address, value);
        }
        return;
    }

    if (address < 0x8000) {
        return;
    }

    bool odd = (address & 1) != 0;

    switch ((address - 0x8000u) >> 13) {
    case 0: // $8000-$9FFF: bank select / bank data
        if (odd) {
            state->bank_regs[state->bank_select & 0x07] = value;
        } else {
            state->bank_select = value;
        }
        break;

    case 1: // $A000-$BFFF: mirroring / PRG RAM protect
        if (odd) {
            state->prg_ram_protect = value;
        } else if (bus->ppu && bus->ppu->mirror_mode != MIRROR_FOUR_SCREEN) {
            // Boards wired for four-screen VRAM ignore this register.
            PPU_SetMirroring(bus->ppu, (value & 0x01) ? MIRROR_HORIZONTAL : MIRROR_VERTICAL);
        }
        break;

    case 2: // $C000-$DFFF: IRQ latch / reload
        if (odd) {
            state->irq_counter = 0;
            state->irq_reload  = true;
        } else {
            state->irq_latch = value;
        }
        break;

    default: // $E000-$FFFF: IRQ disable (and acknowledge) / enable
        if (odd) {
            state->irq_enabled = true;
        } else {
            state->irq_enabled = false;
            bus->irq_asserted  = false;
        }
        break;
    }
}

static size_t Mapper4_ChrBank(const Mapper4_State *state, uint16_t address, uint16_t *offset_out)
{
    uint16_t a = address & 0x1FFFu;

    // Bit 7 of the bank-select register swaps the two 4KB halves of the pattern
    // table address space.
    if (state->bank_select & 0x80) {
        a ^= 0x1000u;
    }

    *offset_out = a & 0x03FFu;

    // $0000 and $0800 are 2KB windows (so the register's low bit is ignored);
    // the four windows from $1000 are 1KB each.
    if (a < 0x0800u) {
        return (size_t)(state->bank_regs[0] & 0xFEu) + ((a >> 10) & 1u);
    }
    if (a < 0x1000u) {
        return (size_t)(state->bank_regs[1] & 0xFEu) + ((a >> 10) & 1u);
    }
    return state->bank_regs[2 + ((a - 0x1000u) >> 10)];
}

static uint8_t Mapper4_PPURead(BUS *bus, uint16_t address)
{
    if (address >= 0x2000) {
        return 0;
    }

    Mapper4_State *state = (Mapper4_State *)bus->mapper_data;
    uint16_t offset;
    size_t bank = Mapper4_ChrBank(state, address, &offset);

    return Mapper_ReadCHR1(bus, bank, offset);
}

static void Mapper4_PPUWrite(BUS *bus, uint16_t address, uint8_t value)
{
    if (address >= 0x2000 || bus->chrRomDataSize > 0) {
        return; // CHR ROM is not writable
    }

    Mapper4_State *state = (Mapper4_State *)bus->mapper_data;
    uint16_t offset;
    size_t bank = Mapper4_ChrBank(state, address, &offset);

    bus->chrRam[((bank * 0x400u) + offset) & 0x1FFFu] = value;
}

static void Mapper4_PPUAddr(BUS *bus, uint16_t address)
{
    Mapper4_State *state = (Mapper4_State *)bus->mapper_data;
    if (!state) {
        return;
    }

    bool     a12 = (address & 0x1000u) != 0;
    uint64_t now = bus->ppu ? PPU_GetTotalCycles(bus->ppu) : 0;

    if (!a12) {
        if (state->a12_high) {
            state->a12_fell_at = now;
        }
        state->a12_high = false;
        return;
    }

    if (state->a12_high) {
        return; // Already high; not an edge
    }
    state->a12_high = true;

    if (now - state->a12_fell_at < MMC3_A12_FILTER_DOTS) {
        return; // Too soon after the fall to count
    }

    if (state->irq_counter == 0 || state->irq_reload) {
        state->irq_counter = state->irq_latch;
        state->irq_reload  = false;
    } else {
        state->irq_counter--;
    }

    if (state->irq_counter == 0 && state->irq_enabled) {
        bus->irq_asserted = true;
    }
}

// === Metadata ===

// Names only. These used to be ~250 nine-field rows whose handler pointers were
// all identical; the handlers now live in a six-entry dispatch table below.
static const char *const mapper_names[256] = {
    [0] = "NROM", [1] = "MMC1", [2] = "UNROM", [3] = "CNROM", [4] = "MMC3/MMC6", [5] = "MMC5",
    [6] = "FFE F4xxx", [7] = "AOROM", [8] = "FFE F3xxx", [9] = "MMC2", [10] = "MMC4",
    [11] = "Color Dreams", [12] = "FFE F6xxx", [13] = "CPROM", [15] = "100-in-1 Contra Function 16",
    [16] = "Bandai FCG", [17] = "FFE F8xxx", [18] = "Jaleco SS8806", [19] = "Namco 163",
    [20] = "Famicom Disk System", [21] = "Konami VRC4a/VRC4c", [22] = "Konami VRC2a",
    [23] = "Konami VRC2b/VRC4e/VRC4f", [24] = "Konami VRC6a", [25] = "Konami VRC4b/VRC4d",
    [26] = "Konami VRC6b", [28] = "Action 53", [30] = "UNROM 512", [31] = "NSF",
    [32] = "Irem G-101", [33] = "Taito TC0190/TC0350", [34] = "BNROM/NINA-001",
    [36] = "TXC 01-22000-400", [38] = "Crime Busters", [39] = "Subor", [41] = "Caltron 6-in-1",
    [42] = "Mario Baby", [43] = "SMB2j pirate", [46] = "Rumble Station", [47] = "MMC3 variant",
    [48] = "Taito TC190V", [49] = "NINA-03/NINA-06", [50] = "SMB2j pirate (alt)",
    [51] = "11-in-1 Ball Games", [52] = "MMC3 variant", [57] = "GK 6-in-1",
    [58] = "Study and Game 32-in-1", [60] = "Reset-based multicart", [61] = "20-in-1",
    [62] = "700-in-1", [64] = "Tengen RAMBO-1", [65] = "Irem H3001", [66] = "GxROM",
    [67] = "Sunsoft-3", [68] = "Sunsoft-4", [69] = "Sunsoft FME-7", [70] = "Bandai 74161/32",
    [71] = "Camerica/Codemasters", [72] = "Jaleco JF-17", [73] = "Konami VRC3",
    [74] = "MMC3 pirate", [75] = "Konami VRC1", [76] = "Namco 109", [77] = "Irem 74HC161/32",
    [78] = "Irem 74HC161/32 (Holy Diver)", [79] = "NINA-03/NINA-06", [80] = "Taito X1-005",
    [82] = "Taito X1-017", [85] = "Konami VRC7", [86] = "Jaleco JF-13",
    [87] = "Jaleco JF-17 (CHR only)", [88] = "Namco 118", [89] = "Sunsoft-2", [90] = "JY Company",
    [91] = "PC-HK-SF3", [92] = "Jaleco JF-19", [93] = "Sunsoft-2 (alt)", [94] = "UN1ROM",
    [95] = "Namco 108", [96] = "Bandai Oeka Kids", [97] = "Irem TAM-S1", [99] = "VS Unisystem",
    [100] = "Nesticle MMC3 pirate", [101] = "Jaleco", [105] = "NES-EVENT", [107] = "Magic Dragon",
    [108] = "FDS conversion", [111] = "GTROM", [112] = "Asder", [113] = "HES 6-in-1",
    [114] = "MMC3 variant", [115] = "MMC3 variant", [116] = "MMC3 + VRC2 hybrid",
    [117] = "Future Media", [118] = "TXSROM", [119] = "TQROM", [121] = "MMC3 variant",
    [123] = "MMC3 variant", [133] = "Sachen SA72007", [134] = "Sachen SA72008",
    [136] = "Sachen 3013", [137] = "Sachen 3014", [138] = "Sachen 3015", [139] = "Sachen 3016",
    [140] = "Jaleco JF-11/JF-14", [141] = "Sachen 8259A", [142] = "Sachen 8259B",
    [143] = "Sachen 8259C", [144] = "Sachen 8259D", [145] = "Sachen TCU01", [146] = "Sachen TCU02",
    [147] = "Sachen SA-016-1M", [148] = "Sachen SA-72007", [149] = "Sachen SA-72008",
    [150] = "Sachen SA-0036", [152] = "Bandai Oeka Kids (alt)", [153] = "Bandai",
    [154] = "Namcot 3453", [155] = "MMC1A", [156] = "Bandai", [157] = "Bandai Datach",
    [158] = "Tengen 800032", [159] = "Bandai", [163] = "Nanjing", [164] = "MMC3 variant",
    [165] = "MMC3 variant", [166] = "Subor", [167] = "Subor", [168] = "Racermate",
    [170] = "Future Media", [171] = "MMC3 variant", [172] = "CNROM clone", [173] = "MMC3 variant",
    [174] = "MMC3 variant", [175] = "Kaiser", [176] = "MMC3 variant", [177] = "Henggedianzi",
    [178] = "Education", [180] = "UNROM (Crazy Climber)", [182] = "Sunsoft", [183] = "Pirate",
    [184] = "Sunsoft-1", [185] = "CNROM with protection", [186] = "Family Study Box",
    [187] = "MMC3 variant", [188] = "Bandai", [189] = "MMC3 variant", [191] = "MMC3 variant",
    [192] = "MMC3 variant", [193] = "NTDEC TC-112", [194] = "MMC3 variant", [195] = "MMC3 variant",
    [196] = "MMC3 variant", [197] = "MMC3 variant", [198] = "MMC3 variant", [199] = "MMC3 variant",
    [200] = "MMC3 variant", [201] = "21-in-1", [202] = "150-in-1", [203] = "35-in-1",
    [204] = "64-in-1", [205] = "JY Company", [206] = "Namco 118 variant", [207] = "Taito",
    [210] = "Namco 175/340", [211] = "Mapper 211", [212] = "Mapper 212", [213] = "Mapper 213",
    [214] = "Mapper 214", [215] = "Mapper 215", [216] = "Mapper 216", [217] = "Mapper 217",
    [218] = "Mapper 218", [219] = "Mapper 219", [220] = "Mapper 220", [221] = "Mapper 221",
    [222] = "Mapper 222", [223] = "Mapper 223", [224] = "Mapper 224", [225] = "72-in-1",
    [226] = "76-in-1", [227] = "1200-in-1", [228] = "Active Enterprise", [229] = "31-in-1",
    [230] = "22-in-1", [231] = "20-in-1", [232] = "Camerica Quattro", [233] = "42-in-1",
    [234] = "Maxi-15", [235] = "150-in-1", [236] = "800-in-1", [237] = "1200-in-1",
    [238] = "Super 1000000-in-1", [239] = "Mapper 239", [240] = "Mapper 240", [241] = "Mapper 241",
    [242] = "Mapper 242", [243] = "Sachen 74LS374N", [244] = "Mapper 244", [245] = "Mapper 245",
    [246] = "Mapper 246", [247] = "Mapper 247", [248] = "Mapper 248", [249] = "Mapper 249",
    [250] = "Mapper 250", [251] = "Mapper 251", [252] = "Mapper 252", [253] = "Mapper 253",
    [254] = "Mapper 254", [255] = "Mapper 255",
};
// === Dispatch ===

// Behaves like NROM. Returned for every mapper cNES cannot emulate, including
// the unassigned and out-of-range IDs, so the bus never calls through a null
// handler -- IDs missing from the old 256-entry table did exactly that.
static const NES_MapperInfo mapper_default = {
    .id        = 0xFFFF,
    .cpu_read  = Mapper_DefaultCPURead,
    .cpu_write = Mapper_DefaultCPUWrite,
    .ppu_read  = Mapper_DefaultPPURead,
    .ppu_write = Mapper_DefaultPPUWrite
};

static const NES_MapperInfo mapper_impls[] = {
    { .id = 0,
      .cpu_read = Mapper0_CPURead, .cpu_write = Mapper0_CPUWrite,
      .ppu_read = Mapper0_PPURead, .ppu_write = Mapper0_PPUWrite },

    { .id = 1,
      .init = Mapper1_Init, .reset = Mapper1_Reset, .destroy = Mapper_FreeState,
      .cpu_read = Mapper1_CPURead, .cpu_write = Mapper1_CPUWrite,
      .ppu_read = Mapper1_PPURead, .ppu_write = Mapper1_PPUWrite },

    { .id = 2,
      .cpu_read = Mapper2_CPURead, .cpu_write = Mapper2_CPUWrite,
      .ppu_read = Mapper_DefaultPPURead, .ppu_write = Mapper_DefaultPPUWrite },

    { .id = 3,
      .cpu_read = Mapper_DefaultCPURead, .cpu_write = Mapper3_CPUWrite,
      .ppu_read = Mapper3_PPURead, .ppu_write = Mapper_DefaultPPUWrite },

    { .id = 4,
      .init = Mapper4_Init, .reset = Mapper4_Reset, .destroy = Mapper_FreeState,
      .cpu_read = Mapper4_CPURead, .cpu_write = Mapper4_CPUWrite,
      .ppu_read = Mapper4_PPURead, .ppu_write = Mapper4_PPUWrite,
      .ppu_addr = Mapper4_PPUAddr },

    { .id = 7,
      .cpu_read = Mapper7_CPURead, .cpu_write = Mapper7_CPUWrite,
      .ppu_read = Mapper_DefaultPPURead, .ppu_write = Mapper_DefaultPPUWrite }
};

#define MAPPER_IMPL_COUNT (sizeof(mapper_impls) / sizeof(mapper_impls[0]))

const NES_MapperInfo *NES_Mapper_Get(uint16_t mapper_id)
{
    // No logging in here. This is reached through the cached bus pointer on
    // every cartridge access, and a warning in this path once emitted millions
    // of lines per second for any ROM whose mapper was not implemented.
    for (size_t i = 0; i < MAPPER_IMPL_COUNT; ++i) {
        if (mapper_impls[i].id == mapper_id) {
            return &mapper_impls[i];
        }
    }

    return &mapper_default;
}

const char *NES_Mapper_GetName(uint16_t mapper_id)
{
    const char *name = (mapper_id < 256) ? mapper_names[mapper_id] : NULL;
    return name ? name : "Unknown/Unassigned";
}

bool NES_Mapper_IsKnown(uint16_t mapper_id)
{
    return mapper_id < 256 && mapper_names[mapper_id] != NULL;
}

bool NES_Mapper_IsSupported(uint16_t mapper_id)
{
    for (size_t i = 0; i < MAPPER_IMPL_COUNT; ++i) {
        if (mapper_impls[i].id == mapper_id) {
            return true;
        }
    }
    return false;
}
