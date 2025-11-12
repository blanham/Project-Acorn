/* This file is part of Project Acorn.
 * Licensed under the University of Illinois/NCSA Open Source License.
 * See LICENSE file in the project root for full license information.
 */

#include "intel8086.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* Test result structure */
typedef struct {
	int total;
	int passed;
	int failed;
	int skipped;
} TestResults;

/* Macro for running a single test */
#define RUN_TEST(name, condition) do { \
	results->total++; \
	if (condition) { \
		printf("  ✓ %s\n", name); \
		results->passed++; \
	} else { \
		printf("  ✗ %s\n", name); \
		results->failed++; \
	} \
} while(0)

/* Helper to write instruction bytes */
static void write_instr(X86Cpu *cpu, const uint8_t *bytes, size_t len) {
	uint32_t pc = cpu_get_pc(cpu);
	for (size_t i = 0; i < len; i++) {
		cpu_write_byte(cpu, pc + i, bytes[i]);
	}
}

/* Test arithmetic instructions: ADD */
void test_add(TestResults *results) {
	printf("\n=== Testing ADD ===\n");
	X86Cpu *cpu = malloc(sizeof(X86Cpu));

	/* ADD AL, imm8 */
	init_8086(cpu);
	cpu->ax.l = 0x10;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0x04, 0x20}, 2); /* ADD AL, 0x20 */
	do_op(cpu);
	RUN_TEST("ADD AL, 0x20", cpu->ax.l == 0x30 && cpu->ip == 0x0102);

	/* ADD AX, imm16 */
	init_8086(cpu);
	cpu->ax.w = 0x1234;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0x05, 0x66, 0x00}, 3); /* ADD AX, 0x0066 */
	do_op(cpu);
	RUN_TEST("ADD AX, 0x0066", cpu->ax.w == 0x129A && cpu->ip == 0x0103);

	/* Test carry flag */
	init_8086(cpu);
	cpu->ax.l = 0xFF;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0x04, 0x01}, 2); /* ADD AL, 1 */
	do_op(cpu);
	RUN_TEST("ADD sets carry", (cpu->flags & 0x001) && cpu->ax.l == 0x00);

	free(cpu->ram);
	free(cpu);
}

/* Test ADC (Add with Carry) */
void test_adc(TestResults *results) {
	printf("\n=== Testing ADC ===\n");
	X86Cpu *cpu = malloc(sizeof(X86Cpu));

	/* ADC AL, imm8 without carry */
	init_8086(cpu);
	cpu->ax.l = 0x10;
	cpu->flags = 0;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0x14, 0x20}, 2); /* ADC AL, 0x20 */
	do_op(cpu);
	RUN_TEST("ADC AL, 0x20 (CF=0)", cpu->ax.l == 0x30);

	/* ADC AL, imm8 with carry */
	init_8086(cpu);
	cpu->ax.l = 0x10;
	cpu->flags = 0x001; /* Set carry */
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0x14, 0x20}, 2); /* ADC AL, 0x20 */
	do_op(cpu);
	RUN_TEST("ADC AL, 0x20 (CF=1)", cpu->ax.l == 0x31);

	free(cpu->ram);
	free(cpu);
}

/* Test SUB */
void test_sub(TestResults *results) {
	printf("\n=== Testing SUB ===\n");
	X86Cpu *cpu = malloc(sizeof(X86Cpu));

	/* SUB AL, imm8 */
	init_8086(cpu);
	cpu->ax.l = 0x50;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0x2C, 0x30}, 2); /* SUB AL, 0x30 */
	do_op(cpu);
	RUN_TEST("SUB AL, 0x30", cpu->ax.l == 0x20);

	/* Test borrow (carry flag on underflow) */
	init_8086(cpu);
	cpu->ax.l = 0x10;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0x2C, 0x20}, 2); /* SUB AL, 0x20 */
	do_op(cpu);
	RUN_TEST("SUB sets carry on borrow", (cpu->flags & 0x001) && cpu->ax.l == 0xF0);

	free(cpu->ram);
	free(cpu);
}

/* Test SBB (Subtract with Borrow) */
void test_sbb(TestResults *results) {
	printf("\n=== Testing SBB ===\n");
	X86Cpu *cpu = malloc(sizeof(X86Cpu));

	/* SBB AL, imm8 without borrow */
	init_8086(cpu);
	cpu->ax.l = 0x50;
	cpu->flags = 0;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0x1C, 0x30}, 2); /* SBB AL, 0x30 */
	do_op(cpu);
	RUN_TEST("SBB AL, 0x30 (CF=0)", cpu->ax.l == 0x20);

	/* SBB AL, imm8 with borrow */
	init_8086(cpu);
	cpu->ax.l = 0x50;
	cpu->flags = 0x001; /* Set carry */
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0x1C, 0x30}, 2); /* SBB AL, 0x30 */
	do_op(cpu);
	RUN_TEST("SBB AL, 0x30 (CF=1)", cpu->ax.l == 0x1F);

	free(cpu->ram);
	free(cpu);
}

