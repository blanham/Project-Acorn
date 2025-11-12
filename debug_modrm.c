/* Debug ModR/M decoding */
#include "opcode.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
	X86Cpu cpu = {0};
	cpu.ram = calloc(1, RAM_SIZE);

	/* Test case: FE 03 at address 0x0A46A */
	cpu.cs = 0xFBC5;
	cpu.ip = 0xF81A;
	cpu.ss = 0x9048;
	cpu.bp = 0x5C60;
	cpu.di = 0x8667;

	uint32_t pc = cpu_get_pc(&cpu);
	printf("PC = %08X (should be 0A46A after wrapping)\n", pc);

	/* Write the instruction bytes */
	cpu_write_byte(&cpu, pc, 0xFE);
	cpu_write_byte(&cpu, pc + 1, 0x03);

	/* Read and decode ModR/M */
	uint8_t modrm_byte = cpu_read_byte(&cpu, pc + 1);
	printf("ModR/M byte = %02X\n", modrm_byte);
	printf("  MOD = %d\n", (modrm_byte >> 6) & 0x3);
	printf("  REG = %d\n", (modrm_byte >> 3) & 0x7);
	printf("  R/M = %d\n", modrm_byte & 0x7);

	/* Decode using our function */
	ModRM modrm = decode_modrm(&cpu, pc + 1);
	printf("\nDecoded ModR/M:\n");
	printf("  reg = %d\n", modrm.reg);
	printf("  rm = %d\n", modrm.rm);
	printf("  mode = %d\n", modrm.mode);
	printf("  is_memory = %d\n", modrm.is_memory);
	printf("  length = %d\n", modrm.length);
	printf("  ea = %08X\n", modrm.ea);

	printf("\nInstruction length calculation:\n");
	printf("  Opcode: 1 byte (FE)\n");
	printf("  ModR/M length: %d bytes\n", modrm.length);
	printf("  Total: %d bytes\n", 1 + modrm.length);
	printf("  IP should advance from %04X to %04X\n", cpu.ip, cpu.ip + 1 + modrm.length);

	free(cpu.ram);
	return 0;
}
