#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "libmse/libmse_debug.h"
#include "cNES/nes.h"
#include "cNES/bus.h"
#include "cNES/ppu.h"
#include "cNES/apu.h"

#include "cNES/cpu.h"

#define CPU_ADDR_FLAG_PAGE_CROSSED 0x01u // Indexing carried into the high byte
#define CPU_ADDR_FLAG_INDEXED 0x02u		 // abs,X / abs,Y / (ind),Y

// Foward declarations
static inline void	  CPU_ServicePendingDMA(CPU *cpu);
static inline uint8_t CPU_ReadCycle(CPU *cpu, uint16_t address);
static void			  CPU_PollInterrupts(CPU *cpu);

// Addressing modes
static inline uint16_t CPU_ADDR_IMP(CPU *cpu, uint8_t *addr_flags);
static inline uint16_t CPU_ADDR_ACC(CPU *cpu, uint8_t *addr_flags);
static inline uint16_t CPU_ADDR_IMM(CPU *cpu, uint8_t *addr_flags);
static inline uint16_t CPU_ADDR_ZP(CPU *cpu, uint8_t *addr_flags);
static inline uint16_t CPU_ADDR_ZPX(CPU *cpu, uint8_t *addr_flags);
static inline uint16_t CPU_ADDR_ZPY(CPU *cpu, uint8_t *addr_flags);
static inline uint16_t CPU_ADDR_REL(CPU *cpu, uint8_t *addr_flags);
static inline uint16_t CPU_ADDR_ABS(CPU *cpu, uint8_t *addr_flags);
static inline uint16_t CPU_ADDR_ABSX(CPU *cpu, uint8_t *addr_flags);
static inline uint16_t CPU_ADDR_ABSY(CPU *cpu, uint8_t *addr_flags);
static inline uint16_t CPU_ADDR_IND(CPU *cpu, uint8_t *addr_flags);
static inline uint16_t CPU_ADDR_IZX(CPU *cpu, uint8_t *addr_flags);
static inline uint16_t CPU_ADDR_IZY(CPU *cpu, uint8_t *addr_flags);

