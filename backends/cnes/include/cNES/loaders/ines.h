#ifndef CNES_LOADERS_INES_H
#define CNES_LOADERS_INES_H

#include <stddef.h>
#include <stdint.h>

typedef struct ROM ROM;

ROM *INES_LoadFile(const char *path);
ROM *INES_LoadMemory(const uint8_t *data, size_t size);

#endif // CNES_LOADERS_INES_H