/* Test a single file in isolation */
#define _POSIX_C_SOURCE 200809L

#include "intel8086.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <cjson/cJSON.h>
#include <zlib.h>
#include <unistd.h>
#include <fcntl.h>

typedef struct {
	int total;
	int passed;
	int failed;
} TestResults;

static char* read_gzip_file(const char *filename) {
	gzFile gz = gzopen(filename, "rb");
	if (!gz) return NULL;

	size_t buffer_size = 1024 * 1024;
	size_t total_read = 0;
	char *buffer = malloc(buffer_size);
	if (!buffer) { gzclose(gz); return NULL; }

	while (1) {
		int bytes_read = gzread(gz, buffer + total_read, buffer_size - total_read - 1);
		if (bytes_read < 0) { free(buffer); gzclose(gz); return NULL; }
		if (bytes_read == 0) break;
		total_read += bytes_read;
		if (total_read >= buffer_size - 1024) {
			buffer_size *= 2;
			char *new_buffer = realloc(buffer, buffer_size);
			if (!new_buffer) { free(buffer); gzclose(gz); return NULL; }
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

static bool check_cpu_regs(X86Cpu *cpu, cJSON *regs) {
	cJSON *reg;
	if ((reg = cJSON_GetObjectItem(regs, "ax")) && cpu->ax.w != reg->valueint) return false;
	if ((reg = cJSON_GetObjectItem(regs, "bx")) && cpu->bx.w != reg->valueint) return false;
	if ((reg = cJSON_GetObjectItem(regs, "cx")) && cpu->cx.w != reg->valueint) return false;
	if ((reg = cJSON_GetObjectItem(regs, "dx")) && cpu->dx.w != reg->valueint) return false;
	if ((reg = cJSON_GetObjectItem(regs, "cs")) && cpu->cs != (uint16_t)reg->valueint) return false;
	if ((reg = cJSON_GetObjectItem(regs, "ss")) && cpu->ss != (uint16_t)reg->valueint) return false;
	if ((reg = cJSON_GetObjectItem(regs, "ds")) && cpu->ds != (uint16_t)reg->valueint) return false;
	if ((reg = cJSON_GetObjectItem(regs, "es")) && cpu->es != (uint16_t)reg->valueint) return false;
	if ((reg = cJSON_GetObjectItem(regs, "sp")) && cpu->sp != (uint16_t)reg->valueint) return false;
	if ((reg = cJSON_GetObjectItem(regs, "bp")) && cpu->bp != (uint16_t)reg->valueint) return false;
	if ((reg = cJSON_GetObjectItem(regs, "si")) && cpu->si != (uint16_t)reg->valueint) return false;
	if ((reg = cJSON_GetObjectItem(regs, "di")) && cpu->di != (uint16_t)reg->valueint) return false;
	if ((reg = cJSON_GetObjectItem(regs, "ip")) && cpu->ip != (uint16_t)reg->valueint) return false;
	if ((reg = cJSON_GetObjectItem(regs, "flags")) && cpu->flags != (uint16_t)reg->valueint) return false;
	return true;
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

static bool check_cpu_ram(X86Cpu *cpu, cJSON *ram) {
	cJSON *entry;
	cJSON_ArrayForEach(entry, ram) {
		if (cJSON_GetArraySize(entry) >= 2) {
			uint32_t addr = cJSON_GetArrayItem(entry, 0)->valueint;
			uint8_t expected = cJSON_GetArrayItem(entry, 1)->valueint;
			uint8_t actual = cpu_read_byte(cpu, addr);
			if (actual != expected) return false;
		}
	}
	return true;
}

static bool run_test_case(X86Cpu *cpu, cJSON *test, int test_num) {
	cJSON *initial = cJSON_GetObjectItem(test, "initial");
	cJSON *final = cJSON_GetObjectItem(test, "final");
	if (!initial || !final) return false;

	init_8086(cpu);
	set_cpu_regs(cpu, cJSON_GetObjectItem(initial, "regs"));
	set_cpu_ram(cpu, cJSON_GetObjectItem(initial, "ram"));

	/* Debug: print first 5 test states */
	if (test_num < 5) {
		printf("Test %d before: IP=%04X FLAGS=%04X\n", test_num, cpu->ip, cpu->flags);
	}

	do_op(cpu);

	if (test_num < 5) {
		printf("Test %d after: IP=%04X FLAGS=%04X\n", test_num, cpu->ip, cpu->flags);
		cJSON *expected_regs = cJSON_GetObjectItem(final, "regs");
		cJSON *reg;
		if ((reg = cJSON_GetObjectItem(expected_regs, "ip"))) {
			printf("Test %d expected IP=%04X\n", test_num, reg->valueint);
		}
		if ((reg = cJSON_GetObjectItem(expected_regs, "flags"))) {
			printf("Test %d expected FLAGS=%04X\n", test_num, reg->valueint);
		}
	}

	bool regs_ok = check_cpu_regs(cpu, cJSON_GetObjectItem(final, "regs"));
	bool ram_ok = check_cpu_ram(cpu, cJSON_GetObjectItem(final, "ram"));

	if (!regs_ok || !ram_ok) {
		if (test_num < 5) {
			printf("Test %d FAILED: regs_ok=%d ram_ok=%d\n", test_num, regs_ok, ram_ok);
		}
	}

	return regs_ok && ram_ok;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <test_file.json.gz>\n", argv[0]);
		return 1;
	}

	char *json_data = read_gzip_file(argv[1]);
	if (!json_data) return 1;

	cJSON *tests = cJSON_Parse(json_data);
	free(json_data);
	if (!tests) return 1;

	X86Cpu *cpu = malloc(sizeof(X86Cpu));
	if (!cpu) { cJSON_Delete(tests); return 1; }

	TestResults results = {0, 0, 0};
	cJSON *test;
	int test_num = 0;
	cJSON_ArrayForEach(test, tests) {
		results.total++;
		if (run_test_case(cpu, test, test_num)) {
			results.passed++;
		} else {
			results.failed++;
		}
		test_num++;
	}

	printf("\nResults: %d/%d passed (%.1f%%)\n",
		results.passed, results.total,
		results.total > 0 ? (results.passed * 100.0 / results.total) : 0.0);

	free(cpu->ram);
	free(cpu);
	cJSON_Delete(tests);
	return results.failed > 0 ? 1 : 0;
}
