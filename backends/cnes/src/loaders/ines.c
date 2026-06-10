#include "cNES/loaders/ines.h"

#include "libmse/debug.h"
#include "cNES/rom.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t INES_MapTNESMapper(uint8_t raw_mapper, bool *supported)
{
    if (supported) {
        *supported = true;
    }

    switch (raw_mapper) {
        case 0: return 0;  /* NROM */
        case 1: return 1;  /* SxROM / MMC1 */
        case 2: return 9;  /* PNROM / MMC2 */
        case 3: return 4;  /* TxROM / MMC3 */
        case 4: return 10; /* FxROM / MMC4 */
        case 5: return 5;  /* ExROM / MMC5 */
        case 6: return 2;  /* UxROM / UNROM */
        case 7: return 3;  /* CNROM */
        case 8: return 7;  /* AxROM / AOROM */
        default:
            if (supported) {
                *supported = false;
            }
            return 0;
    }
}

static ROM *INES_LoadFromBuffer(uint8_t *data, size_t size, const char *path)
{
    if (!data || size < 16) {
        DEBUG_ERROR("Invalid ROM data or size");
        return NULL;
    }

    ROM *rom = malloc(sizeof(ROM));
    if (!rom) {
        DEBUG_ERROR("Memory allocation failed for ROM struct");
        return NULL;
    }

    memset(rom, 0, sizeof(ROM));
    rom->format = ROM_FORMAT_UNKNOWN;

    rom->data = malloc(size);
    if (!rom->data) {
        DEBUG_ERROR("Memory allocation failed for ROM data buffer");
        goto error;
    }

    memcpy(rom->data, data, size);
    rom->size = size;
    memcpy(rom->header, data, sizeof(rom->header));

    if (memcmp(rom->header, "NES\x1A", 4) == 0) {
        rom->format = ((rom->header[7] & 0x0C) == 0x08) ? ROM_FORMAT_NES20 : ROM_FORMAT_INES;
        rom->mapper_id = (uint8_t)(((rom->header[8] & 0x0F) << 8) |
                                   (rom->header[7] & 0xF0) |
                                   ((rom->header[6] & 0xF0) >> 4));

        if (rom->format == ROM_FORMAT_NES20) {
            uint16_t prg_msb = rom->header[9] & 0x0F;
            if (prg_msb == 0x0F) {
                uint8_t exponent = (rom->header[4] >> 2) & 0x3F;
                uint8_t multiplier = rom->header[4] & 0x03;
                rom->prg_rom_size = (size_t)((1ULL << exponent) * (size_t)(multiplier * 2u + 1u));
            } else {
                rom->prg_rom_size = (size_t)(((prg_msb << 8) | rom->header[4]) * 0x4000u);
            }

            uint16_t chr_msb = (rom->header[9] >> 4) & 0x0F;
            if (chr_msb == 0x0F) {
                uint8_t exponent = (rom->header[5] >> 2) & 0x3F;
                uint8_t multiplier = rom->header[5] & 0x03;
                rom->chr_rom_size = (size_t)((1ULL << exponent) * (size_t)(multiplier * 2u + 1u));
            } else {
                rom->chr_rom_size = (size_t)(((chr_msb << 8) | rom->header[5]) * 0x2000u);
            }
        } else {
            rom->prg_rom_size = (size_t)rom->header[4] * 0x4000u;
            rom->chr_rom_size = (size_t)rom->header[5] * 0x2000u;
        }

        rom->trainer_size = (rom->header[6] & 0x04u) ? 512u : 0u;
        rom->prg_rom_offset = 16u + rom->trainer_size;
        rom->chr_rom_offset = rom->prg_rom_offset + rom->prg_rom_size;
    } else if (memcmp(rom->header, "TNES", 4) == 0) {
        bool supported = true;
        uint8_t raw_mapper = rom->header[4];

        if (raw_mapper == 100u) {
            DEBUG_ERROR("TNES FDS images are not supported yet");
            goto error;
        }

        rom->format = ROM_FORMAT_TNES;
        rom->mapper_id = INES_MapTNESMapper(raw_mapper, &supported);
        if (!supported) {
            DEBUG_ERROR("TNES mapper %u is not supported", (unsigned)raw_mapper);
            goto error;
        }

        rom->prg_rom_size = (size_t)rom->header[5] * 8192u;
        rom->chr_rom_size = (size_t)rom->header[6] * 8192u;
        rom->trainer_size = 0u;
        rom->prg_rom_offset = 16u;
        rom->chr_rom_offset = rom->prg_rom_offset + rom->prg_rom_size;
    } else {
        DEBUG_ERROR("Invalid ROM header");
        goto error;
    }

    if (rom->prg_rom_size == 0) {
        DEBUG_ERROR("Invalid PRG ROM size");
        goto error;
    }

    if (rom->prg_rom_offset + rom->prg_rom_size > rom->size) {
        DEBUG_ERROR("PRG ROM data exceeds file size");
        goto error;
    }

    if (rom->chr_rom_size > 0 && rom->chr_rom_offset + rom->chr_rom_size > rom->size) {
        DEBUG_ERROR("CHR ROM data exceeds file size");
        goto error;
    }

    if (path) {
        rom->path = strdup(path);
        if (!rom->path) {
            DEBUG_ERROR("Memory allocation failed for ROM path");
            goto error;
        }

        const char *filename = strrchr(path, '/');
        if (!filename) {
            filename = strrchr(path, '\\');
        }

        rom->name = strdup(filename ? filename + 1 : path);
        if (!rom->name) {
            DEBUG_ERROR("Memory allocation failed for ROM name");
            goto error;
        }
    }

    return rom;

error:
    ROM_Destroy(rom);
    DEBUG_ERROR("Unable to load ROM");
    return NULL;
}

ROM *INES_LoadMemory(const uint8_t *data, size_t size)
{
    return INES_LoadFromBuffer((uint8_t *)data, size, NULL);
}

ROM *INES_LoadFile(const char *path)
{
    if (!path || path[0] == '\0') {
        DEBUG_ERROR("Invalid ROM path provided");
        return NULL;
    }

    FILE *file = fopen(path, "rb");
    if (!file) {
        DEBUG_ERROR("Failed to open ROM file %s", path);
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        DEBUG_ERROR("Failed to seek ROM file %s", path);
        return NULL;
    }

    long file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        DEBUG_ERROR("Failed to determine ROM file size for %s", path);
        return NULL;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        DEBUG_ERROR("Failed to rewind ROM file %s", path);
        return NULL;
    }

    if (file_size < 16) {
        fclose(file);
        DEBUG_ERROR("ROM file %s is too small to contain a header", path);
        return NULL;
    }

    size_t size = (size_t)file_size;
    uint8_t *data = malloc(size);
    if (!data) {
        fclose(file);
        DEBUG_ERROR("Memory allocation failed for ROM data");
        return NULL;
    }

    if (fread(data, 1, size, file) != size) {
        fclose(file);
        free(data);
        DEBUG_ERROR("Failed to read ROM data from file %s", path);
        return NULL;
    }

    fclose(file);

    ROM *rom = INES_LoadFromBuffer(data, size, path);
    free(data);
    return rom;
}
