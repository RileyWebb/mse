#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "libmse/libmse_debug.h"
#include "cNES/nes.h"
#include "cNES/bus.h"
#include "cNES/ppu.h"

#include "cNES/cpu.h"

// Function pointers
typedef uint16_t (*addr_mode_func_ptr)(CPU *cpu, bool *page_crossed);
typedef void (*op_func_ptr)(CPU *cpu, uint16_t address, uint8_t *cycles_ref);

typedef struct {
    op_func_ptr        operation;
    addr_mode_func_ptr addressing_mode;
    uint8_t            cycles;
    uint8_t            add_cycles_on_page_cross;
} CPU_Instruction;

static inline void CPU_Push(CPU *cpu, uint8_t value)
{
    BUS_Write(cpu->nes, 0x0100 + cpu->sp, value);
    cpu->sp = (cpu->sp - 1) & 0xFF;
}

static inline uint8_t CPU_Pop(CPU *cpu)
{
    cpu->sp = (cpu->sp + 1) & 0xFF;
    return BUS_Read(cpu->nes, 0x0100 + cpu->sp);
}

static inline void CPU_Push16(CPU *cpu, uint16_t value)
{
    CPU_Push(cpu, (uint8_t)(value >> 8));   // High byte
    CPU_Push(cpu, (uint8_t)(value & 0xFF)); // Low byte
}

static inline uint16_t CPU_Pop16(CPU *cpu)
{
    uint8_t lo = CPU_Pop(cpu);
    uint8_t hi = CPU_Pop(cpu);
    return (uint16_t)lo | ((uint16_t)hi << 8);
}

static inline void CPU_UpdateZeroNegativeFlags(CPU *cpu, uint8_t value)
{
    CPU_SetFlag(cpu, CPU_FLAG_ZERO, value == 0);
    CPU_SetFlag(cpu, CPU_FLAG_NEGATIVE, (value & 0x80) != 0);
}

static inline bool CPU_IndexedPageCrossed(uint16_t address, uint8_t index, uint8_t *base_high_out)
{
    uint16_t base_address = address - index;
    uint8_t  base_high    = (uint8_t)(base_address >> 8);

    if (base_high_out != NULL) {
        *base_high_out = base_high;
    }

    return (address >> 8) != base_high;
}

// Addressing Modes
static inline uint16_t CPU_ADDR_IMP(CPU *cpu, bool *page_crossed)
{
    (void)cpu;

    *page_crossed = false;
    return cpu->pc;
}

static inline uint16_t CPU_ADDR_ACC(CPU *cpu, bool *page_crossed)
{
    (void)cpu;

    *page_crossed = false;
    return 0;
}

static inline uint16_t CPU_ADDR_IMM(CPU *cpu, bool *page_crossed)
{
    *page_crossed = false;
    return cpu->pc++;
}

static inline uint16_t CPU_ADDR_ZP(CPU *cpu, bool *page_crossed)
{
    *page_crossed = false;
    return BUS_Read(cpu->nes, cpu->pc++);
}

static inline uint16_t CPU_ADDR_ZPX(CPU *cpu, bool *page_crossed)
{
    *page_crossed = false;
    return (BUS_Read(cpu->nes, cpu->pc++) + cpu->x) & 0xFF;
}

static inline uint16_t CPU_ADDR_ZPY(CPU *cpu, bool *page_crossed)
{
    *page_crossed = false;
    return (BUS_Read(cpu->nes, cpu->pc++) + cpu->y) & 0xFF;
}

static inline uint16_t CPU_ADDR_REL(CPU *cpu, bool *page_crossed)
{
    *page_crossed        = false;
    uint16_t offset_addr = cpu->pc++;
    int8_t   offset      = (int8_t)BUS_Read(cpu->nes, offset_addr);
    return cpu->pc + (uint16_t)offset;
}

static inline uint16_t CPU_ADDR_ABS(CPU *cpu, bool *page_crossed)
{
    *page_crossed    = false;
    uint16_t address = BUS_Read16(cpu->nes, cpu->pc);
    cpu->pc += 2;
    return address;
}

static inline uint16_t CPU_ADDR_ABSX(CPU *cpu, bool *page_crossed)
{
    uint16_t base_addr = BUS_Read16(cpu->nes, cpu->pc);
    cpu->pc += 2;
    uint16_t final_addr = base_addr + cpu->x;
    *page_crossed       = ((base_addr & 0xFF00) != (final_addr & 0xFF00));
    return final_addr;
}

static inline uint16_t CPU_ADDR_ABSY(CPU *cpu, bool *page_crossed)
{
    uint16_t base_addr = BUS_Read16(cpu->nes, cpu->pc);
    cpu->pc += 2;
    uint16_t final_addr = base_addr + cpu->y;
    *page_crossed       = ((base_addr & 0xFF00) != (final_addr & 0xFF00));
    return final_addr;
}

static inline uint16_t CPU_ADDR_IND(CPU *cpu, bool *page_crossed)
{
    *page_crossed     = false;
    uint16_t ptr_addr = BUS_Read16(cpu->nes, cpu->pc);
    cpu->pc += 2;
    uint16_t effective_addr_lo = BUS_Read(cpu->nes, ptr_addr);
    uint16_t effective_addr_hi_addr;
    if ((ptr_addr & 0x00FF) == 0x00FF) {
        effective_addr_hi_addr = ptr_addr & 0xFF00;
    } else {
        effective_addr_hi_addr = ptr_addr + 1;
    }
    uint16_t effective_addr_hi = BUS_Read(cpu->nes, effective_addr_hi_addr);
    return (effective_addr_hi << 8) | effective_addr_lo;
}

static inline uint16_t CPU_ADDR_IZX(CPU *cpu, bool *page_crossed)
{
    *page_crossed              = false;
    uint8_t  zp_addr_base      = BUS_Read(cpu->nes, cpu->pc++);
    uint8_t  zp_addr           = (zp_addr_base + cpu->x) & 0xFF;
    uint16_t effective_addr_lo = BUS_Read(cpu->nes, zp_addr);
    uint16_t effective_addr_hi = BUS_Read(cpu->nes, (zp_addr + 1) & 0xFF);
    return (effective_addr_hi << 8) | effective_addr_lo;
}

