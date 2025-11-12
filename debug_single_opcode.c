/* Debug a single test case from the test suite */
#define _POSIX_C_SOURCE 200809L

#include "intel8086.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <cjson/cJSON.h>
#include <zlib.h>

/* Read and decompress gzipped file */
static char* read_gzip_file(const char *filename) {
	gzFile gz = gzopen(filename, "rb");
	if (!gz) {
		fprintf(stderr, "Failed to open %s\n", filename);
		return NULL;
	}

	size_t buffer_size = 1024 * 1024;
	size_t total_read = 0;
	char *buffer = malloc(buffer_size);

	if (!buffer) {
		gzclose(gz);
		return NULL;
	}

	while (1) {
		int bytes_read = gzread(gz, buffer + total_read, buffer_size - total_read - 1);
		if (bytes_read < 0) {
			free(buffer);
			gzclose(gz);
			return NULL;
		}
		if (bytes_read == 0) break;

		total_read += bytes_read;
		if (total_read >= buffer_size - 1024) {
			buffer_size *= 2;
			char *new_buffer = realloc(buffer, buffer_size);
			if (!new_buffer) {
				free(buffer);
				gzclose(gz);
				return NULL;
			}
			buffer = new_buffer;
		}
	}

	buffer[total_read] = '\0';
	gzclose(gz);
	return buffer;
}

