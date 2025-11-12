/* This file is part of Project Acorn.
 * Licensed under the University of Illinois/NCSA Open Source License.
 * See LICENSE file in the project root for full license information.
 */

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

/* Test result structure */
typedef struct {
	int total;
	int passed;
	int failed;
	int skipped;
} TestResults;

/* Read and decompress gzipped file */
static char* read_gzip_file(const char *filename) {
	gzFile gz = gzopen(filename, "rb");
	if (!gz) {
		fprintf(stderr, "Failed to open %s\n", filename);
		return NULL;
	}

	size_t buffer_size = 1024 * 1024; /* Start with 1MB */
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

/* Set CPU register state from JSON object */
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

/* Check if CPU register state matches expected JSON */
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

/* Set memory from RAM array */
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

/* Check memory matches expected state */
static bool check_cpu_ram(X86Cpu *cpu, cJSON *ram) {
	cJSON *entry;
	cJSON_ArrayForEach(entry, ram) {
		if (cJSON_GetArraySize(entry) >= 2) {
			uint32_t addr = cJSON_GetArrayItem(entry, 0)->valueint;
			uint8_t expected = cJSON_GetArrayItem(entry, 1)->valueint;
			uint8_t actual = cpu_read_byte(cpu, addr);
			if (actual != expected) {
				return false;
			}
		}
	}
	return true;
}

/* Run a single test case */
static bool run_test_case(X86Cpu *cpu, cJSON *test) {
	cJSON *initial = cJSON_GetObjectItem(test, "initial");
	cJSON *final = cJSON_GetObjectItem(test, "final");

	if (!initial || !final) return false;

	/* Initialize CPU */
	init_8086(cpu);

	/* Redirect stderr to /dev/null to suppress warnings during testing */
	fflush(stderr);
	int saved_stderr = dup(2);
	int devnull = open("/dev/null", O_WRONLY);
	dup2(devnull, 2);

	/* Set initial state */
	set_cpu_regs(cpu, cJSON_GetObjectItem(initial, "regs"));
	set_cpu_ram(cpu, cJSON_GetObjectItem(initial, "ram"));

	/* Execute one instruction */
	do_op(cpu);

	/* Restore stderr */
	fflush(stderr);
	dup2(saved_stderr, 2);
	close(devnull);
	close(saved_stderr);

	/* Check final state */
	bool regs_ok = check_cpu_regs(cpu, cJSON_GetObjectItem(final, "regs"));
	bool ram_ok = check_cpu_ram(cpu, cJSON_GetObjectItem(final, "ram"));

	return regs_ok && ram_ok;
}

/* Run tests from a single JSON file */
static void run_test_file(const char *filename, TestResults *results) {
	char *json_data = read_gzip_file(filename);
	if (!json_data) {
		fprintf(stderr, "Failed to read %s\n", filename);
		return;
	}

	cJSON *tests = cJSON_Parse(json_data);
	free(json_data);

	if (!tests) {
		fprintf(stderr, "Failed to parse JSON in %s\n", filename);
		return;
	}

	if (!cJSON_IsArray(tests)) {
		fprintf(stderr, "%s does not contain an array\n", filename);
		cJSON_Delete(tests);
		return;
	}

	X86Cpu *cpu = malloc(sizeof(X86Cpu));
	if (!cpu) {
		cJSON_Delete(tests);
		return;
	}

	cJSON *test;
	cJSON_ArrayForEach(test, tests) {
		results->total++;

		if (run_test_case(cpu, test)) {
			results->passed++;
		} else {
			results->failed++;
			/* Optionally print failed test name */
			cJSON *name = cJSON_GetObjectItem(test, "name");
			if (name && cJSON_IsString(name)) {
				/* Only print first few failures to avoid spam */
				if (results->failed <= 10) {
					printf("  ✗ %s\n", name->valuestring);
				}
			}
		}
	}

	free(cpu->ram);
	free(cpu);
	cJSON_Delete(tests);
}

/* Main test runner */
int main(int argc, char **argv)
{
	const char *test_dir = "tests/8086_tests/v1";
	int batch_size = 10; /* Process files in batches */
	int start_batch = 0;
	int end_batch = -1; /* -1 means all */

	/* Parse command line arguments */
	if (argc >= 2) {
		start_batch = atoi(argv[1]);
	}
	if (argc >= 3) {
		end_batch = atoi(argv[2]);
	}

	TestResults results = {0, 0, 0, 0};

	printf("=================================================\n");
	printf("  Project Acorn - 8086 SingleStep Test Suite\n");
	printf("  Comprehensive Instruction Testing (JSON)\n");
	printf("=================================================\n");

	/* Get list of test files */
	char command[512];
	snprintf(command, sizeof(command), "ls %s/*.json.gz | sort", test_dir);

	FILE *fp = popen(command, "r");
	if (!fp) {
		fprintf(stderr, "Failed to list test files\n");
		return 1;
	}

	char **files = NULL;
	int file_count = 0;
	char line[512];

	while (fgets(line, sizeof(line), fp)) {
		line[strcspn(line, "\n")] = 0;
		files = realloc(files, sizeof(char*) * (file_count + 1));
		files[file_count] = strdup(line);
		file_count++;
	}
	pclose(fp);

	printf("Found %d test files\n", file_count);

	/* Determine which files to process */
	int start_file = start_batch * batch_size;
	int end_file = (end_batch < 0) ? file_count : ((end_batch + 1) * batch_size);
	if (end_file > file_count) end_file = file_count;

	if (start_file >= file_count) {
		printf("Start batch %d is beyond available files\n", start_batch);
		return 0;
	}

	printf("Processing files %d to %d (batch %d to %d)\n",
		   start_file, end_file - 1, start_batch,
		   end_batch < 0 ? (file_count - 1) / batch_size : end_batch);
	printf("=================================================\n\n");

	/* Process files in range */
	for (int i = start_file; i < end_file; i++) {
		/* Extract opcode from filename */
		char *basename = strrchr(files[i], '/');
		if (basename) basename++; else basename = files[i];

		printf("Testing %s...\n", basename);
		run_test_file(files[i], &results);
	}

	/* Clean up */
	for (int i = 0; i < file_count; i++) {
		free(files[i]);
	}
	free(files);

	/* Print summary */
	printf("\n=================================================\n");
	printf("  Test Summary (Batch %d to %d)\n", start_batch,
		   end_batch < 0 ? (file_count - 1) / batch_size : end_batch);
	printf("=================================================\n");
	printf("  Total:   %d\n", results.total);
	printf("  Passed:  %d (%.1f%%)\n", results.passed,
		results.total > 0 ? (results.passed * 100.0 / results.total) : 0.0);
	printf("  Failed:  %d\n", results.failed);
	printf("  Skipped: %d\n", results.skipped);
	printf("=================================================\n");

	return results.failed > 0 ? 1 : 0;
}
