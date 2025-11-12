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

/* Function to set CPU state from test data */
void set_cpu_state(X86Cpu *cpu, uint16_t ax, uint16_t bx, uint16_t cx, uint16_t dx,
		uint16_t cs, uint16_t ss, uint16_t ds, uint16_t es,
		uint16_t sp, uint16_t bp, uint16_t si, uint16_t di,
		uint16_t ip, uint16_t flags)
{
	cpu->ax.w = ax;
	cpu->bx.w = bx;
	cpu->cx.w = cx;
	cpu->dx.w = dx;
	cpu->cs = cs;
	cpu->ss = ss;
	cpu->ds = ds;
	cpu->es = es;
	cpu->sp = sp;
	cpu->bp = bp;
	cpu->si = si;
	cpu->di = di;
	cpu->ip = ip;
	cpu->flags = flags;
}

/* Function to check if CPU state matches expected values */
bool check_cpu_state(X86Cpu *cpu, const char *test_name,
		uint16_t exp_ax, uint16_t exp_bx, uint16_t exp_cx, uint16_t exp_dx,
		uint16_t exp_cs, uint16_t exp_ss, uint16_t exp_ds, uint16_t exp_es,
		uint16_t exp_sp, uint16_t exp_bp, uint16_t exp_si, uint16_t exp_di,
		uint16_t exp_ip, uint16_t exp_flags,
		bool check_all)
{
	(void)test_name;  /* Reserved for future use */
	bool passed = true;

	/* When check_all is false, only check registers that changed */
	if (check_all || exp_ax != cpu->ax.w) {
		if (cpu->ax.w != exp_ax) {
			printf("  FAIL: AX = 0x%04X (expected 0x%04X)\n",
				cpu->ax.w, exp_ax);
			passed = false;
		}
	}

	if (check_all || exp_bx != cpu->bx.w) {
		if (cpu->bx.w != exp_bx) {
			printf("  FAIL: BX = 0x%04X (expected 0x%04X)\n",
				cpu->bx.w, exp_bx);
			passed = false;
		}
	}

	if (check_all || exp_cx != cpu->cx.w) {
		if (cpu->cx.w != exp_cx) {
			printf("  FAIL: CX = 0x%04X (expected 0x%04X)\n",
				cpu->cx.w, exp_cx);
			passed = false;
		}
	}

	if (check_all || exp_dx != cpu->dx.w) {
		if (cpu->dx.w != exp_dx) {
			printf("  FAIL: DX = 0x%04X (expected 0x%04X)\n",
				cpu->dx.w, exp_dx);
			passed = false;
		}
	}

	if (check_all || exp_ip != cpu->ip) {
		if (cpu->ip != exp_ip) {
			printf("  FAIL: IP = 0x%04X (expected 0x%04X)\n",
				cpu->ip, exp_ip);
			passed = false;
		}
	}

	if (check_all || exp_flags != cpu->flags) {
		if (cpu->flags != exp_flags) {
			printf("  FAIL: FLAGS = 0x%04X (expected 0x%04X)\n",
				cpu->flags, exp_flags);
			passed = false;
		}
	}

	/* Check segment registers if check_all is true */
	if (check_all) {
		if (cpu->cs != exp_cs) {
			printf("  FAIL: CS = 0x%04X (expected 0x%04X)\n",
				cpu->cs, exp_cs);
			passed = false;
		}
		if (cpu->ss != exp_ss) {
			printf("  FAIL: SS = 0x%04X (expected 0x%04X)\n",
				cpu->ss, exp_ss);
			passed = false;
		}
		if (cpu->ds != exp_ds) {
			printf("  FAIL: DS = 0x%04X (expected 0x%04X)\n",
				cpu->ds, exp_ds);
			passed = false;
		}
		if (cpu->es != exp_es) {
			printf("  FAIL: ES = 0x%04X (expected 0x%04X)\n",
				cpu->es, exp_es);
			passed = false;
		}
		if (cpu->sp != exp_sp) {
			printf("  FAIL: SP = 0x%04X (expected 0x%04X)\n",
				cpu->sp, exp_sp);
			passed = false;
		}
		if (cpu->bp != exp_bp) {
			printf("  FAIL: BP = 0x%04X (expected 0x%04X)\n",
				cpu->bp, exp_bp);
			passed = false;
		}
		if (cpu->si != exp_si) {
			printf("  FAIL: SI = 0x%04X (expected 0x%04X)\n",
				cpu->si, exp_si);
			passed = false;
		}
		if (cpu->di != exp_di) {
			printf("  FAIL: DI = 0x%04X (expected 0x%04X)\n",
				cpu->di, exp_di);
			passed = false;
		}
	}

	return passed;
}

/* Simple test for MOV AL, imm8 instruction (opcode 0xB0) */
void test_mov_al_imm8(TestResults *results)
{
	X86Cpu *cpu = malloc(sizeof(X86Cpu));
	printf("\n=== Testing MOV AL, imm8 (0xB0) ===\n");

	/* Test 1: Simple MOV AL, 0x8A */
	printf("Test 1: MOV AL, 0x8A\n");
	init_8086(cpu);
	set_cpu_state(cpu, 0xA9B1, 0, 0xFC22, 0xEC22,
		0xEBB4, 0x39EB, 0x4C11, 0x7D4A,
		0x456B, 0x9128, 0x7771, 0x6B96,
		0x5F6C, 0xFC83);

	/* Write instruction bytes at CS:IP */
	uint32_t pc = cpu_get_pc(cpu);
	cpu_write_byte(cpu, pc, 0xB0);  /* MOV AL, imm8 */
	cpu_write_byte(cpu, pc + 1, 0x8A);  /* immediate value */

	/* Execute instruction */
	do_op(cpu);

	/* Check results - AL should be 0x8A, IP should advance by 2 */
	if (check_cpu_state(cpu, "MOV AL, 0x8A",
			0xA98A, 0, 0xFC22, 0xEC22,  /* AX changed to 0xA98A */
			0xEBB4, 0x39EB, 0x4C11, 0x7D4A,
			0x456B, 0x9128, 0x7771, 0x6B96,
			0x5F6E, 0xFC83,  /* IP = 0x5F6C + 2 */
			false)) {
		printf("  PASSED\n");
		results->passed++;
	} else {
		printf("  FAILED\n");
		results->failed++;
	}
	results->total++;

	/* Test 2: MOV AL, 0x00 (zero) */
	printf("Test 2: MOV AL, 0x00\n");
	init_8086(cpu);
	set_cpu_state(cpu, 0xFFFF, 0, 0, 0,
		0xF000, 0, 0, 0,
		0xFFFE, 0, 0, 0,
		0x0100, 0);

	pc = cpu_get_pc(cpu);
	cpu_write_byte(cpu, pc, 0xB0);
	cpu_write_byte(cpu, pc + 1, 0x00);

	do_op(cpu);

	if (check_cpu_state(cpu, "MOV AL, 0x00",
			0xFF00, 0, 0, 0,
			0xF000, 0, 0, 0,
			0xFFFE, 0, 0, 0,
			0x0102, 0,
			false)) {
		printf("  PASSED\n");
		results->passed++;
	} else {
		printf("  FAILED\n");
		results->failed++;
	}
	results->total++;

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
	printf("=================================================\n");

	/* Run tests for implemented instructions */
	test_mov_al_imm8(&results);

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