/* Print CPU state */
void print_state(const char *label, X86Cpu *cpu) {
	printf("\n=== %s ===\n", label);
	printf("AX=%04X BX=%04X CX=%04X DX=%04X\n", cpu->ax.w, cpu->bx.w, cpu->cx.w, cpu->dx.w);
	printf("CS=%04X DS=%04X SS=%04X ES=%04X\n", cpu->cs, cpu->ds, cpu->ss, cpu->es);
	printf("SP=%04X BP=%04X SI=%04X DI=%04X\n", cpu->sp, cpu->bp, cpu->si, cpu->di);
	printf("IP=%04X FLAGS=%04X\n", cpu->ip, cpu->flags);
	printf("Flags: CF=%d PF=%d AF=%d ZF=%d SF=%d IF=%d DF=%d OF=%d\n",
		(cpu->flags & 0x001) ? 1 : 0,
		(cpu->flags & 0x004) ? 1 : 0,
		(cpu->flags & 0x010) ? 1 : 0,
		(cpu->flags & 0x040) ? 1 : 0,
		(cpu->flags & 0x080) ? 1 : 0,
		(cpu->flags & 0x200) ? 1 : 0,
		(cpu->flags & 0x400) ? 1 : 0,
		(cpu->flags & 0x800) ? 1 : 0);
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "Usage: %s <test_file.json.gz> <test_index>\n", argv[0]);
		return 1;
	}

	const char *filename = argv[1];
	int test_index = atoi(argv[2]);

	char *json_data = read_gzip_file(filename);
	if (!json_data) {
		fprintf(stderr, "Failed to read %s\n", filename);
		return 1;
	}

	cJSON *tests = cJSON_Parse(json_data);
	free(json_data);

	if (!tests || !cJSON_IsArray(tests)) {
		fprintf(stderr, "Failed to parse JSON\n");
		return 1;
	}

	int count = cJSON_GetArraySize(tests);
	if (test_index >= count) {
		fprintf(stderr, "Test index %d out of range (0-%d)\n", test_index, count - 1);
		return 1;
	}

	cJSON *test = cJSON_GetArrayItem(tests, test_index);
	cJSON *name = cJSON_GetObjectItem(test, "name");
	cJSON *initial = cJSON_GetObjectItem(test, "initial");
	cJSON *final = cJSON_GetObjectItem(test, "final");
	cJSON *bytes = cJSON_GetObjectItem(test, "bytes");

	printf("========================================\n");
	printf("Test: %s\n", name ? name->valuestring : "unknown");
	printf("========================================\n");

	/* Print instruction bytes */
	printf("\nInstruction bytes: ");
	cJSON *byte;
	cJSON_ArrayForEach(byte, bytes) {
		printf("%02X ", byte->valueint);
	}
	printf("\n");

	/* Initialize CPU */
	X86Cpu *cpu = malloc(sizeof(X86Cpu));
	init_8086(cpu);

	/* Set initial state */
	cJSON *regs = cJSON_GetObjectItem(initial, "regs");
	cpu->ax.w = cJSON_GetObjectItem(regs, "ax")->valueint;
	cpu->bx.w = cJSON_GetObjectItem(regs, "bx")->valueint;
	cpu->cx.w = cJSON_GetObjectItem(regs, "cx")->valueint;
	cpu->dx.w = cJSON_GetObjectItem(regs, "dx")->valueint;
	cpu->cs = cJSON_GetObjectItem(regs, "cs")->valueint;
	cpu->ss = cJSON_GetObjectItem(regs, "ss")->valueint;
	cpu->ds = cJSON_GetObjectItem(regs, "ds")->valueint;
	cpu->es = cJSON_GetObjectItem(regs, "es")->valueint;
	cpu->sp = cJSON_GetObjectItem(regs, "sp")->valueint;
	cpu->bp = cJSON_GetObjectItem(regs, "bp")->valueint;
	cpu->si = cJSON_GetObjectItem(regs, "si")->valueint;
	cpu->di = cJSON_GetObjectItem(regs, "di")->valueint;
	cpu->ip = cJSON_GetObjectItem(regs, "ip")->valueint;
	cpu->flags = cJSON_GetObjectItem(regs, "flags")->valueint;

	/* Set initial RAM */
	cJSON *ram = cJSON_GetObjectItem(initial, "ram");
	cJSON *entry;
	cJSON_ArrayForEach(entry, ram) {
		if (cJSON_GetArraySize(entry) >= 2) {
			uint32_t addr = cJSON_GetArrayItem(entry, 0)->valueint;
			uint8_t value = cJSON_GetArrayItem(entry, 1)->valueint;
			cpu_write_byte(cpu, addr, value);
		}
	}

	print_state("INITIAL STATE", cpu);

	/* Execute instruction */
	do_op(cpu);

	print_state("ACTUAL FINAL STATE", cpu);

	/* Print expected state */
	printf("\n=== EXPECTED FINAL STATE ===\n");
	cJSON *final_regs = cJSON_GetObjectItem(final, "regs");
	cJSON *reg;
	if ((reg = cJSON_GetObjectItem(final_regs, "ax"))) printf("AX=%04X\n", reg->valueint);
	if ((reg = cJSON_GetObjectItem(final_regs, "bx"))) printf("BX=%04X\n", reg->valueint);
	if ((reg = cJSON_GetObjectItem(final_regs, "cx"))) printf("CX=%04X\n", reg->valueint);
	if ((reg = cJSON_GetObjectItem(final_regs, "dx"))) printf("DX=%04X\n", reg->valueint);
	if ((reg = cJSON_GetObjectItem(final_regs, "cs"))) printf("CS=%04X\n", reg->valueint);
	if ((reg = cJSON_GetObjectItem(final_regs, "ds"))) printf("DS=%04X\n", reg->valueint);
	if ((reg = cJSON_GetObjectItem(final_regs, "ss"))) printf("SS=%04X\n", reg->valueint);
	if ((reg = cJSON_GetObjectItem(final_regs, "es"))) printf("ES=%04X\n", reg->valueint);
	if ((reg = cJSON_GetObjectItem(final_regs, "sp"))) printf("SP=%04X\n", reg->valueint);
	if ((reg = cJSON_GetObjectItem(final_regs, "bp"))) printf("BP=%04X\n", reg->valueint);
	if ((reg = cJSON_GetObjectItem(final_regs, "si"))) printf("SI=%04X\n", reg->valueint);
	if ((reg = cJSON_GetObjectItem(final_regs, "di"))) printf("DI=%04X\n", reg->valueint);
	if ((reg = cJSON_GetObjectItem(final_regs, "ip"))) printf("IP=%04X\n", reg->valueint);
	if ((reg = cJSON_GetObjectItem(final_regs, "flags"))) {
		uint16_t expected_flags = reg->valueint;
		printf("FLAGS=%04X\n", expected_flags);
		printf("Expected Flags: CF=%d PF=%d AF=%d ZF=%d SF=%d IF=%d DF=%d OF=%d\n",
			(expected_flags & 0x001) ? 1 : 0,
			(expected_flags & 0x004) ? 1 : 0,
			(expected_flags & 0x010) ? 1 : 0,
			(expected_flags & 0x040) ? 1 : 0,
			(expected_flags & 0x080) ? 1 : 0,
			(expected_flags & 0x200) ? 1 : 0,
			(expected_flags & 0x400) ? 1 : 0,
			(expected_flags & 0x800) ? 1 : 0);
	}

	/* Check differences */
	printf("\n=== DIFFERENCES ===\n");
	bool pass = true;
	if ((reg = cJSON_GetObjectItem(final_regs, "ax")) && cpu->ax.w != (uint16_t)reg->valueint) {
		printf("AX: expected %04X, got %04X\n", reg->valueint, cpu->ax.w);
		pass = false;
	}
	if ((reg = cJSON_GetObjectItem(final_regs, "ip")) && cpu->ip != (uint16_t)reg->valueint) {
		printf("IP: expected %04X, got %04X\n", reg->valueint, cpu->ip);
		pass = false;
	}
	if ((reg = cJSON_GetObjectItem(final_regs, "flags")) && cpu->flags != (uint16_t)reg->valueint) {
		printf("FLAGS: expected %04X, got %04X (diff: %04X)\n",
			reg->valueint, cpu->flags, cpu->flags ^ reg->valueint);
		pass = false;
	}

	printf("\nTest: %s\n", pass ? "PASS" : "FAIL");

	free(cpu->ram);
	free(cpu);
	cJSON_Delete(tests);

	return pass ? 0 : 1;
}
