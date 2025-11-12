/* This file is part of Project Acorn.
 * Licensed under the University of Illinois/NCSA Open Source License.
 * See LICENSE file in the project root for full license information.
 */

#include "opcode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

/* Memory access functions */

uint8_t cpu_read_byte(X86Cpu *cpu, uint32_t addr)
{
	if (addr >= RAM_SIZE) {
		fprintf(stderr, "WARNING: Memory read out of bounds: 0x%08X\n", addr);
		return 0xFF;
	}
	return cpu->ram[addr];
}

uint16_t cpu_read_word(X86Cpu *cpu, uint32_t addr)
{
	if (addr + 1 >= RAM_SIZE) {
		fprintf(stderr, "WARNING: Memory read out of bounds: 0x%08X\n", addr);
		return 0xFFFF;
	}
	/* x86 is little-endian */
	return cpu->ram[addr] | (cpu->ram[addr + 1] << 8);
}

void cpu_write_byte(X86Cpu *cpu, uint32_t addr, uint8_t value)
{
	if (addr >= RAM_SIZE) {
		fprintf(stderr, "WARNING: Memory write out of bounds: 0x%08X\n", addr);
		return;
	}
	cpu->ram[addr] = value;
}

void cpu_write_word(X86Cpu *cpu, uint32_t addr, uint16_t value)
{
	if (addr + 1 >= RAM_SIZE) {
		fprintf(stderr, "WARNING: Memory write out of bounds: 0x%08X\n", addr);
		return;
	}
	/* x86 is little-endian */
	cpu->ram[addr] = value & 0xFF;
	cpu->ram[addr + 1] = (value >> 8) & 0xFF;
}

/* CPU initialization */

int undef_op(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	fprintf(stderr, "Undefined opcode 0x%02X @ 0x%08X\n",
		cpu_read_byte(cpu, pc), pc);
	return 1;
}

void init_8086(X86Cpu *cpu)
{
	memset(cpu, 0, sizeof(X86Cpu));

	/* Set reset vector values (8086 powers on at F000:FFF0) */
	cpu->ip = 0xFFF0;
	cpu->cs = 0xF000;
	cpu->sp = 0xFFFE;

	/* Allocate and clear RAM */
	cpu->ram = malloc(RAM_SIZE);
	if (cpu->ram == NULL) {
		fprintf(stderr, "FATAL: Failed to allocate CPU RAM\n");
		exit(1);
	}
	memset(cpu->ram, 0, RAM_SIZE);

	/* CPU starts in running state */
	cpu->running = 1;
	cpu->cycles = 0;
}

/* CPU state debugging output functions */

void print_registers(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	printf("PC: %04X:%04X (0x%08X) ", cpu->cs, cpu->ip, pc);
	printf("AX: %04X BX: %04X CX: %04X DX: %04X\n",
		cpu->ax.w, cpu->bx.w, cpu->cx.w, cpu->dx.w);
}

void print_flags(X86Cpu *cpu)
{
	uint16_t flags = cpu->flags;
	printf("FLAGS: %04X [", flags);

	/* Print flag names with uppercase = set, lowercase = clear */
	printf("%c", (flags & 0x800) ? 'O' : 'o');  /* Overflow */
	printf("%c", (flags & 0x400) ? 'D' : 'd');  /* Direction */
	printf("%c", (flags & 0x200) ? 'I' : 'i');  /* Interrupt */
	printf("%c", (flags & 0x100) ? 'T' : 't');  /* Trap */
	printf("%c", (flags & 0x080) ? 'S' : 's');  /* Sign */
	printf("%c", (flags & 0x040) ? 'Z' : 'z');  /* Zero */
	printf("%c", (flags & 0x010) ? 'A' : 'a');  /* Auxiliary carry */
	printf("%c", (flags & 0x004) ? 'P' : 'p');  /* Parity */
	printf("%c", (flags & 0x001) ? 'C' : 'c');  /* Carry */
	printf("]");
}

