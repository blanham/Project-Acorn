#include "5150emu.h"
#include "intel8086.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
int cycles;
//need to use IP in order to be like actual Processor
int PC;

//registers
unsigned short AX, BX, CX, DX;
//Pointer, index
unsigned short SP, BP, SI, DI; 
unsigned short FLAGS;
long IP;
unsigned short CS, DS, SS, ES; 

/* Ok, need to structure instructions better:
 * case 0xXX:
 *	InstructionMode(ram[PC+1]); //probably need R/W/M
 *	//the above will go ahead and increase the PC for us
 * 	//do what ever the op code is
 *	
 *
 *
 */
void Init8086()
{
	AX=BX=CX=DX=SP=BP=SI=DI=IP=CS=DS=SS=ES=0;
	FLAGS=0;
	
}

void PrintRegisters()
{
	printf(" AX: %.4X, BX: %.4X, CX: %.4X, DX: %.4X FL: %.4X", AX, BX, CX, DX,FLAGS);
}


void PrintFlags()
{
    printf("FLAGS:");

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

//need flag checkers
void ChkPF(unsigned short data)
{

	 data ^= (data >> 1);
	 data ^= (data >> 2);
	 data ^= (data >> 4);
	 data ^= (data >> 8);
	 data ^= (data >> 16);
	if (data & 0x1)
	{
		FLAGS &= 0xFFFB;
	}
	else FLAGS |= 0x4;
	
}

void ChkZF(unsigned short data)
{
	if (data == 0)
	{
		FLAGS |= 0x40;
	}
	else FLAGS &= 0xFFBF;
}

//from GRP2, SHL w/ consant 1
void SHL1(unsigned char tmp)
{

	switch (tmp&0x7)
	{
		case 0:
			printf("SHL AL,1");
			FLAGS = (FLAGS & 0xFFFE) | ((AL<<1) & 0x01);
			AX = (AH<<8) + (AL<<1);
			ChkPF(AX);
			ChkZF(AX);
			if(CF^SF)
			{
				FLAGS |= 1<<11;
			}	
			break;
		default:
			printf("Missing SHL1 Arg!\n");
			exit(1);
		
	}

}



int DoOP(unsigned char OP) {

	//cycles = 0;
	int tmp;
	printf("$%X:\t", PCnew);
	
	switch (OP)
	{
		case 0x32:
			printf("XOR");
			//lets trial some mod reg r/w shit
			if(!(ram[PC+1]&0xC0)) exit(1);
			switch (ram[PC+1]&0x3F)
			{
				case 0x24:
					printf(" AH,AH");
					AX = ((AX&0xFF00)^(AX&0xFF00)) + AL;
					ChkZF(AX);
					ChkPF(AX);
					//check if neg
					if(AX&0x8000) FLAGS |= 0x80;
					else FLAGS &= 0xF7F;
					//zeros Z and O
					FLAGS &= 0x7FE;
					break;
				default:
					break;
					
			}
			printf("2nd: %.2X",ram[PC+1]);
			PC += 2;
			break;			
		case 0x71:
			printf("JNO");
			if (!OF)
			{
				PC += ram[PC+1] + 2;		
			}
			else PC += 2;
			break;
		case 0x73:
			printf("JNC");
			if (!CF)
			{
				PC += ram[PC+1] + 2;
			}
			else PC += 2;
			break;
		case 0x75:
			printf("JNZ");
			if (!ZF)
			{
				PC += ram[PC+1] + 2;
			}
			else PC += 2;
			break;
		case 0x76:
			printf("JBE");
			if(ZF || CF)
			{
				PC += ram[PC+1] + 2;
			}
			else PC += 2;
			break; 
		case 0x79:
			printf("JNS");
			if (!SF)
			{
				PC += ram[PC+1] + 2;
			}
			else PC += 2;
			break;

		case 0x7B:
			printf("JNP");
			if (!PF)
			{
				PC += ram[PC+1] + 2;
			}
			else PC += 2;
			break;
		case 0x9E:
			printf("SAHF");
			FLAGS = (FLAGS & 0xF00) + AH;
			if(CF^SF)
			{
				FLAGS |= 1<<11;
			}
			PC++;
			break;
		case 0x9F:
			printf("LAHF");
			AX = (((FLAGS & 0xFF)+0x2)<<8) + (AX & 0xFF);
			PC++;
			break;
		case 0xB0:
			printf("MOV AL,%.2X",ram[PC+1]);
			AX = ram[PC+1] + (AX & 0xFF00);
			PC += 2;	
			break;
		case 0xB1:
			printf("MOV CL,%.2X",ram[PC+1]);
			CX = ram[PC+1] + (CX & 0xFF00);
			PC += 2;
			break;
		case 0xB4:
			printf("MOV AH,%.2X", ram[PC+1]);
			AX = ram[PC+1] << 8;
			PC += 2;
			break;
		case 0xD0:
			//all have 1 as constant
			//middle 3 bits determine instruction
			tmp = (ram[PC+1]&0x38)>>3;
			if (!(ram[PC+1]&0xC0)) exit(1);
			switch (tmp)
			{
				case 4:
				SHL1(ram[PC+1]);
				break;
				default:
				printf("Unimplemented  GRP2 OP code\n");
				exit(1);
			}
			PC += 2;
			break;
		case 0xD2:
			//fix this to match 0xD0
			printf("SHR AH,CL");
			if(ram[PC+1] == 0xEC)
			{
				FLAGS = (FLAGS & 0xFFFE) | ((AH>>(CL-1)) & 0x01);
				AX = AL + ((AH>>CL)<<8);
				ChkPF(AX);
				ChkZF(AX);
				if(CF^SF)
				{
					FLAGS |= 1<<11;
				}	
				PC +=2;
			}else exit(1);
			break;
		case 0xEA:
			CS=(ram[PC+4]<<8)+ram[PC+3];
			IP=(ram[PC+2]<<8)+ram[PC+1];
			PC=PCnew;
			printf("JMP Direct to $0x%.6X", PCnew );

			//PC=ram[PC+1]+(ram[PC+2]<<8)+(ram[PC+3]<<16)+(ram[PC+4]<<12);
			printf("\nCS:%.4X IP:%.4X\n",CS,IP);
			break;
		//Flags
		case 0xFA:
			printf("%.2x CLI ", OP);
			FLAGS &= 0xDFF;
			PC++;
			break;
		default:
			printf("Unknown OP Code: %.2X\n", OP);
			exit (1);
			

	}
	IP = PC & 0xFFFF;
}

