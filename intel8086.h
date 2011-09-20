#ifndef I86_H
#define I86_H

/* This file is part of Project Acorn.  Project Acorn is free software: you can
 * redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include <stdint.h>

typedef union {
	struct {
		uint8_t l;
		uint8_t h;
	};
	uint16_t w;
} ShortReg;	

typedef struct {
	uint8_t *ram;
	uint32_t pc;

	//registers
	ShortReg ax, bx, cx, dx;
	//Pointer, index
	uint16_t sp, bp, si, di; 
	uint16_t ip;
	uint16_t flags;	
	uint16_t cs, ds, ss, es; 

	int cycles;
	int running;
} X86Cpu;


void init_8086(X86Cpu *cpu);
void print_registers(X86Cpu *cpu);
void print_flags(X86Cpu *cpu);
int do_op(X86Cpu *cpu);

#define RAM_SIZE 0x100000
#if 0
#define PCnew (CS<<4)+IP
#define AH (AX>>8 & 0xFF)
#define AL (AX & 0xFF)
#define CH (CX>>8 & 0xFF) 
#define CL (CX & 0xFF)
#define IF (FLAGS>>9 & 0x1)
#define CF (FLAGS & 0x1)
#define PF (FLAGS>>2 & 0x1)
#define SF (FLAGS>>7 & 0x1)
#define ZF (FLAGS>>6 & 0x1)
#define OF (FLAGS>>11 & 0x1)


//macros
#define CHKZF(arg) if (arg == 0) FLAGS |= 0x40; else FLAGS &= 0xFFBF;
#define RDRAM(addr) RAM[CS]
#endif
#endif