void print_cpu_state(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);

	printf("\n========== CPU STATE ==========\n");

	/* Segment registers */
	printf("Segment Registers:\n");
	printf("  CS: %04X  DS: %04X  SS: %04X  ES: %04X\n",
		cpu->cs, cpu->ds, cpu->ss, cpu->es);

	/* General purpose registers */
	printf("General Purpose Registers:\n");
	printf("  AX: %04X (AH: %02X AL: %02X)  BX: %04X (BH: %02X BL: %02X)\n",
		cpu->ax.w, cpu->ax.h, cpu->ax.l,
		cpu->bx.w, cpu->bx.h, cpu->bx.l);
	printf("  CX: %04X (CH: %02X CL: %02X)  DX: %04X (DH: %02X DL: %02X)\n",
		cpu->cx.w, cpu->cx.h, cpu->cx.l,
		cpu->dx.w, cpu->dx.h, cpu->dx.l);

	/* Pointer and index registers */
	printf("Pointer/Index Registers:\n");
	printf("  SP: %04X  BP: %04X  SI: %04X  DI: %04X\n",
		cpu->sp, cpu->bp, cpu->si, cpu->di);

	/* Instruction pointer and flags */
	printf("Instruction Pointer:\n");
	printf("  IP: %04X  (Physical: %08X)\n", cpu->ip, pc);

	printf("Flags: ");
	print_flags(cpu);
	printf("\n");

	/* Current instruction bytes */
	printf("Current Instruction:\n");
	printf("  %08X: %02X %02X %02X %02X %02X %02X %02X %02X\n",
		pc,
		cpu_read_byte(cpu, pc),
		cpu_read_byte(cpu, pc + 1),
		cpu_read_byte(cpu, pc + 2),
		cpu_read_byte(cpu, pc + 3),
		cpu_read_byte(cpu, pc + 4),
		cpu_read_byte(cpu, pc + 5),
		cpu_read_byte(cpu, pc + 6),
		cpu_read_byte(cpu, pc + 7));

	/* Execution state */
	printf("Emulator State:\n");
	printf("  Cycles: %d  Running: %d\n", cpu->cycles, cpu->running);

	printf("===============================\n\n");
}

/* Instruction execution dispatcher */