static inline uint16_t CPU_ADDR_IZY(CPU *cpu, bool *page_crossed)
{
    uint8_t  zp_addr      = BUS_Read(cpu->nes, cpu->pc++);
    uint16_t base_addr_lo = BUS_Read(cpu->nes, zp_addr);
    uint16_t base_addr_hi = BUS_Read(cpu->nes, (zp_addr + 1) & 0xFF);
    uint16_t base_addr    = (base_addr_hi << 8) | base_addr_lo;
    uint16_t final_addr   = base_addr + cpu->y;
    *page_crossed         = ((base_addr & 0xFF00) != (final_addr & 0xFF00));
    return final_addr;
}

// Official Opcodes
static void CPU_OP_ADC(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;

    uint8_t  M    = BUS_Read(cpu->nes, address);
    uint16_t temp = (uint16_t)(cpu->a + M + (CPU_GetFlag(cpu, CPU_FLAG_CARRY) ? 1 : 0));
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, temp > 0xFF);
    CPU_SetFlag(cpu, CPU_FLAG_OVERFLOW, (~(cpu->a ^ M) & (cpu->a ^ (uint8_t)temp) & 0x80) != 0);
    cpu->a = (uint8_t)temp;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_AND(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;

    cpu->a &= BUS_Read(cpu->nes, address);
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_ASL_A(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (cpu->a & 0x80) != 0);
    cpu->a <<= 1;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_ASL_MEM(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;

    uint8_t M = BUS_Read(cpu->nes, address);
    BUS_Write(cpu->nes, address, M);
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (M & 0x80) != 0);
    M <<= 1;
    BUS_Write(cpu->nes, address, M);
    CPU_UpdateZeroNegativeFlags(cpu, M);
}

static void CPU_OP_BCC(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    if (!CPU_GetFlag(cpu, CPU_FLAG_CARRY)) {
        uint16_t old_pc = cpu->pc;
        cpu->pc         = address;
        *cycles_ref += 1;
        if ((old_pc & 0xFF00) != (cpu->pc & 0xFF00)) *cycles_ref += 1;
    }
}

static void CPU_OP_BCS(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    if (CPU_GetFlag(cpu, CPU_FLAG_CARRY)) {
        uint16_t old_pc = cpu->pc;
        cpu->pc         = address;
        *cycles_ref += 1;
        if ((old_pc & 0xFF00) != (cpu->pc & 0xFF00)) *cycles_ref += 1;
    }
}

static void CPU_OP_BEQ(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    if (CPU_GetFlag(cpu, CPU_FLAG_ZERO)) {
        uint16_t old_pc = cpu->pc;
        cpu->pc         = address;
        *cycles_ref += 1;
        if ((old_pc & 0xFF00) != (cpu->pc & 0xFF00)) *cycles_ref += 1;
    }
}

static void CPU_OP_BIT(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;

    uint8_t M = BUS_Read(cpu->nes, address);
    CPU_SetFlag(cpu, CPU_FLAG_ZERO, (cpu->a & M) == 0);
    CPU_SetFlag(cpu, CPU_FLAG_OVERFLOW, (M & 0x40) != 0);
    CPU_SetFlag(cpu, CPU_FLAG_NEGATIVE, (M & 0x80) != 0);
}

static void CPU_OP_BMI(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    if (CPU_GetFlag(cpu, CPU_FLAG_NEGATIVE)) {
        uint16_t old_pc = cpu->pc;
        cpu->pc         = address;
        *cycles_ref += 1;
        if ((old_pc & 0xFF00) != (cpu->pc & 0xFF00)) *cycles_ref += 1;
    }
}

static void CPU_OP_BNE(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    if (!CPU_GetFlag(cpu, CPU_FLAG_ZERO)) {
        uint16_t old_pc = cpu->pc;
        cpu->pc         = address;
        *cycles_ref += 1;
        if ((old_pc & 0xFF00) != (cpu->pc & 0xFF00)) *cycles_ref += 1;
    }
}

static void CPU_OP_BPL(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    if (!CPU_GetFlag(cpu, CPU_FLAG_NEGATIVE)) {
        uint16_t old_pc = cpu->pc;
        cpu->pc         = address;
        *cycles_ref += 1;
        if ((old_pc & 0xFF00) != (cpu->pc & 0xFF00)) *cycles_ref += 1;
    }
}

static void CPU_OP_BRK(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    cpu->pc++;
    CPU_Push16(cpu, cpu->pc);
    CPU_Push(cpu, cpu->status | CPU_FLAG_BREAK | CPU_FLAG_UNUSED);
    CPU_SetFlag(cpu, CPU_FLAG_INTERRUPT, true);
    cpu->pc = BUS_Read16(cpu->nes, 0xFFFE);
}

static void CPU_OP_BVC(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    if (!CPU_GetFlag(cpu, CPU_FLAG_OVERFLOW)) {
        uint16_t old_pc = cpu->pc;
        cpu->pc         = address;
        *cycles_ref += 1;
        if ((old_pc & 0xFF00) != (cpu->pc & 0xFF00)) *cycles_ref += 1;
    }
}

static void CPU_OP_BVS(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    if (CPU_GetFlag(cpu, CPU_FLAG_OVERFLOW)) {
        uint16_t old_pc = cpu->pc;
        cpu->pc         = address;
        *cycles_ref += 1;
        if ((old_pc & 0xFF00) != (cpu->pc & 0xFF00)) *cycles_ref += 1;
    }
}

static void CPU_OP_CLC(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    CPU_SetFlag(cpu, CPU_FLAG_CARRY, false);
}

static void CPU_OP_CLD(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    CPU_SetFlag(cpu, CPU_FLAG_DECIMAL, false);
}

static void CPU_OP_CLI(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    CPU_SetFlag(cpu, CPU_FLAG_INTERRUPT, false);
}

static void CPU_OP_CLV(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    CPU_SetFlag(cpu, CPU_FLAG_OVERFLOW, false);
}

static void CPU_OP_CMP(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;

    uint8_t M    = BUS_Read(cpu->nes, address);
    uint8_t temp = cpu->a - M;
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, cpu->a >= M);
    CPU_UpdateZeroNegativeFlags(cpu, temp);
}

static void CPU_OP_CPX(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;

    uint8_t M    = BUS_Read(cpu->nes, address);
    uint8_t temp = cpu->x - M;
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, cpu->x >= M);
    CPU_UpdateZeroNegativeFlags(cpu, temp);
}

