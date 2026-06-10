#ifndef CNES_MAPPER_H
#define CNES_MAPPER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct BUS BUS;

typedef uint8_t (*NES_MapperCPUReadFn)(BUS *bus, uint16_t address);
typedef void (*NES_MapperCPUWriteFn)(BUS *bus, uint16_t address, uint8_t value);
typedef uint8_t (*NES_MapperPPUReadFn)(BUS *bus, uint16_t address);
typedef void (*NES_MapperPPUWriteFn)(BUS *bus, uint16_t address, uint8_t value);

typedef struct NES_MapperInfo {
    uint8_t id;
    const char *name;
    bool known;
    bool supported;
    NES_MapperCPUReadFn cpu_read;
    NES_MapperCPUWriteFn cpu_write;
    NES_MapperPPUReadFn ppu_read;
    NES_MapperPPUWriteFn ppu_write;
} NES_MapperInfo;

typedef struct {
    uint8_t shift_register;
    uint8_t control;
    uint8_t chr_bank_0;
    uint8_t chr_bank_1;
    uint8_t prg_bank;
    uint8_t prg_ram[8192];
} Mapper1_State;

// Returns mapper metadata for any mapper ID in [0, 255].
NES_MapperInfo NES_Mapper_Get(uint32_t mapper_id);

#endif // CNES_MAPPER_H
