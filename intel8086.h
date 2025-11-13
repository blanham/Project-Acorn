#ifndef I86_H
#define I86_H

/* This file is part of Project Acorn.
 * Licensed under the University of Illinois/NCSA Open Source License.
 * See LICENSE file in the project root for full license information.
 */

#include <stdint.h>
#include <stdbool.h>

/* ShortReg - 16-bit register with high/low byte access
 * Allows accessing a 16-bit register as either:
 *   - Two 8-bit registers (h = high byte, l = low byte)
 *   - One 16-bit register (w = word)
 */
typedef union {
	struct {
		uint8_t l;  /* Low byte (e.g., AL from AX) */
		uint8_t h;  /* High byte (e.g., AH from AX) */
	};
	uint16_t w;     /* Full 16-bit word (e.g., AX) */
} ShortReg;

/* X86Cpu - Complete Intel 8086 CPU state */
typedef struct {
	uint8_t *ram;      /* Pointer to 1MB RAM */

	/* General purpose registers */
	ShortReg ax, bx, cx, dx;

	/* Pointer and index registers */
	uint16_t sp, bp, si, di;

	/* Instruction pointer */
	uint16_t ip;

	/* Flags register */
	uint16_t flags;

	/* Segment registers */
	uint16_t cs, ds, ss, es;

	/* Segment override prefix state */
	uint8_t seg_override;  /* 0=none, 1=ES, 2=CS, 3=SS, 4=DS */

	/* REP prefix state */
	uint8_t rep_prefix;    /* 0=none, 0xF2=REPNE/REPNZ, 0xF3=REP/REPE/REPZ */

	/* Emulator state */
	int cycles;
	int running;
} X86Cpu;

/* Memory size constant */
#define RAM_SIZE 0x100000

/* CPU initialization and control */
void init_8086(X86Cpu *cpu);
int do_op(X86Cpu *cpu);

/* CPU state output functions */
void print_registers(X86Cpu *cpu);
void print_flags(X86Cpu *cpu);
void print_cpu_state(X86Cpu *cpu);

/* Memory access functions */
uint8_t cpu_read_byte(X86Cpu *cpu, uint32_t addr);
uint16_t cpu_read_word(X86Cpu *cpu, uint32_t addr);
void cpu_write_byte(X86Cpu *cpu, uint32_t addr, uint8_t value);
void cpu_write_word(X86Cpu *cpu, uint32_t addr, uint16_t value);

/* Calculate physical address from segment:offset */
static inline uint32_t cpu_calc_addr(uint16_t segment, uint16_t offset)
{
	/* 8086 has 20-bit address bus, addresses wrap at 1MB */
	return (((uint32_t)segment << 4) + offset) & 0xFFFFF;
}

/* Get current program counter (physical address) */
static inline uint32_t cpu_get_pc(X86Cpu *cpu)
{
	return cpu_calc_addr(cpu->cs, cpu->ip);
}

#endif