static void CPU_OP_CPY(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;

    uint8_t M    = BUS_Read(cpu->nes, address);
    uint8_t temp = cpu->y - M;
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, cpu->y >= M);
    CPU_UpdateZeroNegativeFlags(cpu, temp);
}

static void CPU_OP_DEC(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;

    uint8_t M_orig = BUS_Read(cpu->nes, address);
    BUS_Write(cpu->nes, address, M_orig);
    uint8_t M = M_orig - 1;
    BUS_Write(cpu->nes, address, M);
    CPU_UpdateZeroNegativeFlags(cpu, M);
}

static void CPU_OP_DEX(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    cpu->x--;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->x);
}

static void CPU_OP_DEY(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    cpu->y--;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->y);
}

static void CPU_OP_EOR(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;

    cpu->a ^= BUS_Read(cpu->nes, address);
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_INC(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;

    uint8_t M_orig = BUS_Read(cpu->nes, address);
    BUS_Write(cpu->nes, address, M_orig);
    uint8_t M = M_orig + 1;
    BUS_Write(cpu->nes, address, M);
    CPU_UpdateZeroNegativeFlags(cpu, M);
}

static void CPU_OP_INX(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    cpu->x++;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->x);
}

static void CPU_OP_INY(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    cpu->y++;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->y);
}

static void CPU_OP_JMP(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;

    cpu->pc = address;
}

static void CPU_OP_JSR(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;

    CPU_Push16(cpu, cpu->pc - 1);
    cpu->pc = address;
}

static void CPU_OP_LDA(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;

    cpu->a = BUS_Read(cpu->nes, address);
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_LDX(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;

    cpu->x = BUS_Read(cpu->nes, address);
    CPU_UpdateZeroNegativeFlags(cpu, cpu->x);
}

static void CPU_OP_LDY(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;

    cpu->y = BUS_Read(cpu->nes, address);
    CPU_UpdateZeroNegativeFlags(cpu, cpu->y);
}

static void CPU_OP_LSR_A(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (cpu->a & 0x01) != 0);
    cpu->a >>= 1;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_LSR_MEM(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;

    uint8_t M = BUS_Read(cpu->nes, address);
    BUS_Write(cpu->nes, address, M);
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (M & 0x01) != 0);
    M >>= 1;
    BUS_Write(cpu->nes, address, M);
    CPU_UpdateZeroNegativeFlags(cpu, M);
}

static void CPU_OP_ORA(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;

    cpu->a |= BUS_Read(cpu->nes, address);
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_PHA(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    CPU_Push(cpu, cpu->a);
}

static void CPU_OP_PHP(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    CPU_Push(cpu, cpu->status | CPU_FLAG_BREAK | CPU_FLAG_UNUSED);
}

static void CPU_OP_PLA(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    cpu->a = CPU_Pop(cpu);
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_PLP(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    cpu->status = CPU_Pop(cpu);
    CPU_SetFlag(cpu, CPU_FLAG_UNUSED, true);
    CPU_SetFlag(cpu, CPU_FLAG_BREAK, false);
}

static void CPU_OP_ROL_A(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    bool old_c = CPU_GetFlag(cpu, CPU_FLAG_CARRY);
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (cpu->a & 0x80) != 0);
    cpu->a <<= 1;
    if (old_c) cpu->a |= 0x01;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_ROL_MEM(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;

    uint8_t M = BUS_Read(cpu->nes, address);
    BUS_Write(cpu->nes, address, M);
    bool old_c = CPU_GetFlag(cpu, CPU_FLAG_CARRY);
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (M & 0x80) != 0);
    M <<= 1;
    if (old_c) M |= 0x01;
    BUS_Write(cpu->nes, address, M);
    CPU_UpdateZeroNegativeFlags(cpu, M);
}

static void CPU_OP_ROR_A(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    bool old_c = CPU_GetFlag(cpu, CPU_FLAG_CARRY);
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (cpu->a & 0x01) != 0);
    cpu->a >>= 1;
    if (old_c) cpu->a |= 0x80;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_ROR_MEM(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;

    uint8_t M = BUS_Read(cpu->nes, address);
    BUS_Write(cpu->nes, address, M);
    bool old_c = CPU_GetFlag(cpu, CPU_FLAG_CARRY);
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (M & 0x01) != 0);
    M >>= 1;
    if (old_c) M |= 0x80;
    BUS_Write(cpu->nes, address, M);
    CPU_UpdateZeroNegativeFlags(cpu, M);
}

static void CPU_OP_RTI(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    cpu->status = CPU_Pop(cpu);
    CPU_SetFlag(cpu, CPU_FLAG_UNUSED, true);
    CPU_SetFlag(cpu, CPU_FLAG_BREAK, false);
    cpu->pc = CPU_Pop16(cpu);
}

static void CPU_OP_RTS(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    cpu->pc = CPU_Pop16(cpu) + 1;
}

static void CPU_OP_SBC(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;

    uint8_t  M    = BUS_Read(cpu->nes, address);
    uint16_t temp = (uint16_t)(cpu->a - M - (CPU_GetFlag(cpu, CPU_FLAG_CARRY) ? 0 : 1));
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, !(temp > 0xFF));
    CPU_SetFlag(cpu, CPU_FLAG_OVERFLOW, ((cpu->a ^ M) & (cpu->a ^ (uint8_t)temp) & 0x80) != 0);
    cpu->a = (uint8_t)temp;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_SEC(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    CPU_SetFlag(cpu, CPU_FLAG_CARRY, true);
}

static void CPU_OP_SED(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    CPU_SetFlag(cpu, CPU_FLAG_DECIMAL, true);
}

static void CPU_OP_SEI(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    CPU_SetFlag(cpu, CPU_FLAG_INTERRUPT, true);
}

static void CPU_OP_STA(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;

    BUS_Write(cpu->nes, address, cpu->a);
}

static void CPU_OP_STX(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;

    BUS_Write(cpu->nes, address, cpu->x);
}

static void CPU_OP_STY(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;

    BUS_Write(cpu->nes, address, cpu->y);
}

static void CPU_OP_TAX(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    cpu->x = cpu->a;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->x);
}

static void CPU_OP_TAY(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    cpu->y = cpu->a;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->y);
}

