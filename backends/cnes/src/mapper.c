#include "cNES/mapper.h"

#include "libmse/debug.h"

#include "cNES/bus.h"
#include "cNES/ppu.h"

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

static uint8_t Mapper_DefaultCPURead(BUS *bus, uint16_t address)
{
    if (address >= 0x8000) {
        size_t prg_bytes = bus->prgRomDataSize;
        if (prg_bytes == 0 || !bus->prgRomData) {
            return 0;
        }

        size_t index = (address - 0x8000u) % prg_bytes;
        return bus->prgRomData[index];
    }

    // PRG RAM and unmapped areas are not modeled yet.
    return 0;
}

static void Mapper_DefaultCPUWrite(BUS *bus, uint16_t address, uint8_t value)
{
    (void)bus;
    (void)address;
    (void)value;
    // Default behavior for unsupported writes is ignore.
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
    uint16_t chr_addr = address & 0x1FFF;
    if (bus->chrRomDataSize == 0) {
        bus->chrRam[chr_addr] = value;
    }
}


static uint8_t Mapper0_CPURead(BUS *bus, uint16_t address)
{
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
    (void)bus;
    (void)address;
    (void)value;
    // NROM has no bank switching registers.
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

static uint8_t Mapper1_CPURead(BUS *bus, uint16_t address)
{
    Mapper1_State *mmc1_state = (Mapper1_State *)bus->mapper_data;

    if (address >= 0x6000 && address < 0x8000) {
        return mmc1_state->prg_ram[address - 0x6000];
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
        mmc1_state->prg_ram[address - 0x6000] = value;
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

static uint8_t Mapper2_CPURead(BUS *bus, uint16_t address)
{
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
    if (address >= 0x8000) {
        size_t prg_banks = Mapper_GetPRGBankCount(bus);
        if (prg_banks > 0) {
            bus->prgBankSelect = (uint8_t)(value % prg_banks);
        }
    }
}

static uint8_t Mapper3_PPURead(BUS *bus, uint16_t address)
{
    if (bus->chrRomDataSize == 0 || !bus->chrRomData) {
        return bus->chrRam[address & 0x1FFFu];
    }

    return Mapper_ReadCHR8(bus, bus->chrBankSelect, address);
}

static void Mapper3_CPUWrite(BUS *bus, uint16_t address, uint8_t value)
{
    if (address >= 0x8000) {
        size_t chr_banks = Mapper_GetCHRBankCount(bus);
        if (chr_banks > 0) {
            bus->chrBankSelect = (uint8_t)(value % chr_banks);
        }
    }
}

static uint8_t Mapper7_CPURead(BUS *bus, uint16_t address)
{
    if (address < 0x8000) {
        return 0;
    }

    uint16_t offset = (uint16_t)(address - 0x8000u);
    return Mapper_ReadPRG32(bus, bus->prgBankSelect, offset);
}

static void Mapper7_CPUWrite(BUS *bus, uint16_t address, uint8_t value)
{
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

#define MAPPER_ENTRY(ID, NAME, CPU_RD, CPU_WR, PPU_RD, PPU_WR) \
    [ID] = {                                    \
        .id = (uint8_t)(ID),                    \
        .name = (NAME),                         \
        .known = true,                          \
        .supported = true,                      \
        .cpu_read = (CPU_RD),                   \
        .cpu_write = (CPU_WR),                  \
        .ppu_read = (PPU_RD),                   \
        .ppu_write = (PPU_WR)                   \
    }

#define MAPPER_ENTRY_UNSUPPORTED(ID, NAME, SUPPORTED_FLAG) \
    [ID] = {                                    \
        .id = (uint8_t)(ID),                    \
        .name = (NAME),                         \
        .known = true,                          \
        .supported = (SUPPORTED_FLAG),          \
        .cpu_read = Mapper_DefaultCPURead,      \
        .cpu_write = Mapper_DefaultCPUWrite,    \
        .ppu_read = Mapper_DefaultPPURead,      \
        .ppu_write = Mapper_DefaultPPUWrite     \
    }

static const NES_MapperInfo mapper_table[256] = {
    MAPPER_ENTRY(0, "NROM", Mapper0_CPURead, Mapper0_CPUWrite, Mapper0_PPURead, Mapper0_PPUWrite),
    MAPPER_ENTRY(1, "MMC1", Mapper1_CPURead, Mapper1_CPUWrite, Mapper1_PPURead, Mapper1_PPUWrite),
    MAPPER_ENTRY(2, "UNROM", Mapper2_CPURead, Mapper2_CPUWrite, Mapper_DefaultPPURead, Mapper_DefaultPPUWrite),
    MAPPER_ENTRY(3, "CNROM", Mapper_DefaultCPURead, Mapper3_CPUWrite, Mapper3_PPURead, Mapper_DefaultPPUWrite),
    MAPPER_ENTRY_UNSUPPORTED(4, "MMC3/MMC6", false),
    MAPPER_ENTRY_UNSUPPORTED(5, "MMC5", false),
    MAPPER_ENTRY_UNSUPPORTED(6, "FFE F4xxx", false),
    MAPPER_ENTRY(7, "AOROM", Mapper7_CPURead, Mapper7_CPUWrite, Mapper_DefaultPPURead, Mapper_DefaultPPUWrite),
    MAPPER_ENTRY_UNSUPPORTED(8, "FFE F3xxx", false),
    MAPPER_ENTRY_UNSUPPORTED(9, "MMC2", false),
    MAPPER_ENTRY_UNSUPPORTED(10, "MMC4", false),
    MAPPER_ENTRY_UNSUPPORTED(11, "Color Dreams", false),
    MAPPER_ENTRY_UNSUPPORTED(12, "FFE F6xxx", false),
    MAPPER_ENTRY_UNSUPPORTED(13, "CPROM", false),
    MAPPER_ENTRY_UNSUPPORTED(15, "100-in-1 Contra Function 16", false),
    MAPPER_ENTRY_UNSUPPORTED(16, "Bandai FCG", false),
    MAPPER_ENTRY_UNSUPPORTED(17, "FFE F8xxx", false),
    MAPPER_ENTRY_UNSUPPORTED(18, "Jaleco SS8806", false),
    MAPPER_ENTRY_UNSUPPORTED(19, "Namco 163", false),
    MAPPER_ENTRY_UNSUPPORTED(20, "Famicom Disk System", false),
    MAPPER_ENTRY_UNSUPPORTED(21, "Konami VRC4a/VRC4c", false),
    MAPPER_ENTRY_UNSUPPORTED(22, "Konami VRC2a", false),
    MAPPER_ENTRY_UNSUPPORTED(23, "Konami VRC2b/VRC4e/VRC4f", false),
    MAPPER_ENTRY_UNSUPPORTED(24, "Konami VRC6a", false),
    MAPPER_ENTRY_UNSUPPORTED(25, "Konami VRC4b/VRC4d", false),
    MAPPER_ENTRY_UNSUPPORTED(26, "Konami VRC6b", false),
    MAPPER_ENTRY_UNSUPPORTED(28, "Action 53", false),
    MAPPER_ENTRY_UNSUPPORTED(30, "UNROM 512", false),
    MAPPER_ENTRY_UNSUPPORTED(31, "NSF", false),
    MAPPER_ENTRY_UNSUPPORTED(32, "Irem G-101", false),
    MAPPER_ENTRY_UNSUPPORTED(33, "Taito TC0190/TC0350", false),
    MAPPER_ENTRY_UNSUPPORTED(34, "BNROM/NINA-001", false),
    MAPPER_ENTRY_UNSUPPORTED(36, "TXC 01-22000-400", false),
    MAPPER_ENTRY_UNSUPPORTED(38, "Crime Busters", false),
    MAPPER_ENTRY_UNSUPPORTED(39, "Subor", false),
    MAPPER_ENTRY_UNSUPPORTED(41, "Caltron 6-in-1", false),
    MAPPER_ENTRY_UNSUPPORTED(42, "Mario Baby", false),
    MAPPER_ENTRY_UNSUPPORTED(43, "SMB2j pirate", false),
    MAPPER_ENTRY_UNSUPPORTED(46, "Rumble Station", false),
    MAPPER_ENTRY_UNSUPPORTED(47, "MMC3 variant", false),
    MAPPER_ENTRY_UNSUPPORTED(48, "Taito TC190V", false),
    MAPPER_ENTRY_UNSUPPORTED(49, "NINA-03/NINA-06", false),
    MAPPER_ENTRY_UNSUPPORTED(50, "SMB2j pirate (alt)", false),
    MAPPER_ENTRY_UNSUPPORTED(51, "11-in-1 Ball Games", false),
    MAPPER_ENTRY_UNSUPPORTED(52, "MMC3 variant", false),
    MAPPER_ENTRY_UNSUPPORTED(57, "GK 6-in-1", false),
    MAPPER_ENTRY_UNSUPPORTED(58, "Study and Game 32-in-1", false),
    MAPPER_ENTRY_UNSUPPORTED(60, "Reset-based multicart", false),
    MAPPER_ENTRY_UNSUPPORTED(61, "20-in-1", false),
    MAPPER_ENTRY_UNSUPPORTED(62, "700-in-1", false),
    MAPPER_ENTRY_UNSUPPORTED(64, "Tengen RAMBO-1", false),
    MAPPER_ENTRY_UNSUPPORTED(65, "Irem H3001", false),
    MAPPER_ENTRY_UNSUPPORTED(66, "GxROM", false),
    MAPPER_ENTRY_UNSUPPORTED(67, "Sunsoft-3", false),
    MAPPER_ENTRY_UNSUPPORTED(68, "Sunsoft-4", false),
    MAPPER_ENTRY_UNSUPPORTED(69, "Sunsoft FME-7", false),
    MAPPER_ENTRY_UNSUPPORTED(70, "Bandai 74161/32", false),
    MAPPER_ENTRY_UNSUPPORTED(71, "Camerica/Codemasters", false),
    MAPPER_ENTRY_UNSUPPORTED(72, "Jaleco JF-17", false),
    MAPPER_ENTRY_UNSUPPORTED(73, "Konami VRC3", false),
    MAPPER_ENTRY_UNSUPPORTED(74, "MMC3 pirate", false),
    MAPPER_ENTRY_UNSUPPORTED(75, "Konami VRC1", false),
    MAPPER_ENTRY_UNSUPPORTED(76, "Namco 109", false),
    MAPPER_ENTRY_UNSUPPORTED(77, "Irem 74HC161/32", false),
    MAPPER_ENTRY_UNSUPPORTED(78, "Irem 74HC161/32 (Holy Diver)", false),
    MAPPER_ENTRY_UNSUPPORTED(79, "NINA-03/NINA-06", false),
    MAPPER_ENTRY_UNSUPPORTED(80, "Taito X1-005", false),
    MAPPER_ENTRY_UNSUPPORTED(82, "Taito X1-017", false),
    MAPPER_ENTRY_UNSUPPORTED(85, "Konami VRC7", false),
    MAPPER_ENTRY_UNSUPPORTED(86, "Jaleco JF-13", false),
    MAPPER_ENTRY_UNSUPPORTED(87, "Jaleco JF-17 (CHR only)", false),
    MAPPER_ENTRY_UNSUPPORTED(88, "Namco 118", false),
    MAPPER_ENTRY_UNSUPPORTED(89, "Sunsoft-2", false),
    MAPPER_ENTRY_UNSUPPORTED(90, "JY Company", false),
    MAPPER_ENTRY_UNSUPPORTED(91, "PC-HK-SF3", false),
    MAPPER_ENTRY_UNSUPPORTED(92, "Jaleco JF-19", false),
    MAPPER_ENTRY_UNSUPPORTED(93, "Sunsoft-2 (alt)", false),
    MAPPER_ENTRY_UNSUPPORTED(94, "UN1ROM", false),
    MAPPER_ENTRY_UNSUPPORTED(95, "Namco 108", false),
    MAPPER_ENTRY_UNSUPPORTED(96, "Bandai Oeka Kids", false),
    MAPPER_ENTRY_UNSUPPORTED(97, "Irem TAM-S1", false),
    MAPPER_ENTRY_UNSUPPORTED(99, "VS Unisystem", false),
    MAPPER_ENTRY_UNSUPPORTED(100, "Nesticle MMC3 pirate", false),
    MAPPER_ENTRY_UNSUPPORTED(101, "Jaleco", false),
    MAPPER_ENTRY_UNSUPPORTED(105, "NES-EVENT", false),
    MAPPER_ENTRY_UNSUPPORTED(107, "Magic Dragon", false),
    MAPPER_ENTRY_UNSUPPORTED(108, "FDS conversion", false),
    MAPPER_ENTRY_UNSUPPORTED(111, "GTROM", false),
    MAPPER_ENTRY_UNSUPPORTED(112, "Asder", false),
    MAPPER_ENTRY_UNSUPPORTED(113, "HES 6-in-1", false),
    MAPPER_ENTRY_UNSUPPORTED(114, "MMC3 variant", false),
    MAPPER_ENTRY_UNSUPPORTED(115, "MMC3 variant", false),
    MAPPER_ENTRY_UNSUPPORTED(116, "MMC3 + VRC2 hybrid", false),
    MAPPER_ENTRY_UNSUPPORTED(117, "Future Media", false),
    MAPPER_ENTRY_UNSUPPORTED(118, "TXSROM", false),
    MAPPER_ENTRY_UNSUPPORTED(119, "TQROM", false),
    MAPPER_ENTRY_UNSUPPORTED(121, "MMC3 variant", false),
    MAPPER_ENTRY_UNSUPPORTED(123, "MMC3 variant", false),
    MAPPER_ENTRY_UNSUPPORTED(133, "Sachen SA72007", false),
    MAPPER_ENTRY_UNSUPPORTED(134, "Sachen SA72008", false),
    MAPPER_ENTRY_UNSUPPORTED(136, "Sachen 3013", false),
    MAPPER_ENTRY_UNSUPPORTED(137, "Sachen 3014", false),
    MAPPER_ENTRY_UNSUPPORTED(138, "Sachen 3015", false),
    MAPPER_ENTRY_UNSUPPORTED(139, "Sachen 3016", false),
    MAPPER_ENTRY_UNSUPPORTED(140, "Jaleco JF-11/JF-14", false),
    MAPPER_ENTRY_UNSUPPORTED(141, "Sachen 8259A", false),
    MAPPER_ENTRY_UNSUPPORTED(142, "Sachen 8259B", false),
    MAPPER_ENTRY_UNSUPPORTED(143, "Sachen 8259C", false),
    MAPPER_ENTRY_UNSUPPORTED(144, "Sachen 8259D", false),
    MAPPER_ENTRY_UNSUPPORTED(145, "Sachen TCU01", false),
    MAPPER_ENTRY_UNSUPPORTED(146, "Sachen TCU02", false),
    MAPPER_ENTRY_UNSUPPORTED(147, "Sachen SA-016-1M", false),
    MAPPER_ENTRY_UNSUPPORTED(148, "Sachen SA-72007", false),
    MAPPER_ENTRY_UNSUPPORTED(149, "Sachen SA-72008", false),
    MAPPER_ENTRY_UNSUPPORTED(150, "Sachen SA-0036", false),
    MAPPER_ENTRY_UNSUPPORTED(152, "Bandai Oeka Kids (alt)", false),
    MAPPER_ENTRY_UNSUPPORTED(153, "Bandai", false),
    MAPPER_ENTRY_UNSUPPORTED(154, "Namcot 3453", false),
    MAPPER_ENTRY_UNSUPPORTED(155, "MMC1A", false),
    MAPPER_ENTRY_UNSUPPORTED(156, "Bandai", false),
    MAPPER_ENTRY_UNSUPPORTED(157, "Bandai Datach", false),
    MAPPER_ENTRY_UNSUPPORTED(158, "Tengen 800032", false),
    MAPPER_ENTRY_UNSUPPORTED(159, "Bandai", false),
    MAPPER_ENTRY_UNSUPPORTED(163, "Nanjing", false),
    MAPPER_ENTRY_UNSUPPORTED(164, "MMC3 variant", false),
    MAPPER_ENTRY_UNSUPPORTED(165, "MMC3 variant", false),
    MAPPER_ENTRY_UNSUPPORTED(166, "Subor", false),
    MAPPER_ENTRY_UNSUPPORTED(167, "Subor", false),
    MAPPER_ENTRY_UNSUPPORTED(168, "Racermate", false),
    MAPPER_ENTRY_UNSUPPORTED(170, "Future Media", false),
    MAPPER_ENTRY_UNSUPPORTED(171, "MMC3 variant", false),
    MAPPER_ENTRY_UNSUPPORTED(172, "CNROM clone", false),
    MAPPER_ENTRY_UNSUPPORTED(173, "MMC3 variant", false),
    MAPPER_ENTRY_UNSUPPORTED(174, "MMC3 variant", false),
    MAPPER_ENTRY_UNSUPPORTED(175, "Kaiser", false),
    MAPPER_ENTRY_UNSUPPORTED(176, "MMC3 variant", false),
    MAPPER_ENTRY_UNSUPPORTED(177, "Henggedianzi", false),
    MAPPER_ENTRY_UNSUPPORTED(178, "Education", false),
    MAPPER_ENTRY_UNSUPPORTED(180, "UNROM (Crazy Climber)", false),
    MAPPER_ENTRY_UNSUPPORTED(182, "Sunsoft", false),
    MAPPER_ENTRY_UNSUPPORTED(183, "Pirate", false),
    MAPPER_ENTRY_UNSUPPORTED(184, "Sunsoft-1", false),
    MAPPER_ENTRY_UNSUPPORTED(185, "CNROM with protection", false),
    MAPPER_ENTRY_UNSUPPORTED(186, "Family Study Box", false),
    MAPPER_ENTRY_UNSUPPORTED(187, "MMC3 variant", false),
    MAPPER_ENTRY_UNSUPPORTED(188, "Bandai", false),
    MAPPER_ENTRY_UNSUPPORTED(189, "MMC3 variant", false),
    MAPPER_ENTRY_UNSUPPORTED(191, "MMC3 variant", false),
    MAPPER_ENTRY_UNSUPPORTED(192, "MMC3 variant", false),
    MAPPER_ENTRY_UNSUPPORTED(193, "NTDEC TC-112", false),
    MAPPER_ENTRY_UNSUPPORTED(194, "MMC3 variant", false),
    MAPPER_ENTRY_UNSUPPORTED(195, "MMC3 variant", false),
    MAPPER_ENTRY_UNSUPPORTED(196, "MMC3 variant", false),
    MAPPER_ENTRY_UNSUPPORTED(197, "MMC3 variant", false),
    MAPPER_ENTRY_UNSUPPORTED(198, "MMC3 variant", false),
    MAPPER_ENTRY_UNSUPPORTED(199, "MMC3 variant", false),
    MAPPER_ENTRY_UNSUPPORTED(200, "MMC3 variant", false),
    MAPPER_ENTRY_UNSUPPORTED(201, "21-in-1", false),
    MAPPER_ENTRY_UNSUPPORTED(202, "150-in-1", false),
    MAPPER_ENTRY_UNSUPPORTED(203, "35-in-1", false),
    MAPPER_ENTRY_UNSUPPORTED(204, "64-in-1", false),
    MAPPER_ENTRY_UNSUPPORTED(205, "JY Company", false),
    MAPPER_ENTRY_UNSUPPORTED(206, "Namco 118 variant", false),
    MAPPER_ENTRY_UNSUPPORTED(207, "Taito", false),
    MAPPER_ENTRY_UNSUPPORTED(210, "Namco 175/340", false),
    MAPPER_ENTRY_UNSUPPORTED(211, "Mapper 211", false),
    MAPPER_ENTRY_UNSUPPORTED(212, "Mapper 212", false),
    MAPPER_ENTRY_UNSUPPORTED(213, "Mapper 213", false),
    MAPPER_ENTRY_UNSUPPORTED(214, "Mapper 214", false),
    MAPPER_ENTRY_UNSUPPORTED(215, "Mapper 215", false),
    MAPPER_ENTRY_UNSUPPORTED(216, "Mapper 216", false),
    MAPPER_ENTRY_UNSUPPORTED(217, "Mapper 217", false),
    MAPPER_ENTRY_UNSUPPORTED(218, "Mapper 218", false),
    MAPPER_ENTRY_UNSUPPORTED(219, "Mapper 219", false),
    MAPPER_ENTRY_UNSUPPORTED(220, "Mapper 220", false),
    MAPPER_ENTRY_UNSUPPORTED(221, "Mapper 221", false),
    MAPPER_ENTRY_UNSUPPORTED(222, "Mapper 222", false),
    MAPPER_ENTRY_UNSUPPORTED(223, "Mapper 223", false),
    MAPPER_ENTRY_UNSUPPORTED(224, "Mapper 224", false),
    MAPPER_ENTRY_UNSUPPORTED(225, "72-in-1", false),
    MAPPER_ENTRY_UNSUPPORTED(226, "76-in-1", false),
    MAPPER_ENTRY_UNSUPPORTED(227, "1200-in-1", false),
    MAPPER_ENTRY_UNSUPPORTED(228, "Active Enterprise", false),
    MAPPER_ENTRY_UNSUPPORTED(229, "31-in-1", false),
    MAPPER_ENTRY_UNSUPPORTED(230, "22-in-1", false),
    MAPPER_ENTRY_UNSUPPORTED(231, "20-in-1", false),
    MAPPER_ENTRY_UNSUPPORTED(232, "Camerica Quattro", false),
    MAPPER_ENTRY_UNSUPPORTED(233, "42-in-1", false),
    MAPPER_ENTRY_UNSUPPORTED(234, "Maxi-15", false),
    MAPPER_ENTRY_UNSUPPORTED(235, "150-in-1", false),
    MAPPER_ENTRY_UNSUPPORTED(236, "800-in-1", false),
    MAPPER_ENTRY_UNSUPPORTED(237, "1200-in-1", false),
    MAPPER_ENTRY_UNSUPPORTED(238, "Super 1000000-in-1", false),
    MAPPER_ENTRY_UNSUPPORTED(239, "Mapper 239", false),
    MAPPER_ENTRY_UNSUPPORTED(240, "Mapper 240", false),
    MAPPER_ENTRY_UNSUPPORTED(241, "Mapper 241", false),
    MAPPER_ENTRY_UNSUPPORTED(242, "Mapper 242", false),
    MAPPER_ENTRY_UNSUPPORTED(243, "Sachen 74LS374N", false),
    MAPPER_ENTRY_UNSUPPORTED(244, "Mapper 244", false),
    MAPPER_ENTRY_UNSUPPORTED(245, "Mapper 245", false),
    MAPPER_ENTRY_UNSUPPORTED(246, "Mapper 246", false),
    MAPPER_ENTRY_UNSUPPORTED(247, "Mapper 247", false),
    MAPPER_ENTRY_UNSUPPORTED(248, "Mapper 248", false),
    MAPPER_ENTRY_UNSUPPORTED(249, "Mapper 249", false),
    MAPPER_ENTRY_UNSUPPORTED(250, "Mapper 250", false),
    MAPPER_ENTRY_UNSUPPORTED(251, "Mapper 251", false),
    MAPPER_ENTRY_UNSUPPORTED(252, "Mapper 252", false),
    MAPPER_ENTRY_UNSUPPORTED(253, "Mapper 253", false),
    MAPPER_ENTRY_UNSUPPORTED(254, "Mapper 254", false),
    MAPPER_ENTRY_UNSUPPORTED(255, "Mapper 255", false)
};

#undef MAPPER_ENTRY

NES_MapperInfo NES_Mapper_Get(uint32_t mapper_id)
{
    NES_MapperInfo info = {
        .id = (uint8_t)mapper_id,
        .name = "Unknown/Unassigned",
        .known = false,
        .supported = false,
        .cpu_read = Mapper_DefaultCPURead,
        .cpu_write = Mapper_DefaultCPUWrite,
        .ppu_read = Mapper_DefaultPPURead,
        .ppu_write = Mapper_DefaultPPUWrite
    };

    if (mapper_id > 255) {
        DEBUG_WARN("Mapper ID %u is out of valid range [0, 255]; returning default unknown mapper info.", mapper_id);
        return info;
    }

    info = mapper_table[mapper_id];
    
    if (!mapper_table[mapper_id].known) {
        DEBUG_WARN("Mapper %u is unknown; using default behavior.", mapper_id);
    }

    return info;
}
