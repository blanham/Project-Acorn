#include <stdio.h>
#include <stdbool.h>
#include "intel8086.h"
#define FLAGS_CF	0x001
#define FLAGS_PF 	0x004
#define FLAGS_ZF 	0x040
#define FLAGS_SF	0x080
#define FLAGS_INT 	0x200
#define FLAGS_OV    0x800
#define FLAG_TST(x)    ((x & cpu->flags) != 0)
#define PC (cpu->ip | (cpu->cs << 4))
#define RAM_IMM cpu->ram[PC+1]
static inline void jmpf(X86Cpu *cpu)
{
	uint32_t new_cs;
	uint32_t new_ip;
	new_cs = (cpu->ram[PC+4] << 8) + cpu->ram[PC+3];
	new_ip = (cpu->ram[PC+2] << 8) + cpu->ram[PC+1];
	cpu->cs = new_cs;
	cpu->ip = new_ip;
	printf("JMP Direct to $0x%.6X", PC);
}

static inline void set_flag(X86Cpu *cpu, uint16_t flag)
{
	cpu->flags |= flag;
}

static inline void clear_flag(X86Cpu *cpu, uint16_t flag)
{
	cpu->flags &= ~flag;
}


//need flag checkers
static inline void chk_parity(X86Cpu *cpu, unsigned short data)
{
	data ^= (data >> 1);
	data ^= (data >> 2);
	data ^= (data >> 4);
	data ^= (data >> 8);
	data ^= (data >> 16);

	if (data & 0x1)
		clear_flag(cpu, FLAGS_PF);
	else 
		set_flag(cpu, FLAGS_PF);
	
}
static inline void chk_zero(X86Cpu *cpu, unsigned short data)
{
	if (data == 0)
		set_flag(cpu, FLAGS_ZF);
	else 
		clear_flag(cpu, FLAGS_ZF);
	

}
/* 0x70 - 0x7F */
static inline void jcc(X86Cpu *cpu)
{
	fprintf(stderr,"Jcc %x",cpu->ram[PC]);
	bool test;
	switch(cpu->ram[PC] & 0xF)
	{	
		case 0x3://JNC
			test = !FLAG_TST(FLAGS_CF);
			break;
		case 0x5://JNE
			test = !FLAG_TST(FLAGS_ZF);
			break;
		case 0x9://JNS
			test = !FLAG_TST(FLAGS_SF);
			break;
		case 0xB://JNP
			test = !FLAG_TST(FLAGS_PF);
			break;

		default:
		fprintf(stderr,"\nUnhandled conditional jump %x",cpu->ram[PC]);
		cpu->running = 0;
	}


	if(test)
		cpu->ip += RAM_IMM;
		
	cpu->ip +=2;
}
#define JMP1(condition) if(condition) PC += ram[PC+1] + 2; else PC +=2; 
/* 0x90 - 0x9f */
//9e
static inline void sahf(X86Cpu *cpu)
{
	printf("SAHF");
	cpu->flags = (cpu->flags & 0xF00) + cpu->ax.h;
	if(FLAG_TST(FLAGS_CF) ^ FLAG_TST(FLAGS_SF))
	{
		set_flag(cpu,FLAGS_OV);
	}
	cpu->ip++;
}
//9f
static inline void lahf(X86Cpu *cpu)
{
	printf("LAHF");
	cpu->ax.h = (cpu->flags & 0xFF);
	cpu->ip++;
}

/* 0xB0 - 0xBF */
static inline void mov(X86Cpu *cpu)
{
	uint8_t tmp = cpu->ram[PC] & 0xF;
	fprintf(stderr,"%x %x MOV",cpu->ram[PC],cpu->ram[PC+1] );
	switch(tmp)
	{
		case 0x4:
			cpu->ax.h = RAM_IMM; 
			break;

		default:
			fprintf(stderr, "unhandled mov %x",tmp);
			cpu->running = 0;




	}
	
	cpu->ip += 2;
}