static void CPU_OP_TSX(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    cpu->x = cpu->sp;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->x);
}

static void CPU_OP_TXA(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    cpu->a = cpu->x;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_TXS(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    cpu->sp = cpu->x;
}

static void CPU_OP_TYA(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)address;
    (void)cycles_ref;

    cpu->a = cpu->y;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_NOP(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cpu;
    (void)cycles_ref;

    BUS_Read(cpu->nes, address);
}

// Unofficial Opcodes
static void CPU_OP_KIL(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cpu;
    (void)address;
    (void)cycles_ref;

    DEBUG_WARN("KIL instruction encountered at PC: %04X\n", cpu->pc - 1);
}

static void CPU_OP_SLO(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // ASL + ORA
{
    (void)cycles_ref;

    uint8_t M = BUS_Read(cpu->nes, address);
    BUS_Write(cpu->nes, address, M);
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (M & 0x80) != 0);
    M <<= 1;
    BUS_Write(cpu->nes, address, M);
    cpu->a |= M;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_RLA(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // ROL + AND
{
    (void)cycles_ref;

    uint8_t M = BUS_Read(cpu->nes, address);
    BUS_Write(cpu->nes, address, M);
    bool old_c = CPU_GetFlag(cpu, CPU_FLAG_CARRY);
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (M & 0x80) != 0);
    M <<= 1;
    if (old_c) M |= 0x01;
    BUS_Write(cpu->nes, address, M);
    cpu->a &= M;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_SRE(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // LSR + EOR
{
    (void)cycles_ref;
    
    uint8_t M = BUS_Read(cpu->nes, address);
    BUS_Write(cpu->nes, address, M);
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (M & 0x01) != 0);
    M >>= 1;
    BUS_Write(cpu->nes, address, M);
    cpu->a ^= M;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_RRA(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // ROR + ADC
{
    (void)cycles_ref;
    
    uint8_t M = BUS_Read(cpu->nes, address);
    BUS_Write(cpu->nes, address, M);
    bool old_c_ror = CPU_GetFlag(cpu, CPU_FLAG_CARRY);
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (M & 0x01) != 0); // Carry for ROR
    M >>= 1;
    if (old_c_ror) M |= 0x80;
    BUS_Write(cpu->nes, address, M);

    uint16_t temp = (uint16_t)(cpu->a + M + (CPU_GetFlag(cpu, CPU_FLAG_CARRY) ? 1 : 0)); // Use new carry from ROR
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, temp > 0xFF);                                       // Carry for ADC
    CPU_SetFlag(cpu, CPU_FLAG_OVERFLOW, (~(cpu->a ^ M) & (cpu->a ^ (uint8_t)temp) & 0x80) != 0);
    cpu->a = (uint8_t)temp;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_SAX(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // Store A & X
{
    (void)cycles_ref;
    
    BUS_Write(cpu->nes, address, cpu->a & cpu->x);
}

static void CPU_OP_LAX(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // LDA + LDX (or LDA + TAX)
{
    (void)cycles_ref;
    
    cpu->a = BUS_Read(cpu->nes, address);
    cpu->x = cpu->a;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Flags based on A (same as X)
}

static void CPU_OP_DCP(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // DEC + CMP
{
    (void)cycles_ref;

    uint8_t M_orig = BUS_Read(cpu->nes, address);
    BUS_Write(cpu->nes, address, M_orig);
    uint8_t M = M_orig - 1;
    BUS_Write(cpu->nes, address, M);

    uint8_t temp_cmp = cpu->a - M;
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, cpu->a >= M);
    CPU_UpdateZeroNegativeFlags(cpu, temp_cmp);
}

static void CPU_OP_ISC(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // INC + SBC
{
    (void)cycles_ref;

    uint8_t M_orig = BUS_Read(cpu->nes, address);
    BUS_Write(cpu->nes, address, M_orig);
    uint8_t M = M_orig + 1;
    BUS_Write(cpu->nes, address, M);

    uint16_t temp_sbc = (uint16_t)(cpu->a - M - (CPU_GetFlag(cpu, CPU_FLAG_CARRY) ? 0 : 1));
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, !(temp_sbc > 0xFF));
    CPU_SetFlag(cpu, CPU_FLAG_OVERFLOW, ((cpu->a ^ M) & (cpu->a ^ (uint8_t)temp_sbc) & 0x80) != 0);
    cpu->a = (uint8_t)temp_sbc;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_ANC(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // AND, C = N
{
    (void)cycles_ref;
    
    cpu->a &= BUS_Read(cpu->nes, address); // For ANC #imm, address is immediate value
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, CPU_GetFlag(cpu, CPU_FLAG_NEGATIVE));
}

static void CPU_OP_ALR(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // AND #imm, LSR A
{
    (void)cycles_ref;
    
    cpu->a &= BUS_Read(cpu->nes, address); // For ALR #imm, address is immediate value
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (cpu->a & 0x01) != 0);
    cpu->a >>= 1;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_ANE(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // ANE is also known as XAA
{
    (void)cycles_ref;
    // ANE / XAA: Highly unstable instruction.
    // Commonly emulated using a "magic" constant (often 0xEE).
    // Operation: A = (A | 0xEE) & X & M
    uint8_t M = BUS_Read(cpu->nes, address);
    cpu->a    = (cpu->a | 0xEE) & cpu->x & M;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_LXA(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // LXA / OAT
{
    (void)cycles_ref;
    // LXA / OAT: Highly unstable instruction, related to ANE.
    // Commonly emulated using a "magic" constant (often 0xEE).
    // Operation: A = X = (A | 0xEE) & M
    uint8_t M = BUS_Read(cpu->nes, address);
    cpu->a    = (cpu->a | 0xEE) & M;
    cpu->x    = cpu->a;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_ARR(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // AND #imm, ROR A, special flags
{
    (void)cycles_ref;
    
    cpu->a &= BUS_Read(cpu->nes, address); // For ARR #imm, address is immediate value
    bool old_c = CPU_GetFlag(cpu, CPU_FLAG_CARRY);

    bool new_c_for_ror = (cpu->a & 0x01) != 0; // This is NOT the final carry flag for ARR
    cpu->a >>= 1;
    if (old_c) cpu->a |= 0x80;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);

    CPU_SetFlag(cpu, CPU_FLAG_CARRY, (cpu->a & 0x40) != 0); // Bit 6 of result to Carry
    CPU_SetFlag(cpu, CPU_FLAG_OVERFLOW,
                ((cpu->a & 0x40) ^ ((cpu->a & 0x20) << 1)) != 0); // (Bit6 XOR Bit5) of result to Overflow
}

static void CPU_OP_SBX(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // (A & X) - imm -> X
{
    (void)cycles_ref;
    
    uint8_t  M    = BUS_Read(cpu->nes, address); // For SBX #imm, address is immediate value
    uint16_t temp = (cpu->a & cpu->x) - M;
    CPU_SetFlag(cpu, CPU_FLAG_CARRY, !((cpu->a & cpu->x) < M)); // (A&X) >= M
    cpu->x = (uint8_t)temp;
    CPU_UpdateZeroNegativeFlags(cpu, cpu->x);
}

static inline void CPU_SyaSxaAxa(CPU *cpu, uint16_t effective_address, uint8_t index_reg, uint8_t value_reg)
{
    // Infer the original base address's high byte (since effective_address = base_addr + index_reg)
    uint16_t base_high = (uint16_t)((effective_address - index_reg) >> 8);
    
    uint16_t uncorrected_address = (base_high << 8) | (effective_address & 0x00FF);
    bool page_crossed = (effective_address >> 8) != base_high;

    // Save cycle count to check if a DMA (like APU DMC or OAM DMA) 
    // interrupted the CPU during the dummy read cycle.
    uint64_t initial_cycles = cpu->total_cycles;
    
    // Perform the dummy read from the uncorrected address
    (void)BUS_Read(cpu->nes, uncorrected_address);

    // If total_cycles increased, it means DMA stole cycles during the read.
    // (Assuming your emulator adds the base opcode cycles in CPU_Step *after* the instruction runs)
    bool had_dma = (cpu->total_cycles > initial_cycles);

    uint16_t final_address = effective_address;
    if (page_crossed) {
        // When a page is crossed, the address written to is ANDed with the register
        final_address = (effective_address & 0x00FF) | (((effective_address >> 8) & value_reg) << 8);
    }

    // When a DMA interrupts the instruction right before the dummy read cycle,
    // the value written is not ANDed with the MSB of the address (+ 1).
    uint8_t value = had_dma ? value_reg : (uint8_t)(value_reg & (base_high + 1));

    BUS_Write(cpu->nes, final_address, value);
}

static void CPU_OP_SHX(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;
    // SHX Abs, Y
    CPU_SyaSxaAxa(cpu, address, cpu->y, cpu->x);
}

static void CPU_OP_SHY(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;
    // SHY Abs, X
    CPU_SyaSxaAxa(cpu, address, cpu->x, cpu->y);
}

static void CPU_OP_TAS(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;
    // TAS / SHS Abs, Y
    // Set SP = A & X
    cpu->sp = cpu->a & cpu->x;
    
    CPU_SyaSxaAxa(cpu, address, cpu->y, cpu->a & cpu->x);
}

static void CPU_OP_SHA(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;
    // SHA Abs, Y / SHA Ind, Y 
    // (The addressing mode logic correctly resolves `address` before calling this)
    CPU_SyaSxaAxa(cpu, address, cpu->y, cpu->a & cpu->x);
}

static void CPU_OP_LAS(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
    (void)cycles_ref;
    
    uint8_t M = BUS_Read(cpu->nes, address);
    cpu->a = cpu->x = cpu->sp = (M & cpu->sp);
    CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static const CPU_Instruction instructions[256] = {
    // 0x00 - 0x0F
    {CPU_OP_BRK, CPU_ADDR_IMP, 7, 0},
    {CPU_OP_ORA, CPU_ADDR_IZX, 6, 0},
    {CPU_OP_KIL, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_SLO, CPU_ADDR_IZX, 8, 0},
    {CPU_OP_NOP, CPU_ADDR_ZP, 3, 0},
    {CPU_OP_ORA, CPU_ADDR_ZP, 3, 0},
    {CPU_OP_ASL_MEM, CPU_ADDR_ZP, 5, 0},
    {CPU_OP_SLO, CPU_ADDR_ZP, 5, 0},
    {CPU_OP_PHP, CPU_ADDR_IMP, 3, 0},
    {CPU_OP_ORA, CPU_ADDR_IMM, 2, 0},
    {CPU_OP_ASL_A, CPU_ADDR_ACC, 2, 0},
    {CPU_OP_ANC, CPU_ADDR_IMM, 2, 0},
    {CPU_OP_NOP, CPU_ADDR_ABS, 4, 0},
    {CPU_OP_ORA, CPU_ADDR_ABS, 4, 0},
    {CPU_OP_ASL_MEM, CPU_ADDR_ABS, 6, 0},
    {CPU_OP_SLO, CPU_ADDR_ABS, 6, 0},

    // 0x10 - 0x1F
    {CPU_OP_BPL, CPU_ADDR_REL, 2, 0},
    {CPU_OP_ORA, CPU_ADDR_IZY, 5, 1},
    {CPU_OP_KIL, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_SLO, CPU_ADDR_IZY, 8, 0},
    {CPU_OP_NOP, CPU_ADDR_ZPX, 4, 0},
    {CPU_OP_ORA, CPU_ADDR_ZPX, 4, 0},
    {CPU_OP_ASL_MEM, CPU_ADDR_ZPX, 6, 0},
    {CPU_OP_SLO, CPU_ADDR_ZPX, 6, 0},
    {CPU_OP_CLC, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_ORA, CPU_ADDR_ABSY, 4, 1},
    {CPU_OP_NOP, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_SLO, CPU_ADDR_ABSY, 7, 0},
    {CPU_OP_NOP, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_ORA, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_ASL_MEM, CPU_ADDR_ABSX, 7, 0},
    {CPU_OP_SLO, CPU_ADDR_ABSX, 7, 0},

    // 0x20 - 0x2F
    {CPU_OP_JSR, CPU_ADDR_ABS, 6, 0},
    {CPU_OP_AND, CPU_ADDR_IZX, 6, 0},
    {CPU_OP_KIL, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_RLA, CPU_ADDR_IZX, 8, 0},
    {CPU_OP_BIT, CPU_ADDR_ZP, 3, 0},
    {CPU_OP_AND, CPU_ADDR_ZP, 3, 0},
    {CPU_OP_ROL_MEM, CPU_ADDR_ZP, 5, 0},
    {CPU_OP_RLA, CPU_ADDR_ZP, 5, 0},
    {CPU_OP_PLP, CPU_ADDR_IMP, 4, 0},
    {CPU_OP_AND, CPU_ADDR_IMM, 2, 0},
    {CPU_OP_ROL_A, CPU_ADDR_ACC, 2, 0},
    {CPU_OP_ANC, CPU_ADDR_IMM, 2, 0},
    {CPU_OP_BIT, CPU_ADDR_ABS, 4, 0},
    {CPU_OP_AND, CPU_ADDR_ABS, 4, 0},
    {CPU_OP_ROL_MEM, CPU_ADDR_ABS, 6, 0},
    {CPU_OP_RLA, CPU_ADDR_ABS, 6, 0},

    // 0x30 - 0x3F
    {CPU_OP_BMI, CPU_ADDR_REL, 2, 0},
    {CPU_OP_AND, CPU_ADDR_IZY, 5, 1},
    {CPU_OP_KIL, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_RLA, CPU_ADDR_IZY, 8, 0},
    {CPU_OP_NOP, CPU_ADDR_ZPX, 4, 0},
    {CPU_OP_AND, CPU_ADDR_ZPX, 4, 0},
    {CPU_OP_ROL_MEM, CPU_ADDR_ZPX, 6, 0},
    {CPU_OP_RLA, CPU_ADDR_ZPX, 6, 0},
    {CPU_OP_SEC, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_AND, CPU_ADDR_ABSY, 4, 1},
    {CPU_OP_NOP, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_RLA, CPU_ADDR_ABSY, 7, 0},
    {CPU_OP_NOP, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_AND, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_ROL_MEM, CPU_ADDR_ABSX, 7, 0},
    {CPU_OP_RLA, CPU_ADDR_ABSX, 7, 0},

    // 0x40 - 0x4F
    {CPU_OP_RTI, CPU_ADDR_IMP, 6, 0},
    {CPU_OP_EOR, CPU_ADDR_IZX, 6, 0},
    {CPU_OP_KIL, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_SRE, CPU_ADDR_IZX, 8, 0},
    {CPU_OP_NOP, CPU_ADDR_ZP, 3, 0},
    {CPU_OP_EOR, CPU_ADDR_ZP, 3, 0},
    {CPU_OP_LSR_MEM, CPU_ADDR_ZP, 5, 0},
    {CPU_OP_SRE, CPU_ADDR_ZP, 5, 0},
    {CPU_OP_PHA, CPU_ADDR_IMP, 3, 0},
    {CPU_OP_EOR, CPU_ADDR_IMM, 2, 0},
    {CPU_OP_LSR_A, CPU_ADDR_ACC, 2, 0},
    {CPU_OP_ALR, CPU_ADDR_IMM, 2, 0},
    {CPU_OP_JMP, CPU_ADDR_ABS, 3, 0},
    {CPU_OP_EOR, CPU_ADDR_ABS, 4, 0},
    {CPU_OP_LSR_MEM, CPU_ADDR_ABS, 6, 0},
    {CPU_OP_SRE, CPU_ADDR_ABS, 6, 0},

    // 0x50 - 0x5F
    {CPU_OP_BVC, CPU_ADDR_REL, 2, 0},
    {CPU_OP_EOR, CPU_ADDR_IZY, 5, 1},
    {CPU_OP_KIL, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_SRE, CPU_ADDR_IZY, 8, 0},
    {CPU_OP_NOP, CPU_ADDR_ZPX, 4, 0},
    {CPU_OP_EOR, CPU_ADDR_ZPX, 4, 0},
    {CPU_OP_LSR_MEM, CPU_ADDR_ZPX, 6, 0},
    {CPU_OP_SRE, CPU_ADDR_ZPX, 6, 0},
    {CPU_OP_CLI, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_EOR, CPU_ADDR_ABSY, 4, 1},
    {CPU_OP_NOP, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_SRE, CPU_ADDR_ABSY, 7, 0},
    {CPU_OP_NOP, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_EOR, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_LSR_MEM, CPU_ADDR_ABSX, 7, 0},
    {CPU_OP_SRE, CPU_ADDR_ABSX, 7, 0},

    // 0x60 - 0x6F
    {CPU_OP_RTS, CPU_ADDR_IMP, 6, 0},
    {CPU_OP_ADC, CPU_ADDR_IZX, 6, 0},
    {CPU_OP_KIL, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_RRA, CPU_ADDR_IZX, 8, 0},
    {CPU_OP_NOP, CPU_ADDR_ZP, 3, 0},
    {CPU_OP_ADC, CPU_ADDR_ZP, 3, 0},
    {CPU_OP_ROR_MEM, CPU_ADDR_ZP, 5, 0},
    {CPU_OP_RRA, CPU_ADDR_ZP, 5, 0},
    {CPU_OP_PLA, CPU_ADDR_IMP, 4, 0},
    {CPU_OP_ADC, CPU_ADDR_IMM, 2, 0},
    {CPU_OP_ROR_A, CPU_ADDR_ACC, 2, 0},
    {CPU_OP_ARR, CPU_ADDR_IMM, 2, 0},
    {CPU_OP_JMP, CPU_ADDR_IND, 5, 0},
    {CPU_OP_ADC, CPU_ADDR_ABS, 4, 0},
    {CPU_OP_ROR_MEM, CPU_ADDR_ABS, 6, 0},
    {CPU_OP_RRA, CPU_ADDR_ABS, 6, 0},

    // 0x70 - 0x7F
    {CPU_OP_BVS, CPU_ADDR_REL, 2, 0},
    {CPU_OP_ADC, CPU_ADDR_IZY, 5, 1},
    {CPU_OP_KIL, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_RRA, CPU_ADDR_IZY, 8, 0},
    {CPU_OP_NOP, CPU_ADDR_ZPX, 4, 0},
    {CPU_OP_ADC, CPU_ADDR_ZPX, 4, 0},
    {CPU_OP_ROR_MEM, CPU_ADDR_ZPX, 6, 0},
    {CPU_OP_RRA, CPU_ADDR_ZPX, 6, 0},
    {CPU_OP_SEI, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_ADC, CPU_ADDR_ABSY, 4, 1},
    {CPU_OP_NOP, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_RRA, CPU_ADDR_ABSY, 7, 0},
    {CPU_OP_NOP, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_ADC, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_ROR_MEM, CPU_ADDR_ABSX, 7, 0},
    {CPU_OP_RRA, CPU_ADDR_ABSX, 7, 0},

    // 0x80 - 0x8F
    {CPU_OP_NOP, CPU_ADDR_IMM, 2, 0},
    {CPU_OP_STA, CPU_ADDR_IZX, 6, 0},
    {CPU_OP_NOP, CPU_ADDR_IMM, 2, 0},
    {CPU_OP_SAX, CPU_ADDR_IZX, 6, 0},
    {CPU_OP_STY, CPU_ADDR_ZP, 3, 0},
    {CPU_OP_STA, CPU_ADDR_ZP, 3, 0},
    {CPU_OP_STX, CPU_ADDR_ZP, 3, 0},
    {CPU_OP_SAX, CPU_ADDR_ZP, 3, 0},
    {CPU_OP_DEY, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_NOP, CPU_ADDR_IMM, 2, 0},
    {CPU_OP_TXA, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_ANE, CPU_ADDR_IMM, 2, 0},
    {CPU_OP_STY, CPU_ADDR_ABS, 4, 0},
    {CPU_OP_STA, CPU_ADDR_ABS, 4, 0},
    {CPU_OP_STX, CPU_ADDR_ABS, 4, 0},
    {CPU_OP_SAX, CPU_ADDR_ABS, 4, 0},

    // 0x90 - 0x9F
    {CPU_OP_BCC, CPU_ADDR_REL, 2, 0},
    {CPU_OP_STA, CPU_ADDR_IZY, 6, 0},
    {CPU_OP_KIL, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_SHA, CPU_ADDR_IZY, 6, 0},
    {CPU_OP_STY, CPU_ADDR_ZPX, 4, 0},
    {CPU_OP_STA, CPU_ADDR_ZPX, 4, 0},
    {CPU_OP_STX, CPU_ADDR_ZPY, 4, 0},
    {CPU_OP_SAX, CPU_ADDR_ZPY, 4, 0},
    {CPU_OP_TYA, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_STA, CPU_ADDR_ABSY, 5, 0},
    {CPU_OP_TXS, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_TAS, CPU_ADDR_ABSY, 5, 0},
    {CPU_OP_SHY, CPU_ADDR_ABSX, 5, 0},
    {CPU_OP_STA, CPU_ADDR_ABSX, 5, 0},
    {CPU_OP_SHX, CPU_ADDR_ABSY, 5, 0},
    {CPU_OP_SHA, CPU_ADDR_ABSY, 5, 0},

    // 0xA0 - 0xAF
    {CPU_OP_LDY, CPU_ADDR_IMM, 2, 0},
    {CPU_OP_LDA, CPU_ADDR_IZX, 6, 0},
    {CPU_OP_LDX, CPU_ADDR_IMM, 2, 0},
    {CPU_OP_LAX, CPU_ADDR_IZX, 6, 0},
    {CPU_OP_LDY, CPU_ADDR_ZP, 3, 0},
    {CPU_OP_LDA, CPU_ADDR_ZP, 3, 0},
    {CPU_OP_LDX, CPU_ADDR_ZP, 3, 0},
    {CPU_OP_LAX, CPU_ADDR_ZP, 3, 0},
    {CPU_OP_TAY, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_LDA, CPU_ADDR_IMM, 2, 0},
    {CPU_OP_TAX, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_LXA, CPU_ADDR_IMM, 2, 0},
    {CPU_OP_LDY, CPU_ADDR_ABS, 4, 0},
    {CPU_OP_LDA, CPU_ADDR_ABS, 4, 0},
    {CPU_OP_LDX, CPU_ADDR_ABS, 4, 0},
    {CPU_OP_LAX, CPU_ADDR_ABS, 4, 0},

    // 0xB0 - 0xBF
    {CPU_OP_BCS, CPU_ADDR_REL, 2, 0},
    {CPU_OP_LDA, CPU_ADDR_IZY, 5, 1},
    {CPU_OP_KIL, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_LAX, CPU_ADDR_IZY, 5, 1},
    {CPU_OP_LDY, CPU_ADDR_ZPX, 4, 0},
    {CPU_OP_LDA, CPU_ADDR_ZPX, 4, 0},
    {CPU_OP_LDX, CPU_ADDR_ZPY, 4, 0},
    {CPU_OP_LAX, CPU_ADDR_ZPY, 4, 0},
    {CPU_OP_CLV, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_LDA, CPU_ADDR_ABSY, 4, 1},
    {CPU_OP_TSX, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_LAS, CPU_ADDR_ABSY, 4, 1},
    {CPU_OP_LDY, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_LDA, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_LDX, CPU_ADDR_ABSY, 4, 1},
    {CPU_OP_LAX, CPU_ADDR_ABSY, 4, 1},

    // 0xC0 - 0xCF
    {CPU_OP_CPY, CPU_ADDR_IMM, 2, 0},
    {CPU_OP_CMP, CPU_ADDR_IZX, 6, 0},
    {CPU_OP_NOP, CPU_ADDR_IMM, 2, 0},
    {CPU_OP_DCP, CPU_ADDR_IZX, 8, 0},
    {CPU_OP_CPY, CPU_ADDR_ZP, 3, 0},
    {CPU_OP_CMP, CPU_ADDR_ZP, 3, 0},
    {CPU_OP_DEC, CPU_ADDR_ZP, 5, 0},
    {CPU_OP_DCP, CPU_ADDR_ZP, 5, 0},
    {CPU_OP_INY, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_CMP, CPU_ADDR_IMM, 2, 0},
    {CPU_OP_DEX, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_SBX, CPU_ADDR_IMM, 2, 0},
    {CPU_OP_CPY, CPU_ADDR_ABS, 4, 0},
    {CPU_OP_CMP, CPU_ADDR_ABS, 4, 0},
    {CPU_OP_DEC, CPU_ADDR_ABS, 6, 0},
    {CPU_OP_DCP, CPU_ADDR_ABS, 6, 0},

    // 0xD0 - 0xDF
    {CPU_OP_BNE, CPU_ADDR_REL, 2, 0},
    {CPU_OP_CMP, CPU_ADDR_IZY, 5, 1},
    {CPU_OP_KIL, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_DCP, CPU_ADDR_IZY, 8, 0},
    {CPU_OP_NOP, CPU_ADDR_ZPX, 4, 0},
    {CPU_OP_CMP, CPU_ADDR_ZPX, 4, 0},
    {CPU_OP_DEC, CPU_ADDR_ZPX, 6, 0},
    {CPU_OP_DCP, CPU_ADDR_ZPX, 6, 0},
    {CPU_OP_CLD, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_CMP, CPU_ADDR_ABSY, 4, 1},
    {CPU_OP_NOP, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_DCP, CPU_ADDR_ABSY, 7, 0},
    {CPU_OP_NOP, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_CMP, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_DEC, CPU_ADDR_ABSX, 7, 0},
    {CPU_OP_DCP, CPU_ADDR_ABSX, 7, 0},

    // 0xE0 - 0xEF
    {CPU_OP_CPX, CPU_ADDR_IMM, 2, 0},
    {CPU_OP_SBC, CPU_ADDR_IZX, 6, 0},
    {CPU_OP_NOP, CPU_ADDR_IMM, 2, 0},
    {CPU_OP_ISC, CPU_ADDR_IZX, 8, 0},
    {CPU_OP_CPX, CPU_ADDR_ZP, 3, 0},
    {CPU_OP_SBC, CPU_ADDR_ZP, 3, 0},
    {CPU_OP_INC, CPU_ADDR_ZP, 5, 0},
    {CPU_OP_ISC, CPU_ADDR_ZP, 5, 0},
    {CPU_OP_INX, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_SBC, CPU_ADDR_IMM, 2, 0},
    {CPU_OP_NOP, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_SBC, CPU_ADDR_IMM, 2, 0},
    {CPU_OP_CPX, CPU_ADDR_ABS, 4, 0},
    {CPU_OP_SBC, CPU_ADDR_ABS, 4, 0},
    {CPU_OP_INC, CPU_ADDR_ABS, 6, 0},
    {CPU_OP_ISC, CPU_ADDR_ABS, 6, 0},

    // 0xF0 - 0xFF
    {CPU_OP_BEQ, CPU_ADDR_REL, 2, 0},
    {CPU_OP_SBC, CPU_ADDR_IZY, 5, 1},
    {CPU_OP_KIL, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_ISC, CPU_ADDR_IZY, 8, 0},
    {CPU_OP_NOP, CPU_ADDR_ZPX, 4, 0},
    {CPU_OP_SBC, CPU_ADDR_ZPX, 4, 0},
    {CPU_OP_INC, CPU_ADDR_ZPX, 6, 0},
    {CPU_OP_ISC, CPU_ADDR_ZPX, 6, 0},
    {CPU_OP_SED, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_SBC, CPU_ADDR_ABSY, 4, 1},
    {CPU_OP_NOP, CPU_ADDR_IMP, 2, 0},
    {CPU_OP_ISC, CPU_ADDR_ABSY, 7, 0},
    {CPU_OP_NOP, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_SBC, CPU_ADDR_ABSX, 4, 1},
    {CPU_OP_INC, CPU_ADDR_ABSX, 7, 0},
    {CPU_OP_ISC, CPU_ADDR_ABSX, 7, 0}};

CPU *CPU_Create(NES *nes)
{
    CPU *cpu = malloc(sizeof(CPU));
    if (!cpu) return NULL;
    memset(cpu, 0, sizeof(CPU));
    cpu->nes = nes;
    CPU_Reset(cpu);
    return cpu;
}

// https://www.nesdev.org/wiki/CPU_power_up_state
void CPU_Reset(CPU *cpu)
{
    cpu->a            = 0;
    cpu->x            = 0;
    cpu->y            = 0;
    cpu->pc           = BUS_Read16(cpu->nes, 0xFFFC);
    cpu->sp           = 0xFD;
    cpu->status       = CPU_FLAG_UNUSED | CPU_FLAG_INTERRUPT;
    cpu->total_cycles = 0;
    cpu->nmi_pending  = false;
}

void CPU_Destroy(CPU *cpu)
{
    if (cpu) free(cpu);
}

int CPU_Step(CPU *cpu)
{
    if (cpu->nmi_pending) {
        cpu->nmi_pending = false;
        CPU_NMI(cpu);
        return 7; // Vectoring to an interrupt takes 7 cycles
    }

    uint8_t                opcode = BUS_Read(cpu->nes, cpu->pc++);
    const CPU_Instruction *inst   = &instructions[opcode];

    bool     page_crossed_by_addr = false;
    uint16_t effective_address    = inst->addressing_mode(cpu, &page_crossed_by_addr);

    uint8_t current_opcode_cycles = inst->cycles;
    current_opcode_cycles += (uint8_t)(page_crossed_by_addr && inst->add_cycles_on_page_cross);

    if (page_crossed_by_addr && inst->add_cycles_on_page_cross) {
        BUS_Read(cpu->nes, effective_address - 0x0100); // Dummy Read
    }

    cpu->nmi_pending = cpu->nes->ppu->nmi_interrupt_line; 

    inst->operation(cpu, effective_address, &current_opcode_cycles);

    cpu->total_cycles += current_opcode_cycles;

    return current_opcode_cycles;
}

// Interupt Functions
void CPU_NMI(CPU *cpu)
{
    CPU_Push16(cpu, cpu->pc);
    CPU_Push(cpu, (uint8_t)(cpu->status & ~CPU_FLAG_BREAK) | CPU_FLAG_UNUSED);
    CPU_SetFlag(cpu, CPU_FLAG_INTERRUPT, true);
    cpu->total_cycles += 7;
    cpu->pc = BUS_Read16(cpu->nes, 0xFFFA);
}

void CPU_IRQ(CPU *cpu)
{
    if (!CPU_GetFlag(cpu, CPU_FLAG_INTERRUPT)) {
        CPU_Push16(cpu, cpu->pc);
        CPU_Push(cpu, (uint8_t)(cpu->status & ~CPU_FLAG_BREAK) | CPU_FLAG_UNUSED);
        CPU_SetFlag(cpu, CPU_FLAG_INTERRUPT, true);
        cpu->total_cycles += 7;
        cpu->pc = BUS_Read16(cpu->nes, 0xFFFE);
    }
}