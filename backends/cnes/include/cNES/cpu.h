#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct NES NES;
typedef struct BUS BUS;
typedef struct PPU PPU;
typedef struct APU APU;

#define CPU_FLAG_CARRY     (uint8_t)(1 << 0) // Carry Flag (C)
#define CPU_FLAG_ZERO      (uint8_t)(1 << 1) // Zero Flag (Z)
#define CPU_FLAG_INTERRUPT (uint8_t)(1 << 2) // Interrupt Disable Flag (I)
#define CPU_FLAG_DECIMAL   (uint8_t)(1 << 3) // Decimal Mode Flag (D) (unused in NES)
#define CPU_FLAG_BREAK     (uint8_t)(1 << 4) // Break Command Flag (B)
#define CPU_FLAG_UNUSED    (uint8_t)(1 << 5) // Unused flag (always 1)
#define CPU_FLAG_OVERFLOW  (uint8_t)(1 << 6) // Overflow Flag (V)
#define CPU_FLAG_NEGATIVE  (uint8_t)(1 << 7) // Negative Flag (N)

typedef enum CPU_AddressingMode {
    CPU_MODE_IMP,  // Implied
    CPU_MODE_ACC,  // Accumulator
    CPU_MODE_IMM,  // Immediate
    CPU_MODE_ZP,   // Zero page
    CPU_MODE_ZPX,  // Zero page,X
    CPU_MODE_ZPY,  // Zero page,Y
    CPU_MODE_REL,  // Relative
    CPU_MODE_ABS,  // Absolute
    CPU_MODE_ABSX, // Absolute,X
    CPU_MODE_ABSY, // Absolute,Y
    CPU_MODE_IND,  // Indirect
    CPU_MODE_IZX,  // (Indirect,X)
    CPU_MODE_IZY   // (Indirect),Y
} CPU_AddressingMode;

typedef struct CPU {
    uint8_t a;  // Accumulator
    uint8_t x;  // X Register
    uint8_t y;  // Y Register
    uint8_t sp; // Stack Pointer
    uint16_t pc; // Program Counter
    uint8_t status; // Processor Status

    uint64_t total_cycles;
    uint64_t stall_cycles;

    bool nmi_pending; // Edge latched from the PPU at the end of an instruction
    bool irq_line;    // Level sampled from the APU at the end of an instruction

    bool dummy_read_dma;

    NES* nes;
    BUS* bus;
    PPU* ppu;
    APU* apu;
} CPU;

CPU *CPU_Create(NES *nes);
void CPU_Reset(CPU* cpu);
int CPU_Step(CPU* cpu);
void CPU_Destroy(CPU* cpu);

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

static inline void CPU_Stall(CPU *cpu, uint64_t cycles)
{
    cpu->total_cycles += cycles;
    cpu->stall_cycles += cycles;
}

void CPU_DmaHaltCycle(CPU *cpu);

#endif // CPU_H