/* Test INC/DEC */
void test_inc_dec(TestResults *results) {
	printf("\n=== Testing INC/DEC ===\n");
	X86Cpu *cpu = malloc(sizeof(X86Cpu));

	/* INC AX */
	init_8086(cpu);
	cpu->ax.w = 0x1234;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0x40}, 1); /* INC AX */
	do_op(cpu);
	RUN_TEST("INC AX", cpu->ax.w == 0x1235);

	/* DEC AX */
	init_8086(cpu);
	cpu->ax.w = 0x1234;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0x48}, 1); /* DEC AX */
	do_op(cpu);
	RUN_TEST("DEC AX", cpu->ax.w == 0x1233);

	free(cpu->ram);
	free(cpu);
}

/* Test MUL */
void test_mul(TestResults *results) {
	printf("\n=== Testing MUL ===\n");
	X86Cpu *cpu = malloc(sizeof(X86Cpu));

	/* MUL BL (byte) */
	init_8086(cpu);
	cpu->ax.l = 0x10;
	cpu->bx.l = 0x20;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0xF6, 0xE3}, 2); /* MUL BL */
	do_op(cpu);
	RUN_TEST("MUL BL (8-bit)", cpu->ax.w == 0x0200);

	/* MUL BX (word) */
	init_8086(cpu);
	cpu->ax.w = 0x0100;
	cpu->bx.w = 0x0200;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0xF7, 0xE3}, 2); /* MUL BX */
	do_op(cpu);
	RUN_TEST("MUL BX (16-bit)", cpu->ax.w == 0x0000 && cpu->dx.w == 0x0002);

	free(cpu->ram);
	free(cpu);
}

/* Test DIV */
void test_div(TestResults *results) {
	printf("\n=== Testing DIV ===\n");
	X86Cpu *cpu = malloc(sizeof(X86Cpu));

	/* DIV BL (byte) */
	init_8086(cpu);
	cpu->ax.w = 0x0064; /* 100 */
	cpu->bx.l = 0x0A;   /* 10 */
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0xF6, 0xF3}, 2); /* DIV BL */
	do_op(cpu);
	RUN_TEST("DIV BL", cpu->ax.l == 0x0A && cpu->ax.h == 0x00); /* quotient=10, remainder=0 */

	/* DIV BX (word) */
	init_8086(cpu);
	cpu->ax.w = 0x0064; /* low word of dividend */
	cpu->dx.w = 0x0000; /* high word of dividend */
	cpu->bx.w = 0x000A; /* divisor */
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0xF7, 0xF3}, 2); /* DIV BX */
	do_op(cpu);
	RUN_TEST("DIV BX", cpu->ax.w == 0x000A && cpu->dx.w == 0x0000);

	free(cpu->ram);
	free(cpu);
}

/* Test AND/OR/XOR */
void test_logic(TestResults *results) {
	printf("\n=== Testing Logic Operations ===\n");
	X86Cpu *cpu = malloc(sizeof(X86Cpu));

	/* AND AL, imm8 */
	init_8086(cpu);
	cpu->ax.l = 0xFF;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0x24, 0x0F}, 2); /* AND AL, 0x0F */
	do_op(cpu);
	RUN_TEST("AND AL, 0x0F", cpu->ax.l == 0x0F);

	/* OR AL, imm8 */
	init_8086(cpu);
	cpu->ax.l = 0x0F;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0x0C, 0xF0}, 2); /* OR AL, 0xF0 */
	do_op(cpu);
	RUN_TEST("OR AL, 0xF0", cpu->ax.l == 0xFF);

	/* XOR AL, imm8 */
	init_8086(cpu);
	cpu->ax.l = 0xFF;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0x34, 0xFF}, 2); /* XOR AL, 0xFF */
	do_op(cpu);
	RUN_TEST("XOR AL, 0xFF (zero)", cpu->ax.l == 0x00 && (cpu->flags & 0x040)); /* Zero flag */

	free(cpu->ram);
	free(cpu);
}

/* Test shifts/rotates */
void test_shifts(TestResults *results) {
	printf("\n=== Testing Shift/Rotate ===\n");
	X86Cpu *cpu = malloc(sizeof(X86Cpu));

	/* SHL AL, 1 */
	init_8086(cpu);
	cpu->ax.l = 0x40;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0xD0, 0xE0}, 2); /* SHL AL, 1 */
	do_op(cpu);
	RUN_TEST("SHL AL, 1", cpu->ax.l == 0x80);

	/* SHR AL, 1 */
	init_8086(cpu);
	cpu->ax.l = 0x80;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0xD0, 0xE8}, 2); /* SHR AL, 1 */
	do_op(cpu);
	RUN_TEST("SHR AL, 1", cpu->ax.l == 0x40);

	free(cpu->ram);
	free(cpu);
}

/* Test BCD adjustments */
void test_bcd(TestResults *results) {
	printf("\n=== Testing BCD Adjustments ===\n");
	X86Cpu *cpu = malloc(sizeof(X86Cpu));

	/* DAA */
	init_8086(cpu);
	cpu->ax.l = 0x15; /* 15 in BCD */
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	cpu->flags = 0;
	write_instr(cpu, (uint8_t[]){0x27}, 1); /* DAA */
	do_op(cpu);
	RUN_TEST("DAA (no adjust)", cpu->ax.l == 0x15);

	/* AAA */
	init_8086(cpu);
	cpu->ax.l = 0x0F; /* AL > 9 */
	cpu->flags = 0;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0x37}, 1); /* AAA */
	do_op(cpu);
	RUN_TEST("AAA adjusts", cpu->ax.l == 0x05 && cpu->ax.h == 0x01);

	free(cpu->ram);
	free(cpu);
}

/* Test CBW/CWD */
void test_conversions(TestResults *results) {
	printf("\n=== Testing Conversions ===\n");
	X86Cpu *cpu = malloc(sizeof(X86Cpu));

	/* CBW with positive value */
	init_8086(cpu);
	cpu->ax.l = 0x7F;
	cpu->ax.h = 0xFF;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0x98}, 1); /* CBW */
	do_op(cpu);
	RUN_TEST("CBW (positive)", cpu->ax.w == 0x007F);

	/* CBW with negative value */
	init_8086(cpu);
	cpu->ax.l = 0x80;
	cpu->ax.h = 0x00;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0x98}, 1); /* CBW */
	do_op(cpu);
	RUN_TEST("CBW (negative)", cpu->ax.w == 0xFF80);

	/* CWD with positive value */
	init_8086(cpu);
	cpu->ax.w = 0x7FFF;
	cpu->dx.w = 0xFFFF;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0x99}, 1); /* CWD */
	do_op(cpu);
	RUN_TEST("CWD (positive)", cpu->ax.w == 0x7FFF && cpu->dx.w == 0x0000);

	/* CWD with negative value */
	init_8086(cpu);
	cpu->ax.w = 0x8000;
	cpu->dx.w = 0x0000;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0x99}, 1); /* CWD */
	do_op(cpu);
	RUN_TEST("CWD (negative)", cpu->ax.w == 0x8000 && cpu->dx.w == 0xFFFF);

	free(cpu->ram);
	free(cpu);
}

/* Test string operations */
void test_strings(TestResults *results) {
	printf("\n=== Testing String Operations ===\n");
	X86Cpu *cpu = malloc(sizeof(X86Cpu));

	/* STOSB */
	init_8086(cpu);
	cpu->ax.l = 0x42;
	cpu->es = 0x1000;
	cpu->di = 0x0100;
	cpu->flags = 0; /* DF=0, increment */
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0xAA}, 1); /* STOSB */
	do_op(cpu);
	uint8_t stored = cpu_read_byte(cpu, (0x1000 << 4) + 0x0100);
	RUN_TEST("STOSB", stored == 0x42 && cpu->di == 0x0101);

	/* LODSB */
	init_8086(cpu);
	cpu->ds = 0x1000;
	cpu->si = 0x0100;
	cpu->flags = 0;
	cpu->ip = 0x0200;
	cpu->cs = 0x1000;
	cpu_write_byte(cpu, (0x1000 << 4) + 0x0100, 0x88);
	write_instr(cpu, (uint8_t[]){0xAC}, 1); /* LODSB */
	do_op(cpu);
	RUN_TEST("LODSB", cpu->ax.l == 0x88 && cpu->si == 0x0101);

	free(cpu->ram);
	free(cpu);
}

/* Test stack operations */
void test_stack(TestResults *results) {
	printf("\n=== Testing Stack Operations ===\n");
	X86Cpu *cpu = malloc(sizeof(X86Cpu));

	/* PUSH AX */
	init_8086(cpu);
	cpu->ax.w = 0x1234;
	cpu->ss = 0x2000;
	cpu->sp = 0x0100;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0x50}, 1); /* PUSH AX */
	do_op(cpu);
	uint16_t pushed = cpu_read_word(cpu, (0x2000 << 4) + 0x00FE);
	RUN_TEST("PUSH AX", pushed == 0x1234 && cpu->sp == 0x00FE);

	/* POP BX */
	init_8086(cpu);
	cpu->ss = 0x2000;
	cpu->sp = 0x00FE;
	cpu->bx.w = 0x0000;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	cpu_write_word(cpu, (0x2000 << 4) + 0x00FE, 0x5678);
	write_instr(cpu, (uint8_t[]){0x5B}, 1); /* POP BX */
	do_op(cpu);
	RUN_TEST("POP BX", cpu->bx.w == 0x5678 && cpu->sp == 0x0100);

	free(cpu->ram);
	free(cpu);
}

