#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "intel8086.h"
#define BIOS_FILE "0239462.BIN"


void load_bios(X86Cpu *cpu, char *filename);

int main_loop(X86Cpu *cpu, int instructions);

//array for RAM according to emu8086 0x10FFEF bytes
//unsigned char ram[0x100000];

void ram_dump(X86Cpu *cpu)
{
	FILE *ramdmp;
    ramdmp = fopen("ram.dmp", "wr");

    fwrite(cpu->ram,RAM_SIZE,1,ramdmp);
	fclose(ramdmp);
}

int main(int argc, char **argv)
{
	X86Cpu *cpu;
	cpu = malloc(sizeof(X86Cpu)); 
	init_8086(cpu);
	load_bios(cpu, BIOS_FILE);

	main_loop(cpu, 10);

	ram_dump(cpu);
	free(cpu->ram);
	free(cpu);

	return 0;
}

void load_bios(X86Cpu *cpu, char *filename)
{
	FILE *bios;
	#define BIOS_SIZE 0x10000
	#define BIOS_ADDR 0xF0000
	uint8_t *tmp = malloc(BIOS_SIZE);
	uint8_t *ram=cpu->ram;
	bios = fopen(filename,"rb");

	if (bios == NULL)
	{
		printf("BIOS %s not found!",filename);
		fclose(bios);
		exit(1);
	}

	fread(tmp, 1, BIOS_SIZE, bios);
	memcpy(&cpu->ram[BIOS_ADDR], tmp, BIOS_SIZE);
	printf("%x\n", tmp[0]);
	free(tmp);

}

int main_loop(X86Cpu *cpu, int instructions)
{
	uint32_t PC = 0;
	//PC = 0xFFFF0;
	cpu->running = 1;
	fprintf(stderr,"starting at %x\n",cpu->ip | cpu->cs << 4);
	while((cpu->running == 1) && (instructions > 0))
	{
	//need to fix so it uses IP + CS
//	DoOP(ram[PC]);
		PC = 0;//?
		do_op(cpu);
		printf("\n");	
		print_flags(cpu);
		printf(" ");
		print_registers(cpu);
		instructions--;
		if(cpu->running == 0)
		break;
	}
}

