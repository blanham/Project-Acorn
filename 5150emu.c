#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "intel8086.h"

unsigned char bios_img[65536];
int LoadBIOS();
int MainLoop();
//array for RAM according to emu8086 0x10FFEF bytes
unsigned char ram[0x100000];

void RAMDump()
{
	FILE *ramdmp;
    ramdmp = fopen("ram.dmp", "wr");

    fwrite(ram,sizeof(ram),1,ramdmp);
	fclose(ramdmp);
}

int main()
{
	printf("IBM forever!");
	LoadBIOS();
	//fopen();
	atexit(RAMDump);
	MainLoop();
	


}

int LoadBIOS()
{
	FILE *bios;
	bios = fopen("0239462.BIN","rb");
	if (bios == NULL)
	{
		printf("BIOS not found!");
		fclose(bios);
		exit(1);
	}

	
	
	fread(&bios_img, 1, 65536, bios);
	memcpy(&ram[0xF0000], bios_img, sizeof(bios_img));
	printf("%x\n", bios_img[0]);
	return 0;

}

int MainLoop()
{
	PC = 0xFFFF0;
	while(1)
	{
	//need to fix so it uses IP + CS
	DoOP(ram[PC]);
	printf(" \n");
	PrintFlags();
	printf(" ");
	PrintRegisters();
	printf("\n");
	}
}


int RdRAM(){}