// Operations
static void CPU_OP_ADC(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_ALR(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_ANC(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_AND(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_ANE(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_ARR(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_ASL_A(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_ASL_MEM(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_BCC(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_BCS(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_BEQ(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_BIT(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_BMI(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_BNE(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_BPL(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_BRK(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_BVC(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_BVS(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_CLC(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_CLD(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_CLI(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_CLV(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_CMP(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_CPX(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_CPY(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_DCP(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_DEC(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_DEX(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_DEY(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_EOR(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_INC(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_INX(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_INY(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_ISC(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_JMP(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_JSR(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_KIL(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_LAS(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_LAX(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_LDA(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_LDX(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_LDY(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_LSR_A(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_LSR_MEM(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_LXA(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_NOP(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_ORA(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_PHA(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_PHP(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_PLA(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_PLP(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_RLA(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_ROL_A(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_ROL_MEM(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_ROR_A(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_ROR_MEM(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_RRA(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_RTI(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_RTS(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_SAX(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_SBC(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_SBX(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_SEC(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_SED(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_SEI(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_SHA(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_SHX(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_SHY(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_SLO(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_SRE(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_STA(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_STX(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_STY(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_TAS(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_TAX(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_TAY(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_TSX(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_TXA(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_TXS(CPU *cpu, uint16_t address, uint8_t *cycles_ref);
static void CPU_OP_TYA(CPU *cpu, uint16_t address, uint8_t *cycles_ref);

#include "cpu_dispatch.inc"

static inline void CPU_ServicePendingDMA(CPU *cpu)
{
	APU *apu = cpu->apu;
	if (apu && apu->next_dmc_dma_cycle <= cpu->total_cycles) {
		APU_HandleDMCDMA(cpu->nes);
	}
}

void CPU_DmaHaltCycle(CPU *cpu)
{
	CPU_Stall(cpu, 1);
	CPU_ServicePendingDMA(cpu);
}

static inline void CPU_Tick(CPU *cpu)
{
	cpu->total_cycles++;
	CPU_ServicePendingDMA(cpu);
}

static inline uint8_t CPU_ReadCycle(CPU *cpu, uint16_t address)
{
	cpu->total_cycles++;
	CPU_ServicePendingDMA(cpu);
	return BUS_Read(cpu->nes, address);
}

static inline void CPU_WriteCycle(CPU *cpu, uint16_t address, uint8_t value)
{
	cpu->total_cycles++;
	CPU_ServicePendingDMA(cpu);
	BUS_Write(cpu->nes, address, value);
}

static inline uint16_t CPU_ReadCycle16(CPU *cpu, uint16_t address)
{
	uint8_t lo = CPU_ReadCycle(cpu, address);
	uint8_t hi = CPU_ReadCycle(cpu, (uint16_t)(address + 1));
	return (uint16_t)lo | ((uint16_t)hi << 8);
}

static inline void CPU_Push(CPU *cpu, uint8_t value)
{
	CPU_WriteCycle(cpu, 0x0100 + cpu->sp, value);
	cpu->sp = (cpu->sp - 1) & 0xFF;
}

static inline uint8_t CPU_Pop(CPU *cpu)
{
	cpu->sp = (cpu->sp + 1) & 0xFF;
	return CPU_ReadCycle(cpu, 0x0100 + cpu->sp);
}

static inline void CPU_Push16(CPU *cpu, uint16_t value)
{
	CPU_Push(cpu, (uint8_t)(value >> 8));	// High byte
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

// Addressing Modes
static inline uint16_t CPU_ADDR_IMP(CPU *cpu, uint8_t *addr_flags)
{
	*addr_flags = 0;
	return cpu->pc;
}

static inline uint16_t CPU_ADDR_ACC(CPU *cpu, uint8_t *addr_flags)
{
	(void)cpu;
	*addr_flags = 0;
	return 0;
}

static inline uint16_t CPU_ADDR_IMM(CPU *cpu, uint8_t *addr_flags)
{
	*addr_flags = 0;
	return cpu->pc++;
}

static inline uint16_t CPU_ADDR_ZP(CPU *cpu, uint8_t *addr_flags)
{
	*addr_flags = 0;
	return CPU_ReadCycle(cpu, cpu->pc++);
}

static inline uint16_t CPU_ADDR_ZPX(CPU *cpu, uint8_t *addr_flags)
{
	*addr_flags = 0;
	return (CPU_ReadCycle(cpu, cpu->pc++) + cpu->x) & 0xFF;
}

static inline uint16_t CPU_ADDR_ZPY(CPU *cpu, uint8_t *addr_flags)
{
	*addr_flags = 0;
	return (CPU_ReadCycle(cpu, cpu->pc++) + cpu->y) & 0xFF;
}

static inline uint16_t CPU_ADDR_REL(CPU *cpu, uint8_t *addr_flags)
{
	*addr_flags			 = 0;
	uint16_t offset_addr = cpu->pc++;
	int8_t	 offset		 = (int8_t)CPU_ReadCycle(cpu, offset_addr);
	return cpu->pc + (uint16_t)offset;
}

static inline uint16_t CPU_ADDR_ABS(CPU *cpu, uint8_t *addr_flags)
{
	*addr_flags		 = 0;
	uint16_t address = CPU_ReadCycle16(cpu, cpu->pc);
	cpu->pc += 2;
	return address;
}

static inline uint16_t CPU_ADDR_ABSX(CPU *cpu, uint8_t *addr_flags)
{
	uint16_t base_addr = CPU_ReadCycle16(cpu, cpu->pc);
	cpu->pc += 2;
	uint16_t final_addr = base_addr + cpu->x;
	*addr_flags			= CPU_ADDR_FLAG_INDEXED;
	if ((base_addr & 0xFF00) != (final_addr & 0xFF00)) {
		*addr_flags |= CPU_ADDR_FLAG_PAGE_CROSSED;
	}
	return final_addr;
}

static inline uint16_t CPU_ADDR_ABSY(CPU *cpu, uint8_t *addr_flags)
{
	uint16_t base_addr = CPU_ReadCycle16(cpu, cpu->pc);
	cpu->pc += 2;
	uint16_t final_addr = base_addr + cpu->y;
	*addr_flags			= CPU_ADDR_FLAG_INDEXED;
	if ((base_addr & 0xFF00) != (final_addr & 0xFF00)) {
		*addr_flags |= CPU_ADDR_FLAG_PAGE_CROSSED;
	}
	return final_addr;
}

static inline uint16_t CPU_ADDR_IND(CPU *cpu, uint8_t *addr_flags)
{
	*addr_flags		  = 0;
	uint16_t ptr_addr = CPU_ReadCycle16(cpu, cpu->pc);
	cpu->pc += 2;
	uint16_t effective_addr_lo = CPU_ReadCycle(cpu, ptr_addr);
	uint16_t effective_addr_hi_addr;
	if ((ptr_addr & 0x00FF) == 0x00FF) {
		effective_addr_hi_addr = ptr_addr & 0xFF00;
	} else {
		effective_addr_hi_addr = ptr_addr + 1;
	}
	uint16_t effective_addr_hi = CPU_ReadCycle(cpu, effective_addr_hi_addr);
	return (effective_addr_hi << 8) | effective_addr_lo;
}

static inline uint16_t CPU_ADDR_IZX(CPU *cpu, uint8_t *addr_flags)
{
	*addr_flags				   = 0;
	uint8_t	 zp_addr_base	   = CPU_ReadCycle(cpu, cpu->pc++);
	uint8_t	 zp_addr		   = (zp_addr_base + cpu->x) & 0xFF;
	uint16_t effective_addr_lo = CPU_ReadCycle(cpu, zp_addr);
	uint16_t effective_addr_hi = CPU_ReadCycle(cpu, (zp_addr + 1) & 0xFF);
	return (effective_addr_hi << 8) | effective_addr_lo;
}

static inline uint16_t CPU_ADDR_IZY(CPU *cpu, uint8_t *addr_flags)
{
	uint8_t	 zp_addr	  = CPU_ReadCycle(cpu, cpu->pc++);
	uint16_t base_addr_lo = CPU_ReadCycle(cpu, zp_addr);
	uint16_t base_addr_hi = CPU_ReadCycle(cpu, (zp_addr + 1) & 0xFF);
	uint16_t base_addr	  = (base_addr_hi << 8) | base_addr_lo;
	uint16_t final_addr	  = base_addr + cpu->y;
	*addr_flags			  = CPU_ADDR_FLAG_INDEXED;
	if ((base_addr & 0xFF00) != (final_addr & 0xFF00)) {
		*addr_flags |= CPU_ADDR_FLAG_PAGE_CROSSED;
	}
	return final_addr;
}

// Official Opcodes
static void CPU_OP_ADC(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
	(void)cycles_ref;

	uint8_t	 M	  = CPU_ReadCycle(cpu, address);
	uint16_t temp = (uint16_t)(cpu->a + M + (CPU_GetFlag(cpu, CPU_FLAG_CARRY) ? 1 : 0));
	CPU_SetFlag(cpu, CPU_FLAG_CARRY, temp > 0xFF);
	CPU_SetFlag(cpu, CPU_FLAG_OVERFLOW, (~(cpu->a ^ M) & (cpu->a ^ (uint8_t)temp) & 0x80) != 0);
	cpu->a = (uint8_t)temp;
	CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_AND(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
	(void)cycles_ref;

	cpu->a &= CPU_ReadCycle(cpu, address);
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

	uint8_t M = CPU_ReadCycle(cpu, address);
	CPU_WriteCycle(cpu, address, M);
	CPU_SetFlag(cpu, CPU_FLAG_CARRY, (M & 0x80) != 0);
	M <<= 1;
	CPU_WriteCycle(cpu, address, M);
	CPU_UpdateZeroNegativeFlags(cpu, M);
}

static inline void CPU_Branch(CPU *cpu, bool condition, uint16_t address, uint8_t *cycles_ref)
{
	if (!condition) {
		return;
	}

	uint16_t old_pc = cpu->pc;
	cpu->pc			= address;
	*cycles_ref += 1;
	if ((old_pc & 0xFF00) != (address & 0xFF00)) {
		*cycles_ref += 1;
	}
}

#define CPU_DEFINE_BRANCH(NAME, CONDITION)                                                                             \
	static void CPU_OP_##NAME(CPU *cpu, uint16_t address, uint8_t *cycles_ref)                                         \
	{                                                                                                                  \
		CPU_Branch(cpu, (CONDITION), address, cycles_ref);                                                             \
	}

CPU_DEFINE_BRANCH(BCC, !CPU_GetFlag(cpu, CPU_FLAG_CARRY))
CPU_DEFINE_BRANCH(BCS, CPU_GetFlag(cpu, CPU_FLAG_CARRY))
CPU_DEFINE_BRANCH(BEQ, CPU_GetFlag(cpu, CPU_FLAG_ZERO))
CPU_DEFINE_BRANCH(BNE, !CPU_GetFlag(cpu, CPU_FLAG_ZERO))
CPU_DEFINE_BRANCH(BMI, CPU_GetFlag(cpu, CPU_FLAG_NEGATIVE))
CPU_DEFINE_BRANCH(BPL, !CPU_GetFlag(cpu, CPU_FLAG_NEGATIVE))
CPU_DEFINE_BRANCH(BVS, CPU_GetFlag(cpu, CPU_FLAG_OVERFLOW))
CPU_DEFINE_BRANCH(BVC, !CPU_GetFlag(cpu, CPU_FLAG_OVERFLOW))

#undef CPU_DEFINE_BRANCH

static void CPU_OP_BIT(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
	(void)cycles_ref;

	uint8_t M = CPU_ReadCycle(cpu, address);
	CPU_SetFlag(cpu, CPU_FLAG_ZERO, (cpu->a & M) == 0);
	CPU_SetFlag(cpu, CPU_FLAG_OVERFLOW, (M & 0x40) != 0);
	CPU_SetFlag(cpu, CPU_FLAG_NEGATIVE, (M & 0x80) != 0);
}

static void CPU_OP_BRK(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
	(void)address;
	(void)cycles_ref;

	cpu->pc++;
	CPU_Push16(cpu, cpu->pc);
	CPU_Push(cpu, cpu->status | CPU_FLAG_BREAK | CPU_FLAG_UNUSED);
	CPU_SetFlag(cpu, CPU_FLAG_INTERRUPT, true);
	cpu->pc = CPU_ReadCycle16(cpu, 0xFFFE);
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

	uint8_t M	 = CPU_ReadCycle(cpu, address);
	uint8_t temp = cpu->a - M;
	CPU_SetFlag(cpu, CPU_FLAG_CARRY, cpu->a >= M);
	CPU_UpdateZeroNegativeFlags(cpu, temp);
}

static void CPU_OP_CPX(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
	(void)cycles_ref;

	uint8_t M	 = CPU_ReadCycle(cpu, address);
	uint8_t temp = cpu->x - M;
	CPU_SetFlag(cpu, CPU_FLAG_CARRY, cpu->x >= M);
	CPU_UpdateZeroNegativeFlags(cpu, temp);
}

static void CPU_OP_CPY(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
	(void)cycles_ref;

	uint8_t M	 = CPU_ReadCycle(cpu, address);
	uint8_t temp = cpu->y - M;
	CPU_SetFlag(cpu, CPU_FLAG_CARRY, cpu->y >= M);
	CPU_UpdateZeroNegativeFlags(cpu, temp);
}

static void CPU_OP_DEC(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
	(void)cycles_ref;

	uint8_t M_orig = CPU_ReadCycle(cpu, address);
	CPU_WriteCycle(cpu, address, M_orig);
	uint8_t M = M_orig - 1;
	CPU_WriteCycle(cpu, address, M);
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

	cpu->a ^= CPU_ReadCycle(cpu, address);
	CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_INC(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
	(void)cycles_ref;

	uint8_t M_orig = CPU_ReadCycle(cpu, address);
	CPU_WriteCycle(cpu, address, M_orig);
	uint8_t M = M_orig + 1;
	CPU_WriteCycle(cpu, address, M);
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

	cpu->a = CPU_ReadCycle(cpu, address);
	CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_LDX(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
	(void)cycles_ref;

	cpu->x = CPU_ReadCycle(cpu, address);
	CPU_UpdateZeroNegativeFlags(cpu, cpu->x);
}

static void CPU_OP_LDY(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
	(void)cycles_ref;

	cpu->y = CPU_ReadCycle(cpu, address);
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

	uint8_t M = CPU_ReadCycle(cpu, address);
	CPU_WriteCycle(cpu, address, M);
	CPU_SetFlag(cpu, CPU_FLAG_CARRY, (M & 0x01) != 0);
	M >>= 1;
	CPU_WriteCycle(cpu, address, M);
	CPU_UpdateZeroNegativeFlags(cpu, M);
}

static void CPU_OP_ORA(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
	(void)cycles_ref;

	cpu->a |= CPU_ReadCycle(cpu, address);
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

	uint8_t M = CPU_ReadCycle(cpu, address);
	CPU_WriteCycle(cpu, address, M);
	bool old_c = CPU_GetFlag(cpu, CPU_FLAG_CARRY);
	CPU_SetFlag(cpu, CPU_FLAG_CARRY, (M & 0x80) != 0);
	M <<= 1;
	if (old_c) M |= 0x01;
	CPU_WriteCycle(cpu, address, M);
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

	uint8_t M = CPU_ReadCycle(cpu, address);
	CPU_WriteCycle(cpu, address, M);
	bool old_c = CPU_GetFlag(cpu, CPU_FLAG_CARRY);
	CPU_SetFlag(cpu, CPU_FLAG_CARRY, (M & 0x01) != 0);
	M >>= 1;
	if (old_c) M |= 0x80;
	CPU_WriteCycle(cpu, address, M);
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

	uint8_t	 M	  = CPU_ReadCycle(cpu, address);
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

	CPU_WriteCycle(cpu, address, cpu->a);
}

static void CPU_OP_STX(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
	(void)cycles_ref;

	CPU_WriteCycle(cpu, address, cpu->x);
}

static void CPU_OP_STY(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
	(void)cycles_ref;

	CPU_WriteCycle(cpu, address, cpu->y);
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
	(void)cycles_ref;

	// Even the implied forms perform a read, which cartridges can observe.
	CPU_ReadCycle(cpu, address);
}

// Unofficial Opcodes
static void CPU_OP_KIL(CPU *cpu, uint16_t address, uint8_t *cycles_ref)
{
	(void)address;
	(void)cycles_ref;

	DEBUG_WARN("KIL instruction encountered at PC: %04X\n", cpu->pc - 1);
}

static void CPU_OP_SLO(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // ASL + ORA
{
	(void)cycles_ref;

	uint8_t M = CPU_ReadCycle(cpu, address);
	CPU_WriteCycle(cpu, address, M);
	CPU_SetFlag(cpu, CPU_FLAG_CARRY, (M & 0x80) != 0);
	M <<= 1;
	CPU_WriteCycle(cpu, address, M);
	cpu->a |= M;
	CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_RLA(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // ROL + AND
{
	(void)cycles_ref;

	uint8_t M = CPU_ReadCycle(cpu, address);
	CPU_WriteCycle(cpu, address, M);
	bool old_c = CPU_GetFlag(cpu, CPU_FLAG_CARRY);
	CPU_SetFlag(cpu, CPU_FLAG_CARRY, (M & 0x80) != 0);
	M <<= 1;
	if (old_c) M |= 0x01;
	CPU_WriteCycle(cpu, address, M);
	cpu->a &= M;
	CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_SRE(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // LSR + EOR
{
	(void)cycles_ref;

	uint8_t M = CPU_ReadCycle(cpu, address);
	CPU_WriteCycle(cpu, address, M);
	CPU_SetFlag(cpu, CPU_FLAG_CARRY, (M & 0x01) != 0);
	M >>= 1;
	CPU_WriteCycle(cpu, address, M);
	cpu->a ^= M;
	CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_RRA(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // ROR + ADC
{
	(void)cycles_ref;

	uint8_t M = CPU_ReadCycle(cpu, address);
	CPU_WriteCycle(cpu, address, M);
	bool old_c_ror = CPU_GetFlag(cpu, CPU_FLAG_CARRY);
	CPU_SetFlag(cpu, CPU_FLAG_CARRY, (M & 0x01) != 0); // Carry for ROR
	M >>= 1;
	if (old_c_ror) M |= 0x80;
	CPU_WriteCycle(cpu, address, M);

	uint16_t temp = (uint16_t)(cpu->a + M + (CPU_GetFlag(cpu, CPU_FLAG_CARRY) ? 1 : 0)); // Use new carry from ROR
	CPU_SetFlag(cpu, CPU_FLAG_CARRY, temp > 0xFF);										 // Carry for ADC
	CPU_SetFlag(cpu, CPU_FLAG_OVERFLOW, (~(cpu->a ^ M) & (cpu->a ^ (uint8_t)temp) & 0x80) != 0);
	cpu->a = (uint8_t)temp;
	CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_SAX(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // Store A & X
{
	(void)cycles_ref;

	CPU_WriteCycle(cpu, address, cpu->a & cpu->x);
}

static void CPU_OP_LAX(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // LDA + LDX (or LDA + TAX)
{
	(void)cycles_ref;

	cpu->a = CPU_ReadCycle(cpu, address);
	cpu->x = cpu->a;
	CPU_UpdateZeroNegativeFlags(cpu, cpu->a); // Flags based on A (same as X)
}

static void CPU_OP_DCP(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // DEC + CMP
{
	(void)cycles_ref;

	uint8_t M_orig = CPU_ReadCycle(cpu, address);
	CPU_WriteCycle(cpu, address, M_orig);
	uint8_t M = M_orig - 1;
	CPU_WriteCycle(cpu, address, M);

	uint8_t temp_cmp = cpu->a - M;
	CPU_SetFlag(cpu, CPU_FLAG_CARRY, cpu->a >= M);
	CPU_UpdateZeroNegativeFlags(cpu, temp_cmp);
}

static void CPU_OP_ISC(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // INC + SBC
{
	(void)cycles_ref;

	uint8_t M_orig = CPU_ReadCycle(cpu, address);
	CPU_WriteCycle(cpu, address, M_orig);
	uint8_t M = M_orig + 1;
	CPU_WriteCycle(cpu, address, M);

	uint16_t temp_sbc = (uint16_t)(cpu->a - M - (CPU_GetFlag(cpu, CPU_FLAG_CARRY) ? 0 : 1));
	CPU_SetFlag(cpu, CPU_FLAG_CARRY, !(temp_sbc > 0xFF));
	CPU_SetFlag(cpu, CPU_FLAG_OVERFLOW, ((cpu->a ^ M) & (cpu->a ^ (uint8_t)temp_sbc) & 0x80) != 0);
	cpu->a = (uint8_t)temp_sbc;
	CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_ANC(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // AND, C = N
{
	(void)cycles_ref;

	cpu->a &= CPU_ReadCycle(cpu, address); // For ANC #imm, address is immediate value
	CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
	CPU_SetFlag(cpu, CPU_FLAG_CARRY, CPU_GetFlag(cpu, CPU_FLAG_NEGATIVE));
}

static void CPU_OP_ALR(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // AND #imm, LSR A
{
	(void)cycles_ref;

	cpu->a &= CPU_ReadCycle(cpu, address); // For ALR #imm, address is immediate value
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
	uint8_t M = CPU_ReadCycle(cpu, address);
	cpu->a	  = (cpu->a | 0xEE) & cpu->x & M;
	CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_LXA(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // LXA / OAT
{
	(void)cycles_ref;
	// LXA / OAT: Highly unstable instruction, related to ANE.
	// Commonly emulated using a "magic" constant (often 0xEE).
	// Operation: A = X = (A | 0xEE) & M
	uint8_t M = CPU_ReadCycle(cpu, address);
	cpu->a	  = (cpu->a | 0xEE) & M;
	cpu->x	  = cpu->a;
	CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

static void CPU_OP_ARR(CPU *cpu, uint16_t address, uint8_t *cycles_ref) // AND #imm, ROR A, special flags
{
	(void)cycles_ref;

	cpu->a &= CPU_ReadCycle(cpu, address); // For ARR #imm, address is immediate value
	bool old_c = CPU_GetFlag(cpu, CPU_FLAG_CARRY);

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

	uint8_t	 M	  = CPU_ReadCycle(cpu, address); // For SBX #imm, address is immediate value
	uint16_t temp = (cpu->a & cpu->x) - M;
	CPU_SetFlag(cpu, CPU_FLAG_CARRY, !((cpu->a & cpu->x) < M)); // (A&X) >= M
	cpu->x = (uint8_t)temp;
	CPU_UpdateZeroNegativeFlags(cpu, cpu->x);
}

static inline void CPU_SyaSxaAxa(CPU *cpu, uint16_t effective_address, uint8_t index_reg, uint8_t value_reg)
{
	// Infer the original base address's high byte (since effective_address = base_addr + index_reg)
	uint16_t base_high = (uint16_t)((effective_address - index_reg) >> 8);

	bool page_crossed = (effective_address >> 8) != base_high;

	// CPU_Step has already performed the un-carried dummy read for every indexed
	// store, and recorded whether a DMA stole cycles during it. This can no
	// longer be inferred from total_cycles here: the clock now advances on every
	// bus access, so a plain read moves it too.
	bool had_dma = cpu->dummy_read_dma;

	uint16_t final_address = effective_address;
	if (page_crossed) {
		// When a page is crossed, the address written to is ANDed with the register
		final_address = (effective_address & 0x00FF) | (((effective_address >> 8) & value_reg) << 8);
	}

	// When a DMA interrupts the instruction right before the dummy read cycle,
	// the value written is not ANDed with the MSB of the address (+ 1).
	uint8_t value = had_dma ? value_reg : (uint8_t)(value_reg & (base_high + 1));

	CPU_WriteCycle(cpu, final_address, value);
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

	uint8_t M = CPU_ReadCycle(cpu, address);
	cpu->a = cpu->x = cpu->sp = (M & cpu->sp);
	CPU_UpdateZeroNegativeFlags(cpu, cpu->a);
}

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
	// Rebind before anything else: the first reset runs from CPU_Create, before
	// the PPU and APU exist, and the second from NES_Reset once they all do.
	cpu->bus = cpu->nes ? cpu->nes->bus : NULL;
	cpu->ppu = cpu->nes ? cpu->nes->ppu : NULL;
	cpu->apu = cpu->nes ? cpu->nes->apu : NULL;

	cpu->total_cycles	= 0;
	cpu->stall_cycles	= 0;
	cpu->nmi_pending	= false;
	cpu->irq_line		= false;
	cpu->dummy_read_dma = false;

	cpu->a		= 0;
	cpu->x		= 0;
	cpu->y		= 0;
	cpu->sp		= 0xFD;
	cpu->status = CPU_FLAG_UNUSED | CPU_FLAG_INTERRUPT;

	cpu->pc = BUS_Read16(cpu->nes, 0xFFFC);
}

void CPU_Destroy(CPU *cpu)
{
	if (cpu) free(cpu);
}

static void CPU_PollInterrupts(CPU *cpu)
{
	PPU *ppu = cpu->ppu;
	if (ppu && ppu->nmi_interrupt_line) {
		cpu->nmi_pending		= true;
		ppu->nmi_interrupt_line = false;
	}

	APU *apu	  = cpu->apu;
	bool apu_irq  = apu && (apu->frame_irq_flag || apu->dmc.irq_flag);
	bool cart_irq = cpu->bus && cpu->bus->irq_asserted;

	cpu->irq_line = apu_irq || cart_irq;
}

// Interupt Functions
void CPU_NMI(CPU *cpu)
{
	CPU_Tick(cpu);
	CPU_Tick(cpu);
	CPU_Push16(cpu, cpu->pc);
	CPU_Push(cpu, (uint8_t)(cpu->status & ~CPU_FLAG_BREAK) | CPU_FLAG_UNUSED);
	CPU_SetFlag(cpu, CPU_FLAG_INTERRUPT, true);
	cpu->pc = CPU_ReadCycle16(cpu, 0xFFFA);
}

void CPU_IRQ(CPU *cpu)
{
	if (CPU_GetFlag(cpu, CPU_FLAG_INTERRUPT)) return;

	CPU_Tick(cpu);
	CPU_Tick(cpu);
	CPU_Push16(cpu, cpu->pc);
	CPU_Push(cpu, (uint8_t)(cpu->status & ~CPU_FLAG_BREAK) | CPU_FLAG_UNUSED);
	CPU_SetFlag(cpu, CPU_FLAG_INTERRUPT, true);
	cpu->pc = CPU_ReadCycle16(cpu, 0xFFFE);
}
