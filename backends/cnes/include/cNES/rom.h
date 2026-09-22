#ifndef ROM_H
#define ROM_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

typedef enum ROM_Format {
    ROM_FORMAT_UNKNOWN = 0,
    ROM_FORMAT_INES,
    ROM_FORMAT_NES20,
    ROM_FORMAT_TNES,
    ROM_FORMAT_FDS,
} ROM_Format;

typedef struct ROM {
    FILE *file;

    ROM_Format format;
    uint8_t *data;
    size_t size;

    char *name;
    char *path;

    uint8_t header[16];

    uint16_t mapper_id; // NES 2.0 allows up to 4095, so this cannot be a byte
    bool has_battery;   // Header says cartridge RAM is battery backed
    size_t prg_rom_size; // in 16KB units
    size_t chr_rom_size; // in 8KB units
    size_t trainer_size;
    size_t prg_rom_offset;
    size_t chr_rom_offset;
} ROM;

ROM *ROM_LoadFile(const char *path);
ROM *ROM_LoadMemory(uint8_t *data, size_t size);
void ROM_Destroy(ROM *rom);

#endif // ROM_H