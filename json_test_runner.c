/* This file is part of Project Acorn.
 * Licensed under the University of Illinois/NCSA Open Source License.
 * See LICENSE file in the project root for full license information.
 */

#include "intel8086.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <zlib.h>

/* Test statistics */
typedef struct {
	int total_tests;
	int passed_tests;
	int failed_tests;
	int total_opcodes;
	int passed_opcodes;
} TestStats;

/* Read gzipped JSON file */
char *read_gzipped_file(const char *filename) {
	gzFile file = gzopen(filename, "rb");
	if (!file) {
		fprintf(stderr, "Failed to open %s\n", filename);
		return NULL;
	}

	size_t buffer_size = 1024 * 1024 * 10; /* 10MB initial buffer */
	char *buffer = malloc(buffer_size);
	if (!buffer) {
		gzclose(file);
		return NULL;
	}

	size_t total_read = 0;
	int bytes_read;

	while ((bytes_read = gzread(file, buffer + total_read, buffer_size - total_read - 1)) > 0) {
		total_read += bytes_read;
		if (total_read >= buffer_size - 1) {
			buffer_size *= 2;
			char *new_buffer = realloc(buffer, buffer_size);
			if (!new_buffer) {
				free(buffer);
				gzclose(file);
				return NULL;
			}
			buffer = new_buffer;
		}
	}

	buffer[total_read] = '\0';
	gzclose(file);
	return buffer;
}

/* Run a single test case */
bool run_single_test(X86Cpu *cpu, cJSON *test, bool verbose) {
	/* Get test name */
	cJSON *name_obj = cJSON_GetObjectItem(test, "name");
	const char *name = name_obj ? name_obj->valuestring : "unknown";

	/* Get instruction bytes */
	cJSON *bytes_obj = cJSON_GetObjectItem(test, "bytes");
	if (!bytes_obj || !cJSON_IsArray(bytes_obj)) {
		if (verbose) printf("  SKIP: %s (no bytes)\n", name);
		return true; /* Skip, don't count as failure */
	}

	/* Get initial state */
	cJSON *initial = cJSON_GetObjectItem(test, "initial");
	if (!initial) {
		if (verbose) printf("  SKIP: %s (no initial state)\n", name);
		return true;
	}

	cJSON *initial_regs = cJSON_GetObjectItem(initial, "regs");
	if (!initial_regs) {
		if (verbose) printf("  SKIP: %s (no initial regs)\n", name);
		return true;
	}

	/* Set up CPU initial state */
	init_8086(cpu);

	cpu->ax.w = cJSON_GetObjectItem(initial_regs, "ax")->valueint;
	cpu->bx.w = cJSON_GetObjectItem(initial_regs, "bx")->valueint;
	cpu->cx.w = cJSON_GetObjectItem(initial_regs, "cx")->valueint;
	cpu->dx.w = cJSON_GetObjectItem(initial_regs, "dx")->valueint;
	cpu->cs = cJSON_GetObjectItem(initial_regs, "cs")->valueint;
	cpu->ss = cJSON_GetObjectItem(initial_regs, "ss")->valueint;
	cpu->ds = cJSON_GetObjectItem(initial_regs, "ds")->valueint;
	cpu->es = cJSON_GetObjectItem(initial_regs, "es")->valueint;
	cpu->sp = cJSON_GetObjectItem(initial_regs, "sp")->valueint;
	cpu->bp = cJSON_GetObjectItem(initial_regs, "bp")->valueint;
	cpu->si = cJSON_GetObjectItem(initial_regs, "si")->valueint;
	cpu->di = cJSON_GetObjectItem(initial_regs, "di")->valueint;
	cpu->ip = cJSON_GetObjectItem(initial_regs, "ip")->valueint;
	cpu->flags = cJSON_GetObjectItem(initial_regs, "flags")->valueint;

	/* Write instruction bytes to memory */
	uint32_t pc = cpu_get_pc(cpu);
	int byte_count = cJSON_GetArraySize(bytes_obj);
	for (int i = 0; i < byte_count; i++) {
		cJSON *byte = cJSON_GetArrayItem(bytes_obj, i);
		cpu_write_byte(cpu, pc + i, byte->valueint);
	}

	/* Write initial RAM state */
	cJSON *initial_ram = cJSON_GetObjectItem(initial, "ram");
	if (initial_ram && cJSON_IsArray(initial_ram)) {
		int ram_entries = cJSON_GetArraySize(initial_ram);
		for (int i = 0; i < ram_entries; i++) {
			cJSON *entry = cJSON_GetArrayItem(initial_ram, i);
			if (cJSON_IsArray(entry) && cJSON_GetArraySize(entry) == 2) {
				uint32_t addr = cJSON_GetArrayItem(entry, 0)->valueint;
				uint8_t value = cJSON_GetArrayItem(entry, 1)->valueint;
				cpu_write_byte(cpu, addr, value);
			}
		}
	}

	/* Execute the instruction */
	do_op(cpu);

	/* Get final expected state */
	cJSON *final = cJSON_GetObjectItem(test, "final");
	if (!final) {
		if (verbose) printf("  SKIP: %s (no final state)\n", name);
		return true;
	}

	cJSON *final_regs = cJSON_GetObjectItem(final, "regs");
	if (!final_regs) {
		if (verbose) printf("  SKIP: %s (no final regs)\n", name);
		return true;
	}

	/* Check final state - only check registers that are in the final state */
	bool passed = true;

	#define CHECK_REG(reg_name, cpu_val) do { \
		cJSON *expected = cJSON_GetObjectItem(final_regs, reg_name); \
		if (expected && cpu_val != expected->valueint) { \
			if (verbose) printf("  FAIL: %s - " reg_name " = 0x%04X (expected 0x%04X)\n", \
				name, (unsigned int)cpu_val, expected->valueint); \
			passed = false; \
		} \
	} while(0)

	CHECK_REG("ax", cpu->ax.w);
	CHECK_REG("bx", cpu->bx.w);
	CHECK_REG("cx", cpu->cx.w);
	CHECK_REG("dx", cpu->dx.w);
	CHECK_REG("cs", cpu->cs);
	CHECK_REG("ss", cpu->ss);
	CHECK_REG("ds", cpu->ds);
	CHECK_REG("es", cpu->es);
	CHECK_REG("sp", cpu->sp);
	CHECK_REG("bp", cpu->bp);
	CHECK_REG("si", cpu->si);
	CHECK_REG("di", cpu->di);
	CHECK_REG("ip", cpu->ip);
	CHECK_REG("flags", cpu->flags);

	#undef CHECK_REG

	if (passed && verbose) {
		printf("  PASS: %s\n", name);
	}

	return passed;
}

/* Test a single opcode file */
int test_opcode_file(const char *filename, TestStats *stats, bool verbose) {
	if (verbose) {
		printf("\nTesting opcode file: %s\n", filename);
	}

	char *json_str = read_gzipped_file(filename);
	if (!json_str) {
		fprintf(stderr, "Failed to read %s\n", filename);
		return -1;
	}

	cJSON *tests = cJSON_Parse(json_str);
	free(json_str);

	if (!tests) {
		fprintf(stderr, "Failed to parse JSON in %s\n", filename);
		return -1;
	}

	if (!cJSON_IsArray(tests)) {
		fprintf(stderr, "%s does not contain a JSON array\n", filename);
		cJSON_Delete(tests);
		return -1;
	}

	int test_count = cJSON_GetArraySize(tests);
	int passed = 0;
	int failed = 0;

	X86Cpu *cpu = malloc(sizeof(X86Cpu));

	for (int i = 0; i < test_count; i++) {
		cJSON *test = cJSON_GetArrayItem(tests, i);
		if (run_single_test(cpu, test, verbose)) {
			passed++;
		} else {
			failed++;
			if (!verbose && failed <= 10) {
				/* Show first 10 failures even in non-verbose mode */
				cJSON *name_obj = cJSON_GetObjectItem(test, "name");
				printf("  FAIL: %s\n", name_obj ? name_obj->valuestring : "unknown");
			}
		}
	}

	free(cpu->ram);
	free(cpu);
	cJSON_Delete(tests);

	stats->total_tests += test_count;
	stats->passed_tests += passed;
	stats->failed_tests += failed;
	stats->total_opcodes++;
	if (failed == 0) {
		stats->passed_opcodes++;
	}

	if (!verbose) {
		if (failed == 0) {
			printf("✓ %s: %d/%d tests passed\n", filename, passed, test_count);
		} else {
			printf("✗ %s: %d/%d tests passed, %d FAILED\n", filename, passed, test_count, failed);
		}
	}

	return failed;
}

int main(int argc, char **argv) {
	bool verbose = false;
	const char *test_dir = "tests/8086_tests/v1";
	const char *specific_opcode = NULL;

	/* Parse arguments */
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
			verbose = true;
		} else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
			test_dir = argv[++i];
		} else {
			specific_opcode = argv[i];
		}
	}

	printf("=================================================\n");
	printf("  Project Acorn - Comprehensive CPU Test Suite\n");
	printf("  SingleStepTests/8086 Test Runner\n");
	printf("=================================================\n\n");

	TestStats stats = {0, 0, 0, 0, 0};

	if (specific_opcode) {
		/* Test specific opcode */
		char filename[512];
		snprintf(filename, sizeof(filename), "%s/%s.json.gz", test_dir, specific_opcode);
		test_opcode_file(filename, &stats, verbose);
	} else {
		/* Test all opcodes we've implemented */
		const char *implemented_opcodes[] = {
			/* Arithmetic */
			"00", "01", "02", "03", "04", "05", /* ADD */
			"10", "11", "12", "13", "14", "15", /* ADC */
			"18", "19", "1A", "1B", "1C", "1D", /* SBB */
			"28", "29", "2A", "2B", "2C", "2D", /* SUB */
			"38", "39", "3A", "3B", "3C", "3D", /* CMP */
			"40", "41", "42", "43", "44", "45", "46", "47", /* INC */
			"48", "49", "4A", "4B", "4C", "4D", "4E", "4F", /* DEC */

			/* Logical */
			"08", "09", "0A", "0B", "0C", "0D", /* OR */
			"20", "21", "22", "23", "24", "25", /* AND */
			"30", "31", "32", "33", "34", "35", /* XOR */
			"84", "85", /* TEST r/m, r */
			"A8", "A9", /* TEST AL/AX, imm */

			/* BCD/ASCII */
			"27", /* DAA */
			"2F", /* DAS */
			"37", /* AAA */
			"3F", /* AAS */
			"D4", /* AAM */
			"D5", /* AAD */

			/* Shifts/Rotates */
			"D0", "D1", "D2", "D3",

			/* Stack */
			"06", "07", "0E", "0F", /* PUSH/POP segment */
			"16", "17", "1E", "1F", /* PUSH/POP segment */
			"50", "51", "52", "53", "54", "55", "56", "57", /* PUSH reg */
			"58", "59", "5A", "5B", "5C", "5D", "5E", "5F", /* POP reg */
			"9C", "9D", /* PUSHF/POPF */

			/* Control flow */
			"70", "71", "72", "73", "74", "75", "76", "77", /* Jcc */
			"78", "79", "7A", "7B", "7C", "7D", "7E", "7F", /* Jcc */
			"E0", "E1", "E2", "E3", /* LOOP variants */
			"E8", "E9", "EA", "EB", /* CALL/JMP */
			"C2", "C3", "CA", "CB", /* RET */
			"CC", "CD", "CE", "CF", /* INT/IRET */
			"9A", /* CALL far */

			/* Data movement */
			"86", "87", /* XCHG r/m, r */
			"88", "89", "8A", "8B", /* MOV r/m, r */
			"8C", "8E", /* MOV seg */
			"8D", /* LEA */
			"90", "91", "92", "93", "94", "95", "96", "97", /* XCHG/NOP */
			"98", "99", /* CBW/CWD */
			"9E", "9F", /* SAHF/LAHF */
			"A0", "A1", "A2", "A3", /* MOV direct */
			"A4", "A5", /* MOVS */
			"A6", "A7", /* CMPS */
			"AA", "AB", /* STOS */
			"AC", "AD", /* LODS */
			"AE", "AF", /* SCAS */
			"B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7", /* MOV imm8 */
			"B8", "B9", "BA", "BB", "BC", "BD", "BE", "BF", /* MOV imm16 */
			"C4", "C5", /* LES/LDS */

			/* Grp3/Grp4/Grp5 */
			"F6", "F7", /* TEST/NOT/NEG/MUL/IMUL/DIV/IDIV */
			"FE", "FF", /* INC/DEC/CALL/JMP r/m */

			/* Other */
			"F4", /* HLT */
			"FA", "FB", /* CLI/STI */
			"FC", "FD", /* CLD/STD */
		};

		int num_opcodes = sizeof(implemented_opcodes) / sizeof(implemented_opcodes[0]);

		for (int i = 0; i < num_opcodes; i++) {
			char filename[512];
			snprintf(filename, sizeof(filename), "%s/%s.json.gz",
				test_dir, implemented_opcodes[i]);
			test_opcode_file(filename, &stats, verbose);
		}
	}

	printf("\n=================================================\n");
	printf("  Test Summary\n");
	printf("=================================================\n");
	printf("  Total Tests:    %d\n", stats.total_tests);
	printf("  Passed Tests:   %d (%.2f%%)\n", stats.passed_tests,
		stats.total_tests > 0 ? (stats.passed_tests * 100.0 / stats.total_tests) : 0.0);
	printf("  Failed Tests:   %d\n", stats.failed_tests);
	printf("  Total Opcodes:  %d\n", stats.total_opcodes);
	printf("  Passed Opcodes: %d\n", stats.passed_opcodes);
	printf("=================================================\n");

	return stats.failed_tests > 0 ? 1 : 0;
}
