#include "cNES/rom.h"
#include "cNES/loaders/ines.h"

ROM *ROM_LoadFile(const char *path) 
{
    return INES_LoadFile(path);
}

ROM *ROM_LoadMemory(uint8_t *data, size_t size)
{
    return INES_LoadMemory(data, size);
}

void ROM_Destroy(ROM *rom) 
{
    if (rom) {
        if (rom->file) fclose(rom->file);
        if (rom->path) free(rom->path);
        if (rom->name) free(rom->name);
        if (rom->data) free(rom->data);
        free(rom);
    }
}