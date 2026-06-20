#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include <stddef.h>

typedef struct NES NES;

#define CPU_FLAG_CARRY     (uint8_t)(1 << 0) // Carry Flag (C)
#define CPU_FLAG_ZERO      (uint8_t)(1 << 1) // Zero Flag (Z)
#define CPU_FLAG_INTERRUPT (uint8_t)(1 << 2) // Interrupt Disable Flag (I)
#define CPU_FLAG_DECIMAL   (uint8_t)(1 << 3) // Decimal Mode Flag (D) (unused in NES)
#define CPU_FLAG_BREAK     (uint8_t)(1 << 4) // Break Command Flag (B)
#define CPU_FLAG_UNUSED    (uint8_t)(1 << 5) // Unused flag (always 1)
#define CPU_FLAG_OVERFLOW  (uint8_t)(1 << 6) // Overflow Flag (V)
#define CPU_FLAG_NEGATIVE  (uint8_t)(1 << 7) // Negative Flag (N)

typedef struct CPU_Opcode {
    enum {
        CPU_MODE_IMPLIED,
        CPU_MODE_ACCUMULATOR,
        CPU_MODE_IMMEDIATE,
        CPU_MODE_ZERO_PAGE,
        CPU_MODE_ZERO_PAGE_X,
        CPU_MODE_ZERO_PAGE_Y,
        CPU_MODE_RELATIVE,
        CPU_MODE_ABSOLUTE,
        CPU_MODE_ABSOLUTE_X,
        CPU_MODE_ABSOLUTE_Y,
        CPU_MODE_INDIRECT,
        CPU_MODE_INDEXED_INDIRECT,
        CPU_MODE_INDIRECT_INDEXED
    } addressing_mode;
    const char mnemonic[5];
    uint8_t cycles;
} CPU_Opcode;

typedef struct CPU {
    uint8_t a;  // Accumulator
    uint8_t x;  // X Register
    uint8_t y;  // Y Register
    uint8_t sp; // Stack Pointer
    uint16_t pc; // Program Counter
    uint8_t status; // Processor Status

    uint64_t total_cycles;

    bool nmi_pending;

    NES* nes; // Pointer to the NES instance
} CPU;

extern CPU_Opcode cpu_opcodes[256];

CPU *CPU_Create(NES *nes);
void CPU_Reset(CPU* cpu);
int CPU_Step(CPU* cpu);
void CPU_Destroy(CPU* cpu);

void CPU_Interupt(CPU* cpu);
void CPU_NMI(CPU* cpu);
void CPU_IRQ(CPU* cpu);

// Flag Helpers
static inline void CPU_SetFlag(CPU *cpu, uint8_t flag, uint8_t value)
{
    if (value)
        cpu->status |= flag;
    else
        cpu->status &= ~flag;
}

static inline uint8_t CPU_GetFlag(CPU *cpu, uint8_t flag)
{
    return (cpu->status & flag);
}

#endif // CPU_H
