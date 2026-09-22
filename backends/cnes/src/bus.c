#include "cNES/nes.h"
#include "cNES/bus.h"
#include "cNES/apu.h"
#include "cNES/mapper.h"
#include "cNES/ppu.h"
#include "cNES/cpu.h"

// BUS_Peek is for debuggers/tools that need to read memory without side effects.
//
// BUS_Read cannot stand in for it: reading $2002 clears the VBlank flag and
// resets the write latch, $2007 advances the VRAM address, and $4016 clocks the
// controller shift register. Opening a memory viewer must not change what the
// ROM sees, so each region is read from its backing store instead.
//
// The NULL guards are not paranoia: this is called from the debug API, which
// can be pointed at the console before a ROM has been loaded into it.
uint8_t BUS_Peek(NES *nes, uint16_t address)
{
    if (address < 0x2000) {
        return nes->bus->ram[address & (BUS_RAM_SIZE - 1)];
    }

    if (address < 0x4000) {
        PPU *ppu = nes->ppu;
        if (ppu == NULL) {
            return 0;
        }
        // Only these three read back without consequence. The rest are
        // write-only latches, so report the PPU's open bus as hardware would.
        //
        // Deliberately not PPU_GetOpenBusWithDecay: applying the decay writes
        // ppu->open_bus, which is exactly the kind of side effect this function
        // exists to avoid. A peek can therefore report an open bus value that
        // the next real read would have already decayed to zero.
        switch (0x2000 + (address & 0x0007)) {
            case 0x2002: return ppu->status;
            case 0x2004: return ppu->oam[ppu->oam_addr];
            case 0x2007: return ppu->data_buffer;
            default:     return ppu->open_bus;
        }
    }

    if (address == 0x4016 || address == 0x4017) {
        // Same shift-register semantics as the live read, minus the shift.
        return NES_ControllerPeek(nes, address & 1) | (uint8_t)(nes->cpu_open_bus & 0xFE);
    }

    // The rest of the APU/IO block acknowledges IRQs when read for real, so
    // report open bus rather than disturb anything.
    if (address < 0x4020) {
        return BUS_GetOpenBus(nes);
    }

    // $4020 up is the cartridge. A mapper's cpu_read is the only way to see
    // its registers, and is where a peek stops being provably free of side
    // effects -- a mapper whose read ports latch something will still latch it.
    const NES_MapperInfo *mapper = nes->bus->mapper_info;
    if (mapper == NULL || mapper->cpu_read == NULL) {
        return 0;
    }
    return mapper->cpu_read(nes->bus, address);
}

void BUS_Write(NES *nes, uint16_t address, uint8_t value)
{
    if (address >= 0x6000) {
        PPU_CatchUp(nes->ppu);
        APU_CatchUp(nes->apu);
    }

    BUS_DriveOpenBus(nes, value);

    if (address < 0x2000) { // Internal RAM
        nes->bus->ram[address & (BUS_RAM_SIZE - 1)] = value;
    } else if (address >= 0x2000 && address < 0x4000) { // PPU Registers
        // PPU_WriteRegister internally calls PPU_CatchUp
        PPU_WriteRegister(nes->ppu, 0x2000 + (address & 0x0007), value);
    } else if (address == 0x4014) { // OAM DMA
        PPU_CatchUp(nes->ppu); // Sync before driving DMA

        PPU_DriveOpenBus(nes->ppu, value);

        const uint16_t dma_page_addr  = (uint16_t)value << 8;
        const uint8_t  oam_start_addr = nes->ppu->oam_addr;

        // The CPU is halted for one cycle, plus a second if the transfer would
        // otherwise start on a write cycle, and then alternates read and write
        // for 256 bytes -- 513 or 514 cycles in total.
        //
        // These are ticked one at a time rather than charged as a single lump.
        // With the clock frozen for the whole transfer nothing could ever come
        // due partway through, so a DMC fetch could never interleave with an
        // OAM DMA; that is not just a timing inaccuracy, it hangs any ROM that
        // waits for the two to collide.
        CPU_DmaHaltCycle(nes->cpu);
        if (nes->cpu->total_cycles & 1u) {
            CPU_DmaHaltCycle(nes->cpu);
        }

        for (uint16_t i = 0; i < 256; ++i) {
            CPU_DmaHaltCycle(nes->cpu); // read cycle
            uint8_t byte_to_write = BUS_Read(nes, (uint16_t)(dma_page_addr + i));

            CPU_DmaHaltCycle(nes->cpu); // write cycle
            nes->ppu->oam[(oam_start_addr + i) & 0xFF] = byte_to_write;
        }

        // Fast-forward PPU one more time to digest DMA delay immediately
        PPU_CatchUp(nes->ppu);

    } else if (address == 0x4016) { // Controller Strobe
        nes->controller_strobe = value & 0x01;
        if (nes->controller_strobe == 0) {
            nes->controller_shift[0] = nes->controllers[0];
            nes->controller_shift[1] = nes->controllers[1];
        }
    } else if (address >= 0x4000 && address < 0x4020) { // APU and I/O Registers
        if (nes->apu) {
            APU_WriteRegister(nes->apu, address, value);
        }
    } else if (address >= 0x6000) {
        nes->bus->mapper_info->cpu_write(nes->bus, address, value);
    }
}

void BUS_Write16(NES *nes, uint16_t address, uint16_t value)
{
    uint8_t lo = (uint8_t)(value & 0x00FF);
    uint8_t hi = (uint8_t)(value >> 8);
    BUS_Write(nes, address, lo);
    BUS_Write(nes, address + 1, hi);
}

// --- PPU Bus Mapping (for PPU's internal access to CHR and VRAM/Palette) ---

// PPU reads from CHR ROM/RAM
uint8_t BUS_PPU_ReadCHR(struct BUS *bus_ptr, uint16_t address)
{
    return bus_ptr->mapper_info->ppu_read(bus_ptr, address);
}

// PPU writes to CHR RAM
void BUS_PPU_WriteCHR(struct BUS *bus_ptr, uint16_t address, uint8_t value)
{
    bus_ptr->mapper_info->ppu_write(bus_ptr, address, value);
}
