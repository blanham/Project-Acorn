/* This file is part of Project Acorn.
 * Licensed under the University of Illinois/NCSA Open Source License.
 * See LICENSE file in the project root for full license information.
 */

#include <stdio.h>
#include <stdbool.h>
#include "intel8086.h"

/* CPU Flags bits */
#define FLAGS_CF	0x001  /* Carry flag */
#define FLAGS_PF 	0x004  /* Parity flag */
#define FLAGS_AF	0x010  /* Auxiliary carry flag */
#define FLAGS_ZF 	0x040  /* Zero flag */
#define FLAGS_SF	0x080  /* Sign flag */
#define FLAGS_TF	0x100  /* Trap flag */
#define FLAGS_INT 	0x200  /* Interrupt enable flag */
#define FLAGS_DF	0x400  /* Direction flag */
#define FLAGS_OV    0x800  /* Overflow flag */

/* Flag test macro */
#define FLAG_TST(x)    ((x & cpu->flags) != 0)
/* Helper functions for flag manipulation */

static inline void jmpf(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint16_t new_ip = cpu_read_word(cpu, pc + 1);
	uint16_t new_cs = cpu_read_word(cpu, pc + 3);

	cpu->ip = new_ip;
	cpu->cs = new_cs;

	uint32_t target = cpu_get_pc(cpu);
	printf("JMP FAR %04X:%04X (0x%08X)", new_cs, new_ip, target);
}

static inline void set_flag(X86Cpu *cpu, uint16_t flag)
{
	cpu->flags |= flag;
}

static inline void clear_flag(X86Cpu *cpu, uint16_t flag)
{
	cpu->flags &= ~flag;
}


/* Flag checking functions */

static inline void chk_parity(X86Cpu *cpu, uint16_t data)
{
	/* Count number of 1 bits in low byte */
	uint8_t byte = data & 0xFF;
	byte ^= (byte >> 4);
	byte ^= (byte >> 2);
	byte ^= (byte >> 1);

	/* Parity flag is set if even number of 1 bits (result is 0) */
	if (byte & 0x1)
		clear_flag(cpu, FLAGS_PF);
	else
		set_flag(cpu, FLAGS_PF);
}

static inline void chk_zero(X86Cpu *cpu, uint16_t data)
{
	if (data == 0)
		set_flag(cpu, FLAGS_ZF);
	else
		clear_flag(cpu, FLAGS_ZF);
}

static inline void chk_sign(X86Cpu *cpu, uint16_t data, bool is_byte)
{
	/* Check sign bit (bit 7 for byte, bit 15 for word) */
	if (is_byte) {
		if (data & 0x80)
			set_flag(cpu, FLAGS_SF);
		else
			clear_flag(cpu, FLAGS_SF);
	} else {
		if (data & 0x8000)
			set_flag(cpu, FLAGS_SF);
		else
			clear_flag(cpu, FLAGS_SF);
	}
}
/* Conditional jump instructions (0x70 - 0x7F) */
static inline void jcc(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	int8_t displacement = (int8_t)cpu_read_byte(cpu, pc + 1);
	bool condition = false;

	/* Decode condition from opcode low nibble */
	switch (opcode & 0xF) {
		case 0x0:  /* JO - Jump if overflow */
			condition = FLAG_TST(FLAGS_OV);
			break;
		case 0x1:  /* JNO - Jump if not overflow */
			condition = !FLAG_TST(FLAGS_OV);
			break;
		case 0x2:  /* JB/JC - Jump if below/carry */
			condition = FLAG_TST(FLAGS_CF);
			break;
		case 0x3:  /* JNB/JNC - Jump if not below/not carry */
			condition = !FLAG_TST(FLAGS_CF);
			break;
		case 0x4:  /* JZ/JE - Jump if zero/equal */
			condition = FLAG_TST(FLAGS_ZF);
			break;
		case 0x5:  /* JNZ/JNE - Jump if not zero/not equal */
			condition = !FLAG_TST(FLAGS_ZF);
			break;
		case 0x6:  /* JBE/JNA - Jump if below or equal */
			condition = FLAG_TST(FLAGS_CF) || FLAG_TST(FLAGS_ZF);
			break;
		case 0x7:  /* JNBE/JA - Jump if not below or equal */
			condition = !FLAG_TST(FLAGS_CF) && !FLAG_TST(FLAGS_ZF);
			break;
		case 0x8:  /* JS - Jump if sign */
			condition = FLAG_TST(FLAGS_SF);
			break;
		case 0x9:  /* JNS - Jump if not sign */
			condition = !FLAG_TST(FLAGS_SF);
			break;
		case 0xA:  /* JP/JPE - Jump if parity/parity even */
			condition = FLAG_TST(FLAGS_PF);
			break;
		case 0xB:  /* JNP/JPO - Jump if not parity/parity odd */
			condition = !FLAG_TST(FLAGS_PF);
			break;
		case 0xC:  /* JL/JNGE - Jump if less */
			condition = FLAG_TST(FLAGS_SF) != FLAG_TST(FLAGS_OV);
			break;
		case 0xD:  /* JNL/JGE - Jump if not less */
			condition = FLAG_TST(FLAGS_SF) == FLAG_TST(FLAGS_OV);
			break;
		case 0xE:  /* JLE/JNG - Jump if less or equal */
			condition = FLAG_TST(FLAGS_ZF) ||
				(FLAG_TST(FLAGS_SF) != FLAG_TST(FLAGS_OV));
			break;
		case 0xF:  /* JNLE/JG - Jump if not less or equal */
			condition = !FLAG_TST(FLAGS_ZF) &&
				(FLAG_TST(FLAGS_SF) == FLAG_TST(FLAGS_OV));
			break;
	}

	fprintf(stderr, "Jcc 0x%02X (disp: %d) %s", opcode, displacement,
		condition ? "TAKEN" : "NOT TAKEN");

	/* Update IP: +2 for instruction, +displacement if condition met */
	cpu->ip += 2;
	if (condition)
		cpu->ip += displacement;
} 
/* Flag register instructions (0x9E, 0x9F) */

