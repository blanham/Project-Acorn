#ifndef I86_H
#define I86_H

/* This file is part of Project Acorn.
 * Licensed under the University of Illinois/NCSA Open Source License.
 * See LICENSE file in the project root for full license information.
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
