#include "cNES/nes.h"
#include "cNES/bus.h"
#include "cNES/apu.h"
#include "cNES/mapper.h"
#include "cNES/ppu.h"
#include "cNES/cpu.h"

// BUS_Peek is for debuggers/tools that need to read memory without side effects.
uint8_t BUS_Peek(NES *nes, uint16_t address)
{
    if (address < 0x2000) {
        return nes->bus->memory[address & 0x07FF];
    } else if (address >= 0x2000 && address < 0x4000) {
        return PPU_GetOpenBusWithDecay(nes->ppu);
    } else if (address == 0x4016) {
        int controller_idx = 0;
        return nes->controller_shift[controller_idx] & 0x01;
    } else if (address == 0x4017) {
        int controller_idx = 1;
        return nes->controller_shift[controller_idx] & 0x01;
    } else if (address >= 0x4000 && address < 0x4020) {
        return 0;
    } else if (address >= 0x6000) {
        NES_MapperInfo mapper = NES_Mapper_Get(nes->bus->mapper);
        return mapper.cpu_read(nes->bus, address);
    }
    return 0;
}

void BUS_Write(NES *nes, uint16_t address, uint8_t value)
{
    if (address >= 0x6000) {
        PPU_CatchUp(nes->ppu);
        APU_CatchUp(nes->apu);
    }

    BUS_DriveOpenBus(nes, value);

    if (address < 0x2000) { // Internal RAM
        nes->bus->memory[address & 0x07FF] = value;
    } else if (address >= 0x2000 && address < 0x4000) { // PPU Registers
        // PPU_WriteRegister internally calls PPU_CatchUp
        PPU_WriteRegister(nes->ppu, 0x2000 + (address & 0x0007), value);
    } else if (address == 0x4014) { // OAM DMA
        PPU_CatchUp(nes->ppu); // Sync before driving DMA

        PPU_DriveOpenBus(nes->ppu, value);

        uint16_t dma_page_addr  = (uint16_t)value << 8;
        uint8_t  oam_start_addr = nes->ppu->oam_addr; 

        for (uint16_t i = 0; i < 256; ++i) {
            uint8_t byte_to_write                      = BUS_Read(nes, dma_page_addr + i);
            nes->ppu->oam[(oam_start_addr + i) & 0xFF] = byte_to_write;
        }

        // Account for OAM DMA cycles in runahead model
        nes->cpu->total_cycles += 513;
        
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
        NES_MapperInfo mapper = NES_Mapper_Get(nes->bus->mapper);
        mapper.cpu_write(nes->bus, address, value);
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
    NES_MapperInfo mapper = NES_Mapper_Get(bus_ptr->mapper);
    return mapper.ppu_read(bus_ptr, address);
}

// PPU writes to CHR RAM
void BUS_PPU_WriteCHR(struct BUS *bus_ptr, uint16_t address, uint8_t value)
{
    NES_MapperInfo mapper = NES_Mapper_Get(bus_ptr->mapper);
    mapper.ppu_write(bus_ptr, address, value);
}

// PPU reads from VRAM (nametables) and palette RAM
uint8_t BUS_PPU_Read(struct BUS *bus, uint16_t address)
{
    address &= 0x3FFF;
    if (address < 0x2000) {
        return BUS_PPU_ReadCHR(bus, address);
    } else if (address < 0x3F00) {
        uint16_t vram_addr = (address - 0x2000) & 0x0FFF;
        return bus->vram[vram_addr];
    } else if (address < 0x4000) {
        uint16_t pal_addr = (address - 0x3F00) & 0x1F;
        return bus->palette[pal_addr];
    }
    return 0;
}

// PPU writes to VRAM (nametables) and palette RAM
void BUS_PPU_Write(struct BUS *bus, uint16_t address, uint8_t value)
{
    address &= 0x3FFF;
    if (address < 0x2000) {
        BUS_PPU_WriteCHR(bus, address, value);
    } else if (address < 0x3F00) {
        uint16_t vram_addr   = (address - 0x2000) & 0x0FFF;
        bus->vram[vram_addr] = value;
    } else if (address < 0x4000) {
        uint16_t pal_addr      = (address - 0x3F00) & 0x1F;
        bus->palette[pal_addr] = value;
    }
}