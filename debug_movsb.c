/* Debug MOVSB memory issue */
#define _POSIX_C_SOURCE 200809L

#include "intel8086.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <cjson/cJSON.h>
#include <zlib.h>

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

int main(int argc, char **argv) {
	if (argc < 3) {
		fprintf(stderr, "Usage: %s <test_file.json.gz> <test_index>\n", argv[0]);
		return 1;
	}

	char *json_data = read_gzip_file(argv[1]);
	if (!json_data) return 1;

	cJSON *tests = cJSON_Parse(json_data);
	free(json_data);
	if (!tests) return 1;

	int test_index = atoi(argv[2]);
	cJSON *test = cJSON_GetArrayItem(tests, test_index);
	cJSON *initial = cJSON_GetObjectItem(test, "initial");
	cJSON *final = cJSON_GetObjectItem(test, "final");
	cJSON *name_obj = cJSON_GetObjectItem(test, "name");

	printf("Test: %s\n", name_obj ? name_obj->valuestring : "unknown");

	X86Cpu *cpu = malloc(sizeof(X86Cpu));
	init_8086(cpu);

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

	cJSON *ram = cJSON_GetObjectItem(initial, "ram");
	cJSON *entry;
	cJSON_ArrayForEach(entry, ram) {
		if (cJSON_GetArraySize(entry) >= 2) {
			uint32_t addr = cJSON_GetArrayItem(entry, 0)->valueint;
			uint8_t value = cJSON_GetArrayItem(entry, 1)->valueint;
			cpu_write_byte(cpu, addr, value);
		}
	}

	printf("DS=%04X SI=%04X  ES=%04X DI=%04X\n", cpu->ds, cpu->si, cpu->es, cpu->di);
	uint32_t src = cpu_calc_addr(cpu->ds, cpu->si);
	uint32_t dst = cpu_calc_addr(cpu->es, cpu->di);
	printf("Source addr: %08X  Dest addr: %08X\n", src, dst);
	printf("Source value: %02X\n", cpu_read_byte(cpu, src));

	do_op(cpu);

	printf("After: SI=%04X DI=%04X\n", cpu->si, cpu->di);
	printf("Dest value: %02X\n", cpu_read_byte(cpu, dst));

	/* Check expected memory */
	cJSON *final_ram = cJSON_GetObjectItem(final, "ram");
	if (final_ram) {
		printf("\nExpected RAM changes:\n");
		cJSON_ArrayForEach(entry, final_ram) {
			if (cJSON_GetArraySize(entry) >= 2) {
				uint32_t addr = cJSON_GetArrayItem(entry, 0)->valueint;
				uint8_t expected = cJSON_GetArrayItem(entry, 1)->valueint;
				uint8_t actual = cpu_read_byte(cpu, addr);
				printf("  [%08X] expected=%02X actual=%02X %s\n",
					addr, expected, actual, (expected == actual) ? "✓" : "✗");
			}
		}
	}

	free(cpu->ram);
	free(cpu);
	cJSON_Delete(tests);
	return 0;
}