/* SAHF - Store AH into flags (0x9E) */
static inline void sahf(X86Cpu *cpu)
{
	printf("SAHF");
	/* Load SF, ZF, AF, PF, CF from AH (bits 7,6,4,2,0) */
	cpu->flags = (cpu->flags & 0xFF00) | cpu->ax.h;
	cpu->ip++;
}

/* LAHF - Load flags into AH (0x9F) */
static inline void lahf(X86Cpu *cpu)
{
	printf("LAHF");
	/* Store SF, ZF, AF, PF, CF into AH */
	cpu->ax.h = (cpu->flags & 0xFF);
	cpu->ip++;
}

/* MOV immediate to register (0xB0 - 0xBF) */
static inline void mov(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	uint8_t imm8 = cpu_read_byte(cpu, pc + 1);
	uint8_t reg = opcode & 0xF;

	fprintf(stderr, "MOV ");

	/* 0xB0-0xB7: MOV to 8-bit register */
	/* 0xB8-0xBF: MOV to 16-bit register */
	switch (reg) {
		case 0x0:  /* MOV AL, imm8 */
			cpu->ax.l = imm8;
			fprintf(stderr, "AL, 0x%02X", imm8);
			cpu->ip += 2;
			break;
		case 0x1:  /* MOV CL, imm8 */
			cpu->cx.l = imm8;
			fprintf(stderr, "CL, 0x%02X", imm8);
			cpu->ip += 2;
			break;
		case 0x2:  /* MOV DL, imm8 */
			cpu->dx.l = imm8;
			fprintf(stderr, "DL, 0x%02X", imm8);
			cpu->ip += 2;
			break;
		case 0x3:  /* MOV BL, imm8 */
			cpu->bx.l = imm8;
			fprintf(stderr, "BL, 0x%02X", imm8);
			cpu->ip += 2;
			break;
		case 0x4:  /* MOV AH, imm8 */
			cpu->ax.h = imm8;
			fprintf(stderr, "AH, 0x%02X", imm8);
			cpu->ip += 2;
			break;
		case 0x5:  /* MOV CH, imm8 */
			cpu->cx.h = imm8;
			fprintf(stderr, "CH, 0x%02X", imm8);
			cpu->ip += 2;
			break;
		case 0x6:  /* MOV DH, imm8 */
			cpu->dx.h = imm8;
			fprintf(stderr, "DH, 0x%02X", imm8);
			cpu->ip += 2;
			break;
		case 0x7:  /* MOV BH, imm8 */
			cpu->bx.h = imm8;
			fprintf(stderr, "BH, 0x%02X", imm8);
			cpu->ip += 2;
			break;
		case 0x8:  /* MOV AX, imm16 */
			cpu->ax.w = cpu_read_word(cpu, pc + 1);
			fprintf(stderr, "AX, 0x%04X", cpu->ax.w);
			cpu->ip += 3;
			break;
		case 0x9:  /* MOV CX, imm16 */
			cpu->cx.w = cpu_read_word(cpu, pc + 1);
			fprintf(stderr, "CX, 0x%04X", cpu->cx.w);
			cpu->ip += 3;
			break;
		case 0xA:  /* MOV DX, imm16 */
			cpu->dx.w = cpu_read_word(cpu, pc + 1);
			fprintf(stderr, "DX, 0x%04X", cpu->dx.w);
			cpu->ip += 3;
			break;
		case 0xB:  /* MOV BX, imm16 */
			cpu->bx.w = cpu_read_word(cpu, pc + 1);
			fprintf(stderr, "BX, 0x%04X", cpu->bx.w);
			cpu->ip += 3;
			break;
		case 0xC:  /* MOV SP, imm16 */
			cpu->sp = cpu_read_word(cpu, pc + 1);
			fprintf(stderr, "SP, 0x%04X", cpu->sp);
			cpu->ip += 3;
			break;
		case 0xD:  /* MOV BP, imm16 */
			cpu->bp = cpu_read_word(cpu, pc + 1);
			fprintf(stderr, "BP, 0x%04X", cpu->bp);
			cpu->ip += 3;
			break;
		case 0xE:  /* MOV SI, imm16 */
			cpu->si = cpu_read_word(cpu, pc + 1);
			fprintf(stderr, "SI, 0x%04X", cpu->si);
			cpu->ip += 3;
			break;
		case 0xF:  /* MOV DI, imm16 */
			cpu->di = cpu_read_word(cpu, pc + 1);
			fprintf(stderr, "DI, 0x%04X", cpu->di);
			cpu->ip += 3;
			break;
		default:
			fprintf(stderr, "ERROR: Invalid MOV opcode 0x%02X", opcode);
			cpu->running = 0;
			break;
	}
}
