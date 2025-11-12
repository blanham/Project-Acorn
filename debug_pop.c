/* Debug POP DI instruction */
#include "intel8086.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    X86Cpu *cpu = malloc(sizeof(X86Cpu));
    init_8086(cpu);

    /* Set up test case from failing test */
    cpu->ax.w = 0;
    cpu->bx.w = 0;
    cpu->cx.w = 53486;
    cpu->dx.w = 61635;
    cpu->cs = 59006;
    cpu->ss = 43781;  /* 0xAAF5 */
    cpu->ds = 59153;
    cpu->es = 61407;
    cpu->sp = 20273;  /* 0x4F31 */
    cpu->bp = 584;
    cpu->si = 46375;
    cpu->di = 9081;
    cpu->ip = 59081;
    cpu->flags = 64727;

    /* Write POP DI instruction at CS:IP */
    uint32_t pc = cpu_get_pc(cpu);
    cpu_write_byte(cpu, pc, 0x5F);  /* POP DI */

    /* Write expected data on stack */
    uint32_t stack_addr = (cpu->ss << 4) + cpu->sp;
    printf("Stack address: 0x%X\n", stack_addr);
    cpu_write_byte(cpu, stack_addr, 125);     /* 0x7D */
    cpu_write_byte(cpu, stack_addr + 1, 111); /* 0x6F */

    /* Verify stack data */
    uint16_t stack_val = cpu_read_word(cpu, stack_addr);
    printf("Stack value: 0x%X (%d)\n", stack_val, stack_val);

    printf("\nBefore POP DI:\n");
    printf("  DI = 0x%X (%d)\n", cpu->di, cpu->di);
    printf("  SP = 0x%X (%d)\n", cpu->sp, cpu->sp);

    /* Execute POP DI */
    do_op(cpu);

    printf("\nAfter POP DI:\n");
    printf("  DI = 0x%X (%d) [expected: 0x6F7D (28541)]\n", cpu->di, cpu->di);
    printf("  SP = 0x%X (%d) [expected: 0x4F33 (20275)]\n", cpu->sp, cpu->sp);
    printf("  IP = 0x%X (%d) [expected: 0xE6DA (59082)]\n", cpu->ip, cpu->ip);

    int success = (cpu->di == 28541 && cpu->sp == 20275 && cpu->ip == 59082);
    printf("\nTest %s\n", success ? "PASSED" : "FAILED");

    free(cpu->ram);
    free(cpu);
    return success ? 0 : 1;
}
