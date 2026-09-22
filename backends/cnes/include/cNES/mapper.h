#ifndef CNES_MAPPER_H
#define CNES_MAPPER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct BUS BUS;

typedef uint8_t (*NES_MapperCPUReadFn)(BUS *bus, uint16_t address);
typedef void (*NES_MapperCPUWriteFn)(BUS *bus, uint16_t address, uint8_t value);
typedef uint8_t (*NES_MapperPPUReadFn)(BUS *bus, uint16_t address);
typedef void (*NES_MapperPPUWriteFn)(BUS *bus, uint16_t address, uint8_t value);

// Lifecycle. A mapper that needs private state allocates it into bus->mapper_data
// in init and releases it in destroy, so the core no longer has to know which
// mapper numbers have state or how big it is.
typedef bool (*NES_MapperInitFn)(BUS *bus);
typedef void (*NES_MapperResetFn)(BUS *bus);
typedef void (*NES_MapperDestroyFn)(BUS *bus);

// Called by the PPU whenever it drives a pattern-table address onto the video
// bus. Mappers that count A12 rising edges to time an IRQ (MMC3 and its clones)
// hook this; everyone else leaves it NULL.
typedef void (*NES_MapperPPUAddrFn)(BUS *bus, uint16_t address);

typedef struct NES_MapperInfo {
    uint16_t id;

    NES_MapperInitFn    init;    // Optional
    NES_MapperResetFn   reset;   // Optional
    NES_MapperDestroyFn destroy; // Optional

    NES_MapperCPUReadFn  cpu_read;
    NES_MapperCPUWriteFn cpu_write;
    NES_MapperPPUReadFn  ppu_read;
    NES_MapperPPUWriteFn ppu_write;

    NES_MapperPPUAddrFn ppu_addr; // Optional
} NES_MapperInfo;

// Handlers for a mapper number. Never returns NULL and never returns an entry
// with null handlers: unimplemented, unassigned and out-of-range IDs all resolve
// to a safe default that behaves like NROM.
const NES_MapperInfo *NES_Mapper_Get(uint16_t mapper_id);

// Metadata, kept apart from the handlers so that the ~200 mappers cNES knows the
// name of but cannot emulate cost one table row each instead of nine.
const char *NES_Mapper_GetName(uint16_t mapper_id);
bool NES_Mapper_IsKnown(uint16_t mapper_id);
bool NES_Mapper_IsSupported(uint16_t mapper_id);

#endif // CNES_MAPPER_H
