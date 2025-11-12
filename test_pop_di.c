/* Test program for POP DI (0x5F) bug */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "intel8086.h"

int main() {
    X86Cpu cpu;
    init_8086(&cpu);

    /* Set up test case from failing test:
     * Initial: SS=43781, SP=20273, DI=9081
     * Memory at (SS:SP)=720769-720770 contains [125, 111] = 0x6F7D = 28541
     * Expected after POP: SP=20275, DI=28541
     */

    cpu.ss = 43781;
    cpu.sp = 20273;
    cpu.di = 9081;
    cpu.cs = 0;
    cpu.ip = 0;

    /* Calculate physical address: SS:SP = 43781:20273 */
    uint32_t stack_addr = (uint32_t)cpu.ss * 16 + cpu.sp;
    printf("Stack address: %u (0x%X)\n", stack_addr, stack_addr);

    /* Set up memory with value 28541 (0x6F7D) at stack location */
    cpu.ram[stack_addr] = 125;      /* Low byte */
    cpu.ram[stack_addr + 1] = 111;  /* High byte */

    printf("Memory at %u: [%u, %u]\n", stack_addr,
           cpu.ram[stack_addr], cpu.ram[stack_addr + 1]);

    /* Place POP DI instruction at IP */
    cpu.ram[0] = 0x5F;

    printf("\nBefore POP DI:\n");
    printf("  SS=%u, SP=%u, DI=%u\n", cpu.ss, cpu.sp, cpu.di);

    /* Execute instruction */
    do_op(&cpu);

    printf("\nAfter POP DI:\n");
    printf("  SS=%u, SP=%u, DI=%u\n", cpu.ss, cpu.sp, cpu.di);

    printf("\nExpected:\n");
    printf("  SS=%u, SP=%u, DI=%u\n", 43781, 20275, 28541);

    /* Verify results */
    if (cpu.sp == 20275 && cpu.di == 28541) {
        printf("\n✓ TEST PASSED\n");
        return 0;
    } else {
        printf("\n✗ TEST FAILED\n");
        if (cpu.sp != 20275) {
            printf("  SP: expected 20275, got %u (diff: %d)\n",
                   cpu.sp, (int)cpu.sp - 20275);
        }
        if (cpu.di != 28541) {
            printf("  DI: expected 28541, got %u (diff: %d)\n",
                   cpu.di, (int)cpu.di - 28541);
        }
        return 1;
    }
}
