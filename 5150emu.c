/* This file is part of Project Acorn.
 * Licensed under the University of Illinois/NCSA Open Source License.
 * See LICENSE file in the project root for full license information.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include "intel8086.h"

#define BIOS_FILE "0239462.BIN"


void load_bios(X86Cpu *cpu, char *filename);

int main_loop(X86Cpu *cpu, int instructions);

//array for RAM according to emu8086 0x10FFEF bytes
//unsigned char ram[0x100000];

void ram_dump(X86Cpu *cpu)
{
	FILE *ramdmp;
	ramdmp = fopen("ram.dmp", "wb");
	if (ramdmp == NULL) {
		fprintf(stderr, "Failed to open ram.dmp for writing\n");
		return;
	}
	fwrite(cpu->ram, RAM_SIZE, 1, ramdmp);
	fclose(ramdmp);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	/* Initialize SDL */
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
		return 1;
	}

	/* Create window for future display output */
	SDL_Window *window = SDL_CreateWindow(
		"Project Acorn - IBM PC 5150 Emulator",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		640, 480,
		SDL_WINDOW_SHOWN
	);

	if (window == NULL) {
		fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	/* Create renderer for future graphics output */
	SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	if (renderer == NULL) {
		fprintf(stderr, "Renderer creation failed: %s\n", SDL_GetError());
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	/* Initialize CPU and load BIOS */
	X86Cpu *cpu;
	cpu = malloc(sizeof(X86Cpu));
	init_8086(cpu);
	load_bios(cpu, BIOS_FILE);

	/* Run emulation */
	main_loop(cpu, 10);

	/* Cleanup */
	ram_dump(cpu);
	free(cpu->ram);
	free(cpu);

	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}

void load_bios(X86Cpu *cpu, char *filename)
{
	FILE *bios;
	#define BIOS_SIZE 0x10000
	#define BIOS_ADDR 0xF0000
	uint8_t *tmp = malloc(BIOS_SIZE);
	bios = fopen(filename, "rb");

	if (bios == NULL) {
		fprintf(stderr, "BIOS %s not found!\n", filename);
		free(tmp);
		exit(1);
	}

	fread(tmp, 1, BIOS_SIZE, bios);
	memcpy(&cpu->ram[BIOS_ADDR], tmp, BIOS_SIZE);
	printf("%x\n", tmp[0]);
	free(tmp);
	fclose(bios);
}

int main_loop(X86Cpu *cpu, int instructions)
{
	cpu->running = 1;
	fprintf(stderr, "starting at %x\n", cpu->ip | (cpu->cs << 4));
	while ((cpu->running == 1) && (instructions > 0)) {
		do_op(cpu);
		printf("\n");
		print_flags(cpu);
		printf(" ");
		print_registers(cpu);
		instructions--;
		if (cpu->running == 0)
			break;
	}
	return 0;
}