int do_op(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);

	switch (opcode) {
		/* ADD - Add (0x00-0x05) */
		case 0x00 ... 0x05:
			add_op(cpu);
			break;

		/* PUSH ES (0x06) */
		case 0x06:
			push_seg(cpu);
			break;

		/* POP ES (0x07) */
		case 0x07:
			pop_seg(cpu);
			break;

		/* OR - Logical OR (0x08-0x0D) */
		case 0x08 ... 0x0D:
			or_op(cpu);
			break;

		/* PUSH CS (0x0E) */
		case 0x0E:
			push_seg(cpu);
			break;

		/* POP CS (0x0F) - Undocumented but valid */
		case 0x0F:
			pop_seg(cpu);
			break;

		/* PUSH SS (0x16) */
		case 0x16:
			push_seg(cpu);
			break;

		/* POP SS (0x17) */
		case 0x17:
			pop_seg(cpu);
			break;

		/* PUSH DS (0x1E) */
		case 0x1E:
			push_seg(cpu);
			break;

		/* POP DS (0x1F) */
		case 0x1F:
			pop_seg(cpu);
			break;

		/* AND - Logical AND (0x20-0x25) */
		case 0x20 ... 0x25:
			and_op(cpu);
			break;

		/* SUB - Subtract (0x28-0x2D) */
		case 0x28 ... 0x2D:
			sub_op(cpu);
			break;

		/* XOR - Logical XOR (0x30-0x35) */
		case 0x30 ... 0x35:
			xor_op(cpu);
			break;

		/* CMP - Compare (0x38-0x3D) */
		case 0x38 ... 0x3D:
			cmp_op(cpu);
			break;

		/* INC - Increment 16-bit register (0x40-0x47) */
		case 0x40 ... 0x47:
			inc_reg16(cpu);
			break;

		/* DEC - Decrement 16-bit register (0x48-0x4F) */
		case 0x48 ... 0x4F:
			dec_reg16(cpu);
			break;

		/* PUSH r16 (0x50-0x57) - Push 16-bit register */
		case 0x50 ... 0x57:
			push_reg16(cpu);
			break;

		/* POP r16 (0x58-0x5F) - Pop to 16-bit register */
		case 0x58 ... 0x5F:
			pop_reg16(cpu);
			break;

		/* Conditional jumps (0x70-0x7F) */
		case 0x70 ... 0x7F:
			jcc(cpu);
			break;

		/* TEST - Logical compare (0x84-0x85) */
		case 0x84 ... 0x85:
			test_op(cpu);
			break;

		/* CALL far (0x9A) - Call far procedure */
		case 0x9A:
			call_far(cpu);
			break;

		/* PUSHF (0x9C) - Push flags register */
		case 0x9C:
			pushf(cpu);
			break;

		/* POPF (0x9D) - Pop flags register */
		case 0x9D:
			popf(cpu);
			break;

		/* Flag manipulation */
		case 0x9E:  /* SAHF - Store AH into flags */
			sahf(cpu);
			break;
		case 0x9F:  /* LAHF - Load flags into AH */
			lahf(cpu);
			break;

		/* TEST - Logical compare with immediate (0xA8-0xA9) */
		case 0xA8 ... 0xA9:
			test_op(cpu);
			break;

		/* MOV immediate to register (0xB0-0xBF) */
		case 0xB0 ... 0xBF:
			mov(cpu);
			break;

		/* RET near with pop (0xC2) */
		case 0xC2:
			ret_near_pop(cpu);
			break;

		/* RET near (0xC3) */
		case 0xC3:
			ret_near(cpu);
			break;

		/* RET far with pop (0xCA) */
		case 0xCA:
			ret_far_pop(cpu);
			break;

		/* RET far (0xCB) */
		case 0xCB:
			ret_far(cpu);
			break;

		/* INT 3 (0xCC) - Breakpoint */
		case 0xCC:
			int3(cpu);
			break;

		/* INT (0xCD) - Software interrupt */
		case 0xCD:
			int_op(cpu);
			break;

		/* INTO (0xCE) - Interrupt on overflow */
		case 0xCE:
			into(cpu);
			break;

		/* IRET (0xCF) - Return from interrupt */
		case 0xCF:
			iret(cpu);
			break;

		/* Shift/Rotate operations (0xD0-0xD3) */
		case 0xD0 ... 0xD3:
			shift_rotate_op(cpu);
			break;

		/* LOOPNZ/LOOPNE (0xE0) */
		case 0xE0:
			loopnz(cpu);
			break;

		/* LOOPZ/LOOPE (0xE1) */
		case 0xE1:
			loopz(cpu);
			break;

		/* LOOP (0xE2) */
		case 0xE2:
			loop_op(cpu);
			break;

		/* JCXZ (0xE3) */
		case 0xE3:
			jcxz(cpu);
			break;

		/* CALL near (0xE8) */
		case 0xE8:
			call_near(cpu);
			break;

		/* JMP near (0xE9) */
		case 0xE9:
			jmp_near(cpu);
			break;

		/* Jump far direct (0xEA) */
		case 0xEA:
			jmpf(cpu);
			break;

		/* JMP short (0xEB) */
		case 0xEB:
			jmp_short(cpu);
			break;

		/* CLI - Clear interrupt flag (0xFA) */
		case 0xFA:
			printf("%.2X CLI ", opcode);
			clear_flag(cpu, FLAGS_INT);
			cpu->ip++;
			break;

		/* Undefined opcode */
		default:
			undef_op(cpu);
			cpu->running = 0;
			break;
	}

	return 0;
}

