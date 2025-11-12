/* Debug a single test case from JSON */
#define _POSIX_C_SOURCE 200809L
#include "intel8086.h"
#include <stdio.h>
#include <stdlib.h>
#include <cjson/cJSON.h>
#include <zlib.h>
#include <string.h>

static char* read_gzip_file(const char *filename) {
    gzFile gz = gzopen(filename, "rb");
    if (!gz) return NULL;

    size_t buffer_size = 1024 * 1024;
    char *buffer = malloc(buffer_size);
    size_t total_read = 0;

    while (1) {
        int bytes_read = gzread(gz, buffer + total_read, buffer_size - total_read - 1);
        if (bytes_read <= 0) break;
        total_read += bytes_read;
        if (total_read >= buffer_size - 1024) {
            buffer_size *= 2;
            buffer = realloc(buffer, buffer_size);
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
    if (argc != 3) {
        printf("Usage: %s <test_file.json.gz> <test_index>\n", argv[0]);
        return 1;
    }

    char *json_data = read_gzip_file(argv[1]);
    if (!json_data) {
        printf("Failed to read file\n");
        return 1;
    }

    cJSON *tests = cJSON_Parse(json_data);
    free(json_data);

    int test_idx = atoi(argv[2]);
    cJSON *test = cJSON_GetArrayItem(tests, test_idx);
    if (!test) {
        printf("Test %d not found\n", test_idx);
        return 1;
    }

    cJSON *name = cJSON_GetObjectItem(test, "name");
    printf("Test: %s\n", name->valuestring);

    cJSON *initial = cJSON_GetObjectItem(test, "initial");
    cJSON *final = cJSON_GetObjectItem(test, "final");

    X86Cpu *cpu = malloc(sizeof(X86Cpu));
    init_8086(cpu);

    set_cpu_regs(cpu, cJSON_GetObjectItem(initial, "regs"));
    set_cpu_ram(cpu, cJSON_GetObjectItem(initial, "ram"));

    printf("\nBefore execution:\n");
    printf("  AX=%04X BX=%04X CX=%04X DX=%04X\n", cpu->ax.w, cpu->bx.w, cpu->cx.w, cpu->dx.w);
    printf("  SP=%04X BP=%04X SI=%04X DI=%04X\n", cpu->sp, cpu->bp, cpu->si, cpu->di);
    printf("  CS=%04X DS=%04X SS=%04X ES=%04X\n", cpu->cs, cpu->ds, cpu->ss, cpu->es);
    printf("  IP=%04X FLAGS=%04X\n", cpu->ip, cpu->flags);

    do_op(cpu);

    printf("\nAfter execution:\n");
    printf("  AX=%04X BX=%04X CX=%04X DX=%04X\n", cpu->ax.w, cpu->bx.w, cpu->cx.w, cpu->dx.w);
    printf("  SP=%04X BP=%04X SI=%04X DI=%04X\n", cpu->sp, cpu->bp, cpu->si, cpu->di);
    printf("  CS=%04X DS=%04X SS=%04X ES=%04X\n", cpu->cs, cpu->ds, cpu->ss, cpu->es);
    printf("  IP=%04X FLAGS=%04X\n", cpu->ip, cpu->flags);

    printf("\nExpected changes:\n");
    cJSON *final_regs = cJSON_GetObjectItem(final, "regs");
    cJSON *reg;
    if ((reg = cJSON_GetObjectItem(final_regs, "ax"))) printf("  AX=%04X\n", reg->valueint);
    if ((reg = cJSON_GetObjectItem(final_regs, "bx"))) printf("  BX=%04X\n", reg->valueint);
    if ((reg = cJSON_GetObjectItem(final_regs, "cx"))) printf("  CX=%04X\n", reg->valueint);
    if ((reg = cJSON_GetObjectItem(final_regs, "dx"))) printf("  DX=%04X\n", reg->valueint);
    if ((reg = cJSON_GetObjectItem(final_regs, "sp"))) printf("  SP=%04X\n", reg->valueint);
    if ((reg = cJSON_GetObjectItem(final_regs, "bp"))) printf("  BP=%04X\n", reg->valueint);
    if ((reg = cJSON_GetObjectItem(final_regs, "si"))) printf("  SI=%04X\n", reg->valueint);
    if ((reg = cJSON_GetObjectItem(final_regs, "di"))) printf("  DI=%04X\n", reg->valueint);
    if ((reg = cJSON_GetObjectItem(final_regs, "cs"))) printf("  CS=%04X\n", reg->valueint);
    if ((reg = cJSON_GetObjectItem(final_regs, "ds"))) printf("  DS=%04X\n", reg->valueint);
    if ((reg = cJSON_GetObjectItem(final_regs, "ss"))) printf("  SS=%04X\n", reg->valueint);
    if ((reg = cJSON_GetObjectItem(final_regs, "es"))) printf("  ES=%04X\n", reg->valueint);
    if ((reg = cJSON_GetObjectItem(final_regs, "ip"))) printf("  IP=%04X\n", reg->valueint);
    if ((reg = cJSON_GetObjectItem(final_regs, "flags"))) printf("  FLAGS=%04X\n", reg->valueint);

    free(cpu->ram);
    free(cpu);
    cJSON_Delete(tests);
    return 0;
}