/* Test jumps */
void test_jumps(TestResults *results) {
	printf("\n=== Testing Jumps ===\n");
	X86Cpu *cpu = malloc(sizeof(X86Cpu));

	/* JMP short */
	init_8086(cpu);
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0xEB, 0x10}, 2); /* JMP short +16 */
	do_op(cpu);
	RUN_TEST("JMP short", cpu->ip == 0x0112);

	/* JZ (taken) */
	init_8086(cpu);
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	cpu->flags = 0x040; /* ZF=1 */
	write_instr(cpu, (uint8_t[]){0x74, 0x05}, 2); /* JZ +5 */
	do_op(cpu);
	RUN_TEST("JZ (taken)", cpu->ip == 0x0107);

	/* JZ (not taken) */
	init_8086(cpu);
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	cpu->flags = 0x000; /* ZF=0 */
	write_instr(cpu, (uint8_t[]){0x74, 0x05}, 2); /* JZ +5 */
	do_op(cpu);
	RUN_TEST("JZ (not taken)", cpu->ip == 0x0102);

	free(cpu->ram);
	free(cpu);
}

/* Test MOV variants */
void test_mov(TestResults *results) {
	printf("\n=== Testing MOV Variants ===\n");
	X86Cpu *cpu = malloc(sizeof(X86Cpu));

	/* MOV AL, imm8 */
	init_8086(cpu);
	cpu->ax.l = 0x00;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0xB0, 0x42}, 2); /* MOV AL, 0x42 */
	do_op(cpu);
	RUN_TEST("MOV AL, imm8", cpu->ax.l == 0x42 && cpu->ip == 0x0102);

	/* MOV AX, imm16 */
	init_8086(cpu);
	cpu->ax.w = 0x0000;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0xB8, 0x34, 0x12}, 3); /* MOV AX, 0x1234 */
	do_op(cpu);
	RUN_TEST("MOV AX, imm16", cpu->ax.w == 0x1234 && cpu->ip == 0x0103);

	/* MOV [offset], AL */
	init_8086(cpu);
	cpu->ax.l = 0x99;
	cpu->ds = 0x1000;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	write_instr(cpu, (uint8_t[]){0xA2, 0x50, 0x00}, 3); /* MOV [0x0050], AL */
	do_op(cpu);
	uint8_t stored_val = cpu_read_byte(cpu, (0x1000 << 4) + 0x0050);
	RUN_TEST("MOV [offset], AL", stored_val == 0x99);

	/* MOV AL, [offset] */
	init_8086(cpu);
	cpu->ax.l = 0x00;
	cpu->ds = 0x1000;
	cpu->ip = 0x0100;
	cpu->cs = 0x1000;
	cpu_write_byte(cpu, (0x1000 << 4) + 0x0050, 0x77);
	write_instr(cpu, (uint8_t[]){0xA0, 0x50, 0x00}, 3); /* MOV AL, [0x0050] */
	do_op(cpu);
	RUN_TEST("MOV AL, [offset]", cpu->ax.l == 0x77);

	free(cpu->ram);
	free(cpu);
}

/* Main test runner */
int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	TestResults results = {0, 0, 0, 0};

	printf("=================================================\n");
	printf("  Project Acorn - 8086 CPU Test Suite\n");
	printf("  Comprehensive Instruction Testing\n");
	printf("=================================================\n");

	/* Run all test suites */
	test_add(&results);
	test_adc(&results);
	test_sub(&results);
	test_sbb(&results);
	test_inc_dec(&results);
	test_mul(&results);
	test_div(&results);
	test_logic(&results);
	test_shifts(&results);
	test_bcd(&results);
	test_conversions(&results);
	test_strings(&results);
	test_stack(&results);
	test_jumps(&results);
	test_mov(&results);

	/* Print summary */
	printf("\n=================================================\n");
	printf("  Test Summary\n");
	printf("=================================================\n");
	printf("  Total:   %d\n", results.total);
	printf("  Passed:  %d (%.1f%%)\n", results.passed,
		results.total > 0 ? (results.passed * 100.0 / results.total) : 0.0);
	printf("  Failed:  %d\n", results.failed);
	printf("  Skipped: %d\n", results.skipped);
	printf("=================================================\n");

	return results.failed > 0 ? 1 : 0;
}
