/* Debug test to find exact failures */
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
	if (!gz) return NULL;

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

static void set_cpu_regs(X86Cpu *cpu, cJSON *regs) {
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
}

static void set_cpu_ram(X86Cpu *cpu, cJSON *ram) {
	cJSON *entry;
	cJSON_ArrayForEach(entry, ram) {
		if (cJSON_GetArraySize(entry) >= 2) {
			uint32_t addr = cJSON_GetArrayItem(entry, 0)->valueint;
			uint8_t value = cJSON_GetArrayItem(entry, 1)->valueint;
			cpu_write_byte(cpu, addr, value);
		}
	}
}

int main(int argc, char **argv) {
	if (argc < 2) {
		printf("Usage: %s <test_file.json.gz> [test_num]\n", argv[0]);
		return 1;
	}

	int test_to_run = (argc >= 3) ? atoi(argv[2]) : 0;

	char *json_data = read_gzip_file(argv[1]);
	if (!json_data) {
		fprintf(stderr, "Failed to read %s\n", argv[1]);
		return 1;
	}

	cJSON *tests = cJSON_Parse(json_data);
	free(json_data);
	if (!tests) {
		fprintf(stderr, "Failed to parse JSON\n");
		return 1;
	}

	X86Cpu cpu;
	int test_num = 0;
	cJSON *test;
	cJSON_ArrayForEach(test, tests) {
		if (test_num != test_to_run) {
			test_num++;
			continue;
		}

		printf("=== Test #%d ===\n", test_num);
		cJSON *name = cJSON_GetObjectItem(test, "name");
		if (name) printf("Name: %s\n", name->valuestring);

		cJSON *initial = cJSON_GetObjectItem(test, "initial");
		cJSON *final = cJSON_GetObjectItem(test, "final");

		init_8086(&cpu);
		set_cpu_regs(&cpu, cJSON_GetObjectItem(initial, "regs"));
		set_cpu_ram(&cpu, cJSON_GetObjectItem(initial, "ram"));

		printf("\nInitial state:\n");
		printf("  AX=%04X BX=%04X CX=%04X DX=%04X\n", cpu.ax.w, cpu.bx.w, cpu.cx.w, cpu.dx.w);
		printf("  SP=%04X BP=%04X SI=%04X DI=%04X\n", cpu.sp, cpu.bp, cpu.si, cpu.di);
		printf("  CS=%04X DS=%04X SS=%04X ES=%04X\n", cpu.cs, cpu.ds, cpu.ss, cpu.es);
		printf("  IP=%04X FLAGS=%04X\n", cpu.ip, cpu.flags);

		/* Execute */
		do_op(&cpu);

		printf("\nFinal state:\n");
		printf("  AX=%04X BX=%04X CX=%04X DX=%04X\n", cpu.ax.w, cpu.bx.w, cpu.cx.w, cpu.dx.w);
		printf("  SP=%04X BP=%04X SI=%04X DI=%04X\n", cpu.sp, cpu.bp, cpu.si, cpu.di);
		printf("  CS=%04X DS=%04X SS=%04X ES=%04X\n", cpu.cs, cpu.ds, cpu.ss, cpu.es);
		printf("  IP=%04X FLAGS=%04X\n", cpu.ip, cpu.flags);

		printf("\nExpected changes:\n");
		cJSON *final_regs = cJSON_GetObjectItem(final, "regs");
		cJSON *reg;
		bool all_ok = true;

		#define CHECK_REG(name, field) \
			if ((reg = cJSON_GetObjectItem(final_regs, name))) { \
				uint16_t expected = reg->valueint; \
				uint16_t actual = cpu.field; \
				printf("  %s: expected %04X, got %04X ", name, expected, actual); \
				if (expected == actual) printf("✓\n"); \
				else { printf("✗\n"); all_ok = false; } \
			}

		CHECK_REG("ax", ax.w)
		CHECK_REG("bx", bx.w)
		CHECK_REG("cx", cx.w)
		CHECK_REG("dx", dx.w)
		CHECK_REG("sp", sp)
		CHECK_REG("bp", bp)
		CHECK_REG("si", si)
		CHECK_REG("di", di)
		CHECK_REG("cs", cs)
		CHECK_REG("ds", ds)
		CHECK_REG("ss", ss)
		CHECK_REG("es", es)
		CHECK_REG("ip", ip)
		CHECK_REG("flags", flags)

		printf("\nResult: %s\n", all_ok ? "PASS" : "FAIL");

		free(cpu.ram);
		break;
	}

	cJSON_Delete(tests);
	return 0;
}
