#include "5150emu.h"
#include "opcode.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

//tracer enable/disable
#define TRACE
#ifdef TRACE
#define DPRINTF()
#else
#define DPRINTF()
#endif
#define SET_PC(x) \
	cpu->cs = x & 0xFFFF;\
	cpu->ip = (x >> 4) & 0xFFFF;
/* Ok, need to structure instructions better:
 * case 0xXX:
 *	InstructionMode(ram[PC+1]); //probably need R/W/M
 *	//the above will go ahead and increase the PC for us
 * 	//do what ever the op code is
 *	
 *
 *
 */
int (*opcodes[0x100])(X86Cpu *);
int undef_op(X86Cpu *cpu)
{

	fprintf(stderr,"Undefined opcode %x @ %x",cpu->ram[PC],PC);
	return 1;


}
void init_8086(X86Cpu *cpu)
{
	int i;
	for(i = 0; i < 0x100; i++)
	{
		opcodes[i] = undef_op;
	}
//	opcodes[0xEA] = jmpf;
	
	memset(cpu, 0, sizeof(X86Cpu));
	cpu->ip = 0xFFF0;
	cpu->cs = 0xF000;
	cpu->sp = 0xFFFE;
	cpu->ram = malloc(RAM_SIZE);
	memset(cpu->ram, 0, RAM_SIZE);
}

void print_registers(X86Cpu *cpu)
{
	printf(" PC: 0x%x AX: %.4X, BX: %.4X, CX: %.4X, DX: %.4X FL: %.4X\n",
		 PC,cpu->ax.w, cpu->bx.w, cpu->cx.w, cpu->dx.w,cpu->flags);
}


void print_flags(X86Cpu *cpu)
{
    printf("FLAGS:");
	uint16_t FLAGS = cpu->flags;
    if (FLAGS & 0x800)printf("O");
    else printf("o");
    if (FLAGS & 0x400)printf("D");
    else printf("d");
    if (FLAGS & 0x200)printf("I");
    else printf("i");
    if (FLAGS & 0x100)printf("T");
    else printf("t");
    if (FLAGS & 0x80)printf("S");
    else printf("s");
    if (FLAGS & 0x40)printf("Z");
    else printf("z");
    if (FLAGS & 0x10)printf("A");
    else printf("a");
    if (FLAGS & 0x04)printf("P");
    else printf("p");
    if (FLAGS & 0x01)printf("C");
    else printf("c");
}

//from GRP2, SHL w/ consant 1
void SHL1(unsigned char tmp)
{

	switch (tmp&0x7)
	{
		case 0:
			printf("SHL AL,1");
		//	FLAGS = (FLAGS & 0xFFFE) | ((AL<<1) & 0x01);
		//	AX = (AH<<8) + (AL<<1);
		//	ChkPF(AX);
		//	CHKZF(AX);
		//	if(CF^SF)
			{
		//		FLAGS |= 1<<11;
			}	
			break;
		default:
			printf("Missing SHL1 Arg!\n");
			exit(1);
		
	}

}



int do_op(X86Cpu *cpu) 
{
	uint8_t op = cpu->ram[PC];
	switch (cpu->ram[PC])//cpu->ram[0xFFFF0])
	{
//	case 0x32:
//			printf("XOR");
			//lets trial some mod reg r/w shit
		//	if(!(ram[PC+1]&0xC0)) exit(1);
		//	switch (ram[PC+1]&0x3F)
			{
		//		case 0x24:
		//			printf(" AH,AH");
	//				AX = ((AX&0xFF00)^(AX&0xFF00)) + AL;
	//				CHKZF(AX);
	//				ChkPF(AX);
					//check if neg
	//				if(AX&0x8000) FLAGS |= 0x80;
	//				else FLAGS &= 0xF7F;
					//zeros Z and O
	//				FLAGS &= 0x7FE;
		//			break;
		//		default:
		//			break;
					
			}
		//	printf("2nd: %.2X",ram[PC+1]);
		//	PC += 2;
	//		break;	
			
		//Jumps
	case 0x70 ... 0x7F:	jcc(cpu);	break;
			
	case 0x9E:			sahf(cpu);	break;	
	case 0x9F: 			lahf(cpu); 	break;


	case 0xB0 ... 0xBF:	mov(cpu);	break;

	//	case 0xD0:
			//all have 1 as constant
			//middle 3 bits determine instruction
	//		tmp = (ram[PC+1]&0x38)>>3;
	//		if (!(ram[PC+1]&0xC0)) exit(1);
			//switch (tmp)
			{
			//	case 4:
		//		SHL1(ram[PC+1]);
			//	break;
			//	default:
			//	printf("Unimplemented  GRP2 OP code\n");
			//	exit(1);
			}
	//		PC += 2;
	//		break;
	//	case 0xD2:
			//fix this to match 0xD0
	//		printf("SHR AH,CL");
		//	if(ram[PC+1] == 0xEC)
	//		{
	//			FLAGS = (FLAGS & 0xFFFE) | ((AH>>(CL-1)) & 0x01);
	//			AX = AL + ((AH>>CL)<<8);
	//			ChkPF(AX);
	//			CHKZF(AX);
	//			if(CF^SF)
	//			{
	//				FLAGS |= 1<<11;
	//			}	
	//			PC +=2;
		//	}else exit(1);
	//		break;
		case 0xEA:
			jmpf(cpu);
			break;
		//Flags
		case 0xFA:
			printf("%.2x CLI ",op);
			clear_flag(cpu, FLAGS_INT);
			cpu->ip++;
			break;
		default:
			undef_op(cpu);
			cpu->running = 0;
			

	}
	return 0;
	//IP = PC & 0xFFFF;
}
//need to build functions for address modes(or possibly macros)
//then functions or macros for operations

