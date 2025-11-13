/* This file is part of Project Acorn.
 * Licensed under the University of Illinois/NCSA Open Source License.
 * See LICENSE file in the project root for full license information.
 */

#include <stdio.h>
#include <stdbool.h>
#include "intel8086.h"

/* CPU Flags bits */
#define FLAGS_CF	0x001  /* Carry flag */
#define FLAGS_PF 	0x004  /* Parity flag */
#define FLAGS_AF	0x010  /* Auxiliary carry flag */
#define FLAGS_ZF 	0x040  /* Zero flag */
#define FLAGS_SF	0x080  /* Sign flag */
#define FLAGS_TF	0x100  /* Trap flag */
#define FLAGS_INT 	0x200  /* Interrupt enable flag */
#define FLAGS_DF	0x400  /* Direction flag */
#define FLAGS_OV    0x800  /* Overflow flag */

/* Flag test macro */
#define FLAG_TST(x)    ((x & cpu->flags) != 0)

/* ModR/M byte structure and decoding */

/* ModR/M byte layout:
 * Bits 7-6: mod (addressing mode)
 * Bits 5-3: reg (register operand)
 * Bits 2-0: r/m (register or memory operand)
 */
#define MODRM_MOD(x)   (((x) >> 6) & 0x3)
#define MODRM_REG(x)   (((x) >> 3) & 0x7)
#define MODRM_RM(x)    ((x) & 0x7)

/* ModR/M addressing mode types */
typedef enum {
	ADDR_MODE_INDIRECT,           /* [reg] or [reg+reg] */
	ADDR_MODE_INDIRECT_DISP8,     /* [reg+disp8] or [reg+reg+disp8] */
	ADDR_MODE_INDIRECT_DISP16,    /* [reg+disp16] or [reg+reg+disp16] */
	ADDR_MODE_REGISTER            /* Direct register */
} ModRMAddressMode;

/* ModR/M decoded operand structure */
typedef struct {
	ModRMAddressMode mode;        /* Addressing mode */
	uint8_t reg;                  /* Register field (bits 5-3) */
	uint8_t rm;                   /* R/M field (bits 2-0) */
	bool is_memory;               /* True if r/m specifies memory */
	uint32_t ea;                  /* Effective address (if memory) */
	uint16_t displacement;        /* Displacement value */
	bool has_displacement;        /* True if displacement present */
	uint8_t length;               /* Total bytes consumed (ModR/M + disp) */
} ModRM;

/* Get pointer to 8-bit register */
static inline uint8_t *get_reg8_ptr(X86Cpu *cpu, uint8_t reg)
{
	switch (reg) {
		case 0: return &cpu->ax.l;  /* AL */
		case 1: return &cpu->cx.l;  /* CL */
		case 2: return &cpu->dx.l;  /* DL */
		case 3: return &cpu->bx.l;  /* BL */
		case 4: return &cpu->ax.h;  /* AH */
		case 5: return &cpu->cx.h;  /* CH */
		case 6: return &cpu->dx.h;  /* DH */
		case 7: return &cpu->bx.h;  /* BH */
		default: return NULL;
	}
}

/* Get pointer to 16-bit register */
static inline uint16_t *get_reg16_ptr(X86Cpu *cpu, uint8_t reg)
{
	switch (reg) {
		case 0: return &cpu->ax.w;  /* AX */
		case 1: return &cpu->cx.w;  /* CX */
		case 2: return &cpu->dx.w;  /* DX */
		case 3: return &cpu->bx.w;  /* BX */
		case 4: return &cpu->sp;    /* SP */
		case 5: return &cpu->bp;    /* BP */
		case 6: return &cpu->si;    /* SI */
		case 7: return &cpu->di;    /* DI */
		default: return NULL;
	}
}

/* Get register name for display purposes */
static inline const char *get_reg8_name(uint8_t reg)
{
	const char *names[] = {"AL", "CL", "DL", "BL", "AH", "CH", "DH", "BH"};
	return (reg < 8) ? names[reg] : "??";
}

static inline const char *get_reg16_name(uint8_t reg)
{
	const char *names[] = {"AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI"};
	return (reg < 8) ? names[reg] : "??";
}

/* Calculate effective address from ModR/M byte
 * Returns the physical memory address based on the r/m field and mod
 */
static inline uint32_t calc_ea(X86Cpu *cpu, uint8_t mod, uint8_t rm,
                                uint16_t disp, uint16_t *default_seg)
{
	uint32_t ea = 0;
	*default_seg = cpu->ds;  /* Default segment is DS */

	/* mod = 11 is register mode, not memory */
	if (mod == 3) {
		return 0;
	}

	/* Calculate base address from r/m field */
	switch (rm) {
		case 0:  /* [BX + SI] */
			ea = cpu->bx.w + cpu->si;
			break;
		case 1:  /* [BX + DI] */
			ea = cpu->bx.w + cpu->di;
			break;
		case 2:  /* [BP + SI] */
			ea = cpu->bp + cpu->si;
			*default_seg = cpu->ss;  /* BP uses SS by default */
			break;
		case 3:  /* [BP + DI] */
			ea = cpu->bp + cpu->di;
			*default_seg = cpu->ss;  /* BP uses SS by default */
			break;
		case 4:  /* [SI] */
			ea = cpu->si;
			break;
		case 5:  /* [DI] */
			ea = cpu->di;
			break;
		case 6:
			if (mod == 0) {
				/* [direct address] - 16-bit displacement only */
				ea = disp;
				/* Apply segment override if present */
				if (cpu->seg_override != 0) {
					uint16_t seg = (cpu->seg_override == 1) ? cpu->es :
					               (cpu->seg_override == 2) ? cpu->cs :
					               (cpu->seg_override == 3) ? cpu->ss : cpu->ds;
					return cpu_calc_addr(seg, ea & 0xFFFF);
				}
				return cpu_calc_addr(*default_seg, ea & 0xFFFF);
			} else {
				/* [BP + disp] */
				ea = cpu->bp;
				*default_seg = cpu->ss;  /* BP uses SS by default */
			}
			break;
		case 7:  /* [BX] */
			ea = cpu->bx.w;
			break;
	}

	/* Add displacement based on mod */
	if (mod == 1) {
		/* 8-bit displacement (sign-extended) */
		ea += (int8_t)disp;
	} else if (mod == 2) {
		/* 16-bit displacement */
		ea += disp;
	}

	/* Mask to 16 bits and apply segment override if present */
	ea &= 0xFFFF;

	/* Apply segment override if set, otherwise use default segment */
	if (cpu->seg_override != 0) {
		uint16_t seg = (cpu->seg_override == 1) ? cpu->es :
		               (cpu->seg_override == 2) ? cpu->cs :
		               (cpu->seg_override == 3) ? cpu->ss : cpu->ds;
		return cpu_calc_addr(seg, ea);
	}

	return cpu_calc_addr(*default_seg, ea);
}

/* Decode ModR/M byte and return operand information
 * addr: Address of ModR/M byte
 * Returns: Decoded ModR/M structure
 */
static inline ModRM decode_modrm(X86Cpu *cpu, uint32_t addr)
{
	ModRM modrm = {0};
	uint8_t byte = cpu_read_byte(cpu, addr);
	uint16_t default_seg;

	modrm.reg = MODRM_REG(byte);
	modrm.rm = MODRM_RM(byte);
	uint8_t mod = MODRM_MOD(byte);

	/* Start with 1 byte for the ModR/M byte itself */
	modrm.length = 1;

	/* Determine addressing mode */
	if (mod == 3) {
		/* Register mode */
		modrm.mode = ADDR_MODE_REGISTER;
		modrm.is_memory = false;
		modrm.has_displacement = false;
	} else if (mod == 0) {
		/* Indirect mode (no displacement, except special case) */
		modrm.mode = ADDR_MODE_INDIRECT;
		modrm.is_memory = true;

		if (modrm.rm == 6) {
			/* Special case: direct addressing [disp16] */
			modrm.displacement = cpu_read_word(cpu, addr + 1);
			modrm.has_displacement = true;
			modrm.length += 2;
		} else {
			modrm.has_displacement = false;
		}
	} else if (mod == 1) {
		/* Indirect mode with 8-bit displacement */
		modrm.mode = ADDR_MODE_INDIRECT_DISP8;
		modrm.is_memory = true;
		modrm.displacement = cpu_read_byte(cpu, addr + 1);
		modrm.has_displacement = true;
		modrm.length += 1;
	} else {  /* mod == 2 */
		/* Indirect mode with 16-bit displacement */
		modrm.mode = ADDR_MODE_INDIRECT_DISP16;
		modrm.is_memory = true;
		modrm.displacement = cpu_read_word(cpu, addr + 1);
		modrm.has_displacement = true;
		modrm.length += 2;
	}

	/* Calculate effective address if memory mode */
	if (modrm.is_memory) {
		modrm.ea = calc_ea(cpu, mod, modrm.rm, modrm.displacement,
		                   &default_seg);
	}

	return modrm;
}

/* Helper functions for flag manipulation */

static inline void jmpf(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint16_t new_ip = cpu_read_word(cpu, pc + 1);
	uint16_t new_cs = cpu_read_word(cpu, pc + 3);

	cpu->ip = new_ip;
	cpu->cs = new_cs;

	uint32_t target = cpu_get_pc(cpu);
	printf("JMP FAR %04X:%04X (0x%08X)", new_cs, new_ip, target);
}

static inline void set_flag(X86Cpu *cpu, uint16_t flag)
{
	cpu->flags |= flag;
}

static inline void clear_flag(X86Cpu *cpu, uint16_t flag)
{
	cpu->flags &= ~flag;
}


/* Flag checking functions */

static inline void chk_parity(X86Cpu *cpu, uint16_t data)
{
	/* Count number of 1 bits in low byte */
	uint8_t byte = data & 0xFF;
	byte ^= (byte >> 4);
	byte ^= (byte >> 2);
	byte ^= (byte >> 1);

	/* Parity flag is set if even number of 1 bits (result is 0) */
	if (byte & 0x1)
		clear_flag(cpu, FLAGS_PF);
	else
		set_flag(cpu, FLAGS_PF);
}

static inline void chk_zero(X86Cpu *cpu, uint16_t data)
{
	if (data == 0)
		set_flag(cpu, FLAGS_ZF);
	else
		clear_flag(cpu, FLAGS_ZF);
}

static inline void chk_sign(X86Cpu *cpu, uint16_t data, bool is_byte)
{
	/* Check sign bit (bit 7 for byte, bit 15 for word) */
	if (is_byte) {
		if (data & 0x80)
			set_flag(cpu, FLAGS_SF);
		else
			clear_flag(cpu, FLAGS_SF);
	} else {
		if (data & 0x8000)
			set_flag(cpu, FLAGS_SF);
		else
			clear_flag(cpu, FLAGS_SF);
	}
}

/* Update arithmetic flags (ZF, SF, PF) */
static inline void update_flags_szp(X86Cpu *cpu, uint16_t result, bool is_byte)
{
	chk_zero(cpu, is_byte ? (result & 0xFF) : result);
	chk_sign(cpu, result, is_byte);
	chk_parity(cpu, result);
}

/* Check for carry in addition */
static inline void chk_carry_add(X86Cpu *cpu, uint32_t result, bool is_byte)
{
	if (is_byte) {
		if (result > 0xFF)
			set_flag(cpu, FLAGS_CF);
		else
			clear_flag(cpu, FLAGS_CF);
	} else {
		if (result > 0xFFFF)
			set_flag(cpu, FLAGS_CF);
		else
			clear_flag(cpu, FLAGS_CF);
	}
}

/* Check for overflow in signed addition */
static inline void chk_overflow_add(X86Cpu *cpu, uint16_t src, uint16_t dst,
                                     uint16_t result, bool is_byte)
{
	/* Overflow occurs when adding two numbers of the same sign
	 * produces a result with a different sign */
	if (is_byte) {
		bool src_sign = (src & 0x80) != 0;
		bool dst_sign = (dst & 0x80) != 0;
		bool res_sign = (result & 0x80) != 0;
		if (src_sign == dst_sign && src_sign != res_sign)
			set_flag(cpu, FLAGS_OV);
		else
			clear_flag(cpu, FLAGS_OV);
	} else {
		bool src_sign = (src & 0x8000) != 0;
		bool dst_sign = (dst & 0x8000) != 0;
		bool res_sign = (result & 0x8000) != 0;
		if (src_sign == dst_sign && src_sign != res_sign)
			set_flag(cpu, FLAGS_OV);
		else
			clear_flag(cpu, FLAGS_OV);
	}
}

/* Check for overflow in signed subtraction */
static inline void chk_overflow_sub(X86Cpu *cpu, uint16_t src, uint16_t dst,
                                     uint16_t result, bool is_byte)
{
	/* Overflow occurs when subtracting numbers of different signs
	 * produces a result with a sign different from the destination */
	if (is_byte) {
		bool src_sign = (src & 0x80) != 0;
		bool dst_sign = (dst & 0x80) != 0;
		bool res_sign = (result & 0x80) != 0;
		if (src_sign != dst_sign && dst_sign != res_sign)
			set_flag(cpu, FLAGS_OV);
		else
			clear_flag(cpu, FLAGS_OV);
	} else {
		bool src_sign = (src & 0x8000) != 0;
		bool dst_sign = (dst & 0x8000) != 0;
		bool res_sign = (result & 0x8000) != 0;
		if (src_sign != dst_sign && dst_sign != res_sign)
			set_flag(cpu, FLAGS_OV);
		else
			clear_flag(cpu, FLAGS_OV);
	}
}

/* Check for auxiliary carry (half-carry) */
static inline void chk_aux_carry_add(X86Cpu *cpu, uint8_t src, uint8_t dst)
{
	/* Auxiliary carry is set if there's a carry from bit 3 to bit 4 */
	if (((src & 0x0F) + (dst & 0x0F)) > 0x0F)
		set_flag(cpu, FLAGS_AF);
	else
		clear_flag(cpu, FLAGS_AF);
}

static inline void chk_aux_carry_sub(X86Cpu *cpu, uint8_t src, uint8_t dst)
{
	/* Auxiliary carry is set if there's a borrow from bit 4 */
	if ((dst & 0x0F) < (src & 0x0F))
		set_flag(cpu, FLAGS_AF);
	else
		clear_flag(cpu, FLAGS_AF);
}

/* Arithmetic Instructions */

/* ADD - Add (0x00-0x05) */
static inline void add_op(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	bool is_byte = !(opcode & 0x01);  /* Bit 0: 0=byte, 1=word */
	bool direction = opcode & 0x02;    /* Bit 1: 0=reg is dest, 1=reg is src */

	if (opcode <= 0x03) {
		/* ADD with ModR/M */
		ModRM modrm = decode_modrm(cpu, pc + 1);
		uint16_t src, dst;
		uint32_t result;

		if (is_byte) {
			if (direction) {
				/* reg = reg + r/m */
				uint8_t *reg_ptr = get_reg8_ptr(cpu, modrm.reg);
				dst = *reg_ptr;
				src = modrm.is_memory ? cpu_read_byte(cpu, modrm.ea) :
				      *get_reg8_ptr(cpu, modrm.rm);
				result = (uint32_t)dst + (uint32_t)src;
				*reg_ptr = result & 0xFF;
			} else {
				/* r/m = r/m + reg */
				uint8_t *reg_ptr = get_reg8_ptr(cpu, modrm.reg);
				src = *reg_ptr;
				if (modrm.is_memory) {
					dst = cpu_read_byte(cpu, modrm.ea);
					result = (uint32_t)dst + (uint32_t)src;
					cpu_write_byte(cpu, modrm.ea, result & 0xFF);
				} else {
					uint8_t *rm_ptr = get_reg8_ptr(cpu, modrm.rm);
					dst = *rm_ptr;
					result = (uint32_t)dst + (uint32_t)src;
					*rm_ptr = result & 0xFF;
				}
			}
		} else {
			if (direction) {
				/* reg = reg + r/m */
				uint16_t *reg_ptr = get_reg16_ptr(cpu, modrm.reg);
				dst = *reg_ptr;
				src = modrm.is_memory ? cpu_read_word(cpu, modrm.ea) :
				      *get_reg16_ptr(cpu, modrm.rm);
				result = (uint32_t)dst + (uint32_t)src;
				*reg_ptr = result & 0xFFFF;
			} else {
				/* r/m = r/m + reg */
				uint16_t *reg_ptr = get_reg16_ptr(cpu, modrm.reg);
				src = *reg_ptr;
				if (modrm.is_memory) {
					dst = cpu_read_word(cpu, modrm.ea);
					result = (uint32_t)dst + (uint32_t)src;
					cpu_write_word(cpu, modrm.ea, result & 0xFFFF);
				} else {
					uint16_t *rm_ptr = get_reg16_ptr(cpu, modrm.rm);
					dst = *rm_ptr;
					result = (uint32_t)dst + (uint32_t)src;
					*rm_ptr = result & 0xFFFF;
				}
			}
		}

		/* Update flags */
		chk_carry_add(cpu, result, is_byte);
		chk_overflow_add(cpu, src, dst, result, is_byte);
		chk_aux_carry_add(cpu, src & 0xFF, dst & 0xFF);
		update_flags_szp(cpu, result, is_byte);

		cpu->ip += 1 + modrm.length;
	} else if (opcode == 0x04) {
		/* ADD AL, imm8 */
		uint8_t imm = cpu_read_byte(cpu, pc + 1);
		uint8_t dst = cpu->ax.l;
		uint16_t result = dst + imm;
		cpu->ax.l = result & 0xFF;

		chk_carry_add(cpu, result, true);
		chk_overflow_add(cpu, imm, dst, result, true);
		chk_aux_carry_add(cpu, imm, dst);
		update_flags_szp(cpu, result, true);

		cpu->ip += 2;
	} else {  /* opcode == 0x05 */
		/* ADD AX, imm16 */
		uint16_t imm = cpu_read_word(cpu, pc + 1);
		uint16_t dst = cpu->ax.w;
		uint32_t result = dst + imm;
		cpu->ax.w = result & 0xFFFF;

		chk_carry_add(cpu, result, false);
		chk_overflow_add(cpu, imm, dst, result, false);
		chk_aux_carry_add(cpu, imm & 0xFF, dst & 0xFF);
		update_flags_szp(cpu, result, false);

		cpu->ip += 3;
	}
}

/* SUB - Subtract (0x28-0x2D) */
static inline void sub_op(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	bool is_byte = !(opcode & 0x01);
	bool direction = opcode & 0x02;

	if (opcode <= 0x2B) {
		/* SUB with ModR/M */
		ModRM modrm = decode_modrm(cpu, pc + 1);
		uint16_t src, dst, result;

		if (is_byte) {
			if (direction) {
				/* reg = reg - r/m */
				uint8_t *reg_ptr = get_reg8_ptr(cpu, modrm.reg);
				dst = *reg_ptr;
				src = modrm.is_memory ? cpu_read_byte(cpu, modrm.ea) :
				      *get_reg8_ptr(cpu, modrm.rm);
				result = dst - src;
				*reg_ptr = result & 0xFF;
			} else {
				/* r/m = r/m - reg */
				uint8_t *reg_ptr = get_reg8_ptr(cpu, modrm.reg);
				src = *reg_ptr;
				if (modrm.is_memory) {
					dst = cpu_read_byte(cpu, modrm.ea);
					result = dst - src;
					cpu_write_byte(cpu, modrm.ea, result & 0xFF);
				} else {
					uint8_t *rm_ptr = get_reg8_ptr(cpu, modrm.rm);
					dst = *rm_ptr;
					result = dst - src;
					*rm_ptr = result & 0xFF;
				}
			}
		} else {
			if (direction) {
				/* reg = reg - r/m */
				uint16_t *reg_ptr = get_reg16_ptr(cpu, modrm.reg);
				dst = *reg_ptr;
				src = modrm.is_memory ? cpu_read_word(cpu, modrm.ea) :
				      *get_reg16_ptr(cpu, modrm.rm);
				result = dst - src;
				*reg_ptr = result;
			} else {
				/* r/m = r/m - reg */
				uint16_t *reg_ptr = get_reg16_ptr(cpu, modrm.reg);
				src = *reg_ptr;
				if (modrm.is_memory) {
					dst = cpu_read_word(cpu, modrm.ea);
					result = dst - src;
					cpu_write_word(cpu, modrm.ea, result);
				} else {
					uint16_t *rm_ptr = get_reg16_ptr(cpu, modrm.rm);
					dst = *rm_ptr;
					result = dst - src;
					*rm_ptr = result;
				}
			}
		}

		/* Update flags - carry is set if borrow occurred */
		if (is_byte) {
			if (dst < src)
				set_flag(cpu, FLAGS_CF);
			else
				clear_flag(cpu, FLAGS_CF);
		} else {
			if (dst < src)
				set_flag(cpu, FLAGS_CF);
			else
				clear_flag(cpu, FLAGS_CF);
		}
		chk_overflow_sub(cpu, src, dst, result, is_byte);
		chk_aux_carry_sub(cpu, src & 0xFF, dst & 0xFF);
		update_flags_szp(cpu, result, is_byte);

		cpu->ip += 1 + modrm.length;
	} else if (opcode == 0x2C) {
		/* SUB AL, imm8 */
		uint8_t imm = cpu_read_byte(cpu, pc + 1);
		uint8_t dst = cpu->ax.l;
		uint16_t result = dst - imm;
		cpu->ax.l = result & 0xFF;

		if (dst < imm)
			set_flag(cpu, FLAGS_CF);
		else
			clear_flag(cpu, FLAGS_CF);
		chk_overflow_sub(cpu, imm, dst, result, true);
		chk_aux_carry_sub(cpu, imm, dst);
		update_flags_szp(cpu, result, true);

		cpu->ip += 2;
	} else {  /* opcode == 0x2D */
		/* SUB AX, imm16 */
		uint16_t imm = cpu_read_word(cpu, pc + 1);
		uint16_t dst = cpu->ax.w;
		uint32_t result = dst - imm;
		cpu->ax.w = result & 0xFFFF;

		if (dst < imm)
			set_flag(cpu, FLAGS_CF);
		else
			clear_flag(cpu, FLAGS_CF);
		chk_overflow_sub(cpu, imm, dst, result, false);
		chk_aux_carry_sub(cpu, imm & 0xFF, dst & 0xFF);
		update_flags_szp(cpu, result, false);

		cpu->ip += 3;
	}
}

/* CMP - Compare (0x38-0x3D) */
static inline void cmp_op(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	bool is_byte = !(opcode & 0x01);
	bool direction = opcode & 0x02;

	if (opcode <= 0x3B) {
		/* CMP with ModR/M */
		ModRM modrm = decode_modrm(cpu, pc + 1);
		uint16_t src, dst, result;

		if (is_byte) {
			if (direction) {
				/* Compare reg with r/m */
				uint8_t *reg_ptr = get_reg8_ptr(cpu, modrm.reg);
				dst = *reg_ptr;
				src = modrm.is_memory ? cpu_read_byte(cpu, modrm.ea) :
				      *get_reg8_ptr(cpu, modrm.rm);
			} else {
				/* Compare r/m with reg */
				uint8_t *reg_ptr = get_reg8_ptr(cpu, modrm.reg);
				src = *reg_ptr;
				dst = modrm.is_memory ? cpu_read_byte(cpu, modrm.ea) :
				      *get_reg8_ptr(cpu, modrm.rm);
			}
			result = dst - src;
		} else {
			if (direction) {
				/* Compare reg with r/m */
				uint16_t *reg_ptr = get_reg16_ptr(cpu, modrm.reg);
				dst = *reg_ptr;
				src = modrm.is_memory ? cpu_read_word(cpu, modrm.ea) :
				      *get_reg16_ptr(cpu, modrm.rm);
			} else {
				/* Compare r/m with reg */
				uint16_t *reg_ptr = get_reg16_ptr(cpu, modrm.reg);
				src = *reg_ptr;
				dst = modrm.is_memory ? cpu_read_word(cpu, modrm.ea) :
				      *get_reg16_ptr(cpu, modrm.rm);
			}
			result = dst - src;
		}

		/* Update flags (like SUB but don't store result) */
		if (dst < src)
			set_flag(cpu, FLAGS_CF);
		else
			clear_flag(cpu, FLAGS_CF);
		chk_overflow_sub(cpu, src, dst, result, is_byte);
		chk_aux_carry_sub(cpu, src & 0xFF, dst & 0xFF);
		update_flags_szp(cpu, result, is_byte);

		cpu->ip += 1 + modrm.length;
	} else if (opcode == 0x3C) {
		/* CMP AL, imm8 */
		uint8_t imm = cpu_read_byte(cpu, pc + 1);
		uint8_t dst = cpu->ax.l;
		uint16_t result = dst - imm;

		if (dst < imm)
			set_flag(cpu, FLAGS_CF);
		else
			clear_flag(cpu, FLAGS_CF);
		chk_overflow_sub(cpu, imm, dst, result, true);
		chk_aux_carry_sub(cpu, imm, dst);
		update_flags_szp(cpu, result, true);

		cpu->ip += 2;
	} else {  /* opcode == 0x3D */
		/* CMP AX, imm16 */
		uint16_t imm = cpu_read_word(cpu, pc + 1);
		uint16_t dst = cpu->ax.w;
		uint32_t result = dst - imm;

		if (dst < imm)
			set_flag(cpu, FLAGS_CF);
		else
			clear_flag(cpu, FLAGS_CF);
		chk_overflow_sub(cpu, imm, dst, result, false);
		chk_aux_carry_sub(cpu, imm & 0xFF, dst & 0xFF);
		update_flags_szp(cpu, result, false);

		cpu->ip += 3;
	}
}

/* INC - Increment (0x40-0x47 for 16-bit registers) */
static inline void inc_reg16(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	uint8_t reg = opcode & 0x07;
	uint16_t *reg_ptr = get_reg16_ptr(cpu, reg);
	uint16_t dst = *reg_ptr;
	uint16_t result = dst + 1;
	*reg_ptr = result;

	/* INC affects OF, SF, ZF, AF, PF but not CF */
	chk_overflow_add(cpu, 1, dst, result, false);
	chk_aux_carry_add(cpu, 1, dst & 0xFF);
	update_flags_szp(cpu, result, false);

	cpu->ip++;
}

/* DEC - Decrement (0x48-0x4F for 16-bit registers) */
static inline void dec_reg16(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	uint8_t reg = opcode & 0x07;
	uint16_t *reg_ptr = get_reg16_ptr(cpu, reg);
	uint16_t dst = *reg_ptr;
	uint16_t result = dst - 1;
	*reg_ptr = result;

	/* DEC affects OF, SF, ZF, AF, PF but not CF */
	chk_overflow_sub(cpu, 1, dst, result, false);
	chk_aux_carry_sub(cpu, 1, dst & 0xFF);
	update_flags_szp(cpu, result, false);

	cpu->ip++;
}

/* Logical Instructions */

/* Update flags for logical operations (CF=0, OF=0, AF=0, SF/ZF/PF set) */
static inline void update_flags_logic(X86Cpu *cpu, uint16_t result, bool is_byte)
{
	clear_flag(cpu, FLAGS_CF);
	clear_flag(cpu, FLAGS_OV);
	clear_flag(cpu, FLAGS_AF);  /* AF is cleared for logical operations */
	update_flags_szp(cpu, result, is_byte);
}

/* OR - Logical OR (0x08-0x0D) */
static inline void or_op(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	bool is_byte = !(opcode & 0x01);
	bool direction = opcode & 0x02;

	if (opcode <= 0x0B) {
		/* OR with ModR/M */
		ModRM modrm = decode_modrm(cpu, pc + 1);
		uint16_t src, dst, result;

		if (is_byte) {
			if (direction) {
				uint8_t *reg_ptr = get_reg8_ptr(cpu, modrm.reg);
				dst = *reg_ptr;
				src = modrm.is_memory ? cpu_read_byte(cpu, modrm.ea) :
				      *get_reg8_ptr(cpu, modrm.rm);
				result = dst | src;
				*reg_ptr = result & 0xFF;
			} else {
				uint8_t *reg_ptr = get_reg8_ptr(cpu, modrm.reg);
				src = *reg_ptr;
				if (modrm.is_memory) {
					dst = cpu_read_byte(cpu, modrm.ea);
					result = dst | src;
					cpu_write_byte(cpu, modrm.ea, result & 0xFF);
				} else {
					uint8_t *rm_ptr = get_reg8_ptr(cpu, modrm.rm);
					dst = *rm_ptr;
					result = dst | src;
					*rm_ptr = result & 0xFF;
				}
			}
		} else {
			if (direction) {
				uint16_t *reg_ptr = get_reg16_ptr(cpu, modrm.reg);
				dst = *reg_ptr;
				src = modrm.is_memory ? cpu_read_word(cpu, modrm.ea) :
				      *get_reg16_ptr(cpu, modrm.rm);
				result = dst | src;
				*reg_ptr = result;
			} else {
				uint16_t *reg_ptr = get_reg16_ptr(cpu, modrm.reg);
				src = *reg_ptr;
				if (modrm.is_memory) {
					dst = cpu_read_word(cpu, modrm.ea);
					result = dst | src;
					cpu_write_word(cpu, modrm.ea, result);
				} else {
					uint16_t *rm_ptr = get_reg16_ptr(cpu, modrm.rm);
					dst = *rm_ptr;
					result = dst | src;
					*rm_ptr = result;
				}
			}
		}

		update_flags_logic(cpu, result, is_byte);
		cpu->ip += 1 + modrm.length;
	} else if (opcode == 0x0C) {
		/* OR AL, imm8 */
		uint8_t imm = cpu_read_byte(cpu, pc + 1);
		uint16_t result = cpu->ax.l | imm;
		cpu->ax.l = result & 0xFF;
		update_flags_logic(cpu, result, true);
		cpu->ip += 2;
	} else {  /* opcode == 0x0D */
		/* OR AX, imm16 */
		uint16_t imm = cpu_read_word(cpu, pc + 1);
		uint16_t result = cpu->ax.w | imm;
		cpu->ax.w = result;
		update_flags_logic(cpu, result, false);
		cpu->ip += 3;
	}
}

/* AND - Logical AND (0x20-0x25) */
static inline void and_op(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	bool is_byte = !(opcode & 0x01);
	bool direction = opcode & 0x02;

	if (opcode <= 0x23) {
		/* AND with ModR/M */
		ModRM modrm = decode_modrm(cpu, pc + 1);
		uint16_t src, dst, result;

		if (is_byte) {
			if (direction) {
				uint8_t *reg_ptr = get_reg8_ptr(cpu, modrm.reg);
				dst = *reg_ptr;
				src = modrm.is_memory ? cpu_read_byte(cpu, modrm.ea) :
				      *get_reg8_ptr(cpu, modrm.rm);
				result = dst & src;
				*reg_ptr = result & 0xFF;
			} else {
				uint8_t *reg_ptr = get_reg8_ptr(cpu, modrm.reg);
				src = *reg_ptr;
				if (modrm.is_memory) {
					dst = cpu_read_byte(cpu, modrm.ea);
					result = dst & src;
					cpu_write_byte(cpu, modrm.ea, result & 0xFF);
				} else {
					uint8_t *rm_ptr = get_reg8_ptr(cpu, modrm.rm);
					dst = *rm_ptr;
					result = dst & src;
					*rm_ptr = result & 0xFF;
				}
			}
		} else {
			if (direction) {
				uint16_t *reg_ptr = get_reg16_ptr(cpu, modrm.reg);
				dst = *reg_ptr;
				src = modrm.is_memory ? cpu_read_word(cpu, modrm.ea) :
				      *get_reg16_ptr(cpu, modrm.rm);
				result = dst & src;
				*reg_ptr = result;
			} else {
				uint16_t *reg_ptr = get_reg16_ptr(cpu, modrm.reg);
				src = *reg_ptr;
				if (modrm.is_memory) {
					dst = cpu_read_word(cpu, modrm.ea);
					result = dst & src;
					cpu_write_word(cpu, modrm.ea, result);
				} else {
					uint16_t *rm_ptr = get_reg16_ptr(cpu, modrm.rm);
					dst = *rm_ptr;
					result = dst & src;
					*rm_ptr = result;
				}
			}
		}

		update_flags_logic(cpu, result, is_byte);
		cpu->ip += 1 + modrm.length;
	} else if (opcode == 0x24) {
		/* AND AL, imm8 */
		uint8_t imm = cpu_read_byte(cpu, pc + 1);
		uint16_t result = cpu->ax.l & imm;
		cpu->ax.l = result & 0xFF;
		update_flags_logic(cpu, result, true);
		cpu->ip += 2;
	} else {  /* opcode == 0x25 */
		/* AND AX, imm16 */
		uint16_t imm = cpu_read_word(cpu, pc + 1);
		uint16_t result = cpu->ax.w & imm;
		cpu->ax.w = result;
		update_flags_logic(cpu, result, false);
		cpu->ip += 3;
	}
}

/* XOR - Logical XOR (0x30-0x35) */
static inline void xor_op(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	bool is_byte = !(opcode & 0x01);
	bool direction = opcode & 0x02;

	if (opcode <= 0x33) {
		/* XOR with ModR/M */
		ModRM modrm = decode_modrm(cpu, pc + 1);
		uint16_t src, dst, result;

		if (is_byte) {
			if (direction) {
				uint8_t *reg_ptr = get_reg8_ptr(cpu, modrm.reg);
				dst = *reg_ptr;
				src = modrm.is_memory ? cpu_read_byte(cpu, modrm.ea) :
				      *get_reg8_ptr(cpu, modrm.rm);
				result = dst ^ src;
				*reg_ptr = result & 0xFF;
			} else {
				uint8_t *reg_ptr = get_reg8_ptr(cpu, modrm.reg);
				src = *reg_ptr;
				if (modrm.is_memory) {
					dst = cpu_read_byte(cpu, modrm.ea);
					result = dst ^ src;
					cpu_write_byte(cpu, modrm.ea, result & 0xFF);
				} else {
					uint8_t *rm_ptr = get_reg8_ptr(cpu, modrm.rm);
					dst = *rm_ptr;
					result = dst ^ src;
					*rm_ptr = result & 0xFF;
				}
			}
		} else {
			if (direction) {
				uint16_t *reg_ptr = get_reg16_ptr(cpu, modrm.reg);
				dst = *reg_ptr;
				src = modrm.is_memory ? cpu_read_word(cpu, modrm.ea) :
				      *get_reg16_ptr(cpu, modrm.rm);
				result = dst ^ src;
				*reg_ptr = result;
			} else {
				uint16_t *reg_ptr = get_reg16_ptr(cpu, modrm.reg);
				src = *reg_ptr;
				if (modrm.is_memory) {
					dst = cpu_read_word(cpu, modrm.ea);
					result = dst ^ src;
					cpu_write_word(cpu, modrm.ea, result);
				} else {
					uint16_t *rm_ptr = get_reg16_ptr(cpu, modrm.rm);
					dst = *rm_ptr;
					result = dst ^ src;
					*rm_ptr = result;
				}
			}
		}

		update_flags_logic(cpu, result, is_byte);
		cpu->ip += 1 + modrm.length;
	} else if (opcode == 0x34) {
		/* XOR AL, imm8 */
		uint8_t imm = cpu_read_byte(cpu, pc + 1);
		uint16_t result = cpu->ax.l ^ imm;
		cpu->ax.l = result & 0xFF;
		update_flags_logic(cpu, result, true);
		cpu->ip += 2;
	} else {  /* opcode == 0x35 */
		/* XOR AX, imm16 */
		uint16_t imm = cpu_read_word(cpu, pc + 1);
		uint16_t result = cpu->ax.w ^ imm;
		cpu->ax.w = result;
		update_flags_logic(cpu, result, false);
		cpu->ip += 3;
	}
}

/* TEST - Logical compare (0x84-0x85, 0xA8-0xA9) */
static inline void test_op(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	bool is_byte = !(opcode & 0x01);

	if (opcode <= 0x85) {
		/* TEST with ModR/M (0x84-0x85) */
		ModRM modrm = decode_modrm(cpu, pc + 1);
		uint16_t src, dst, result;

		if (is_byte) {
			uint8_t *reg_ptr = get_reg8_ptr(cpu, modrm.reg);
			src = *reg_ptr;
			dst = modrm.is_memory ? cpu_read_byte(cpu, modrm.ea) :
			      *get_reg8_ptr(cpu, modrm.rm);
			result = dst & src;
		} else {
			uint16_t *reg_ptr = get_reg16_ptr(cpu, modrm.reg);
			src = *reg_ptr;
			dst = modrm.is_memory ? cpu_read_word(cpu, modrm.ea) :
			      *get_reg16_ptr(cpu, modrm.rm);
			result = dst & src;
		}

		update_flags_logic(cpu, result, is_byte);
		cpu->ip += 1 + modrm.length;
	} else if (opcode == 0xA8) {
		/* TEST AL, imm8 */
		uint8_t imm = cpu_read_byte(cpu, pc + 1);
		uint16_t result = cpu->ax.l & imm;
		update_flags_logic(cpu, result, true);
		cpu->ip += 2;
	} else {  /* opcode == 0xA9 */
		/* TEST AX, imm16 */
		uint16_t imm = cpu_read_word(cpu, pc + 1);
		uint16_t result = cpu->ax.w & imm;
		update_flags_logic(cpu, result, false);
		cpu->ip += 3;
	}
}

/* Shift/Rotate Instructions (0xD0-0xD3) */

/* Shift/Rotate group handler */
static inline void shift_rotate_op(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	bool is_byte = !(opcode & 0x01);
	bool use_cl = (opcode & 0x02);  /* 0=count is 1, 1=count is CL */
	ModRM modrm = decode_modrm(cpu, pc + 1);
	uint8_t count = use_cl ? cpu->cx.l : 1;
	uint8_t operation = modrm.reg;  /* Bits 5-3 specify the operation */
	uint16_t value, result;
	bool new_cf, old_cf;

	/* Mask count to 5 bits (8086 behavior) */
	count &= 0x1F;

	/* Get operand value */
	if (is_byte) {
		if (modrm.is_memory)
			value = cpu_read_byte(cpu, modrm.ea);
		else
			value = *get_reg8_ptr(cpu, modrm.rm);
	} else {
		if (modrm.is_memory)
			value = cpu_read_word(cpu, modrm.ea);
		else
			value = *get_reg16_ptr(cpu, modrm.rm);
	}

	result = value;
	old_cf = FLAG_TST(FLAGS_CF);

	/* Perform the operation based on reg field */
	switch (operation) {
		case 0:  /* ROL - Rotate Left */
			for (uint8_t i = 0; i < count; i++) {
				if (is_byte) {
					new_cf = (result & 0x80) != 0;
					result = ((result << 1) | (new_cf ? 1 : 0)) & 0xFF;
				} else {
					new_cf = (result & 0x8000) != 0;
					result = ((result << 1) | (new_cf ? 1 : 0)) & 0xFFFF;
				}
				if (new_cf)
					set_flag(cpu, FLAGS_CF);
				else
					clear_flag(cpu, FLAGS_CF);
			}
			/* OF is set if sign bit changed on single-bit rotate */
			if (count == 1) {
				bool new_msb = is_byte ? (result & 0x80) != 0 : (result & 0x8000) != 0;
				if (new_msb != FLAG_TST(FLAGS_CF))
					set_flag(cpu, FLAGS_OV);
				else
					clear_flag(cpu, FLAGS_OV);
			}
			break;

		case 1:  /* ROR - Rotate Right */
			for (uint8_t i = 0; i < count; i++) {
				new_cf = (result & 0x01) != 0;
				if (is_byte)
					result = ((result >> 1) | (new_cf ? 0x80 : 0)) & 0xFF;
				else
					result = ((result >> 1) | (new_cf ? 0x8000 : 0)) & 0xFFFF;
				if (new_cf)
					set_flag(cpu, FLAGS_CF);
				else
					clear_flag(cpu, FLAGS_CF);
			}
			if (count == 1) {
				bool msb = is_byte ? (result & 0x80) != 0 : (result & 0x8000) != 0;
				bool next_msb = is_byte ? (result & 0x40) != 0 : (result & 0x4000) != 0;
				if (msb != next_msb)
					set_flag(cpu, FLAGS_OV);
				else
					clear_flag(cpu, FLAGS_OV);
			}
			break;

		case 2:  /* RCL - Rotate through Carry Left */
			for (uint8_t i = 0; i < count; i++) {
				if (is_byte) {
					new_cf = (result & 0x80) != 0;
					result = ((result << 1) | (old_cf ? 1 : 0)) & 0xFF;
				} else {
					new_cf = (result & 0x8000) != 0;
					result = ((result << 1) | (old_cf ? 1 : 0)) & 0xFFFF;
				}
				old_cf = new_cf;
				if (new_cf)
					set_flag(cpu, FLAGS_CF);
				else
					clear_flag(cpu, FLAGS_CF);
			}
			if (count == 1) {
				bool new_msb = is_byte ? (result & 0x80) != 0 : (result & 0x8000) != 0;
				if (new_msb != FLAG_TST(FLAGS_CF))
					set_flag(cpu, FLAGS_OV);
				else
					clear_flag(cpu, FLAGS_OV);
			}
			break;

		case 3:  /* RCR - Rotate through Carry Right */
			for (uint8_t i = 0; i < count; i++) {
				new_cf = (result & 0x01) != 0;
				if (is_byte)
					result = ((result >> 1) | (old_cf ? 0x80 : 0)) & 0xFF;
				else
					result = ((result >> 1) | (old_cf ? 0x8000 : 0)) & 0xFFFF;
				old_cf = new_cf;
				if (new_cf)
					set_flag(cpu, FLAGS_CF);
				else
					clear_flag(cpu, FLAGS_CF);
			}
			if (count == 1) {
				bool msb = is_byte ? (result & 0x80) != 0 : (result & 0x8000) != 0;
				bool next_msb = is_byte ? (result & 0x40) != 0 : (result & 0x4000) != 0;
				if (msb != next_msb)
					set_flag(cpu, FLAGS_OV);
				else
					clear_flag(cpu, FLAGS_OV);
			}
			break;

		case 4:  /* SHL/SAL - Shift Left */
		case 6:  /* SAL is the same as SHL */
			if (count > 0) {
				for (uint8_t i = 0; i < count; i++) {
					if (is_byte)
						new_cf = (result & 0x80) != 0;
					else
						new_cf = (result & 0x8000) != 0;
					result = is_byte ? ((result << 1) & 0xFF) : ((result << 1) & 0xFFFF);
				}
				if (new_cf)
					set_flag(cpu, FLAGS_CF);
				else
					clear_flag(cpu, FLAGS_CF);
				update_flags_szp(cpu, result, is_byte);
				/* Clear AF for all shift operations */
				clear_flag(cpu, FLAGS_AF);
				if (count == 1) {
					bool msb = is_byte ? (result & 0x80) != 0 : (result & 0x8000) != 0;
					if (msb != new_cf)
						set_flag(cpu, FLAGS_OV);
					else
						clear_flag(cpu, FLAGS_OV);
				} else {
					/* OF is cleared for multi-bit shifts */
					clear_flag(cpu, FLAGS_OV);
				}
			}
			break;

		case 5:  /* SHR - Shift Right */
			if (count > 0) {
				for (uint8_t i = 0; i < count; i++) {
					new_cf = (result & 0x01) != 0;
					result >>= 1;
				}
				if (new_cf)
					set_flag(cpu, FLAGS_CF);
				else
					clear_flag(cpu, FLAGS_CF);
				update_flags_szp(cpu, result, is_byte);
				/* Clear AF for all shift operations */
				clear_flag(cpu, FLAGS_AF);
				if (count == 1) {
					/* OF is set if MSB was set before shift */
					if (is_byte) {
						if (value & 0x80)
							set_flag(cpu, FLAGS_OV);
						else
							clear_flag(cpu, FLAGS_OV);
					} else {
						if (value & 0x8000)
							set_flag(cpu, FLAGS_OV);
						else
							clear_flag(cpu, FLAGS_OV);
					}
				} else {
					/* OF is cleared for multi-bit shifts */
					clear_flag(cpu, FLAGS_OV);
				}
			}
			break;

		case 7:  /* SAR - Shift Arithmetic Right */
			if (count > 0) {
				for (uint8_t i = 0; i < count; i++) {
					new_cf = (result & 0x01) != 0;
					if (is_byte) {
						bool sign = (result & 0x80) != 0;
						result = (result >> 1) | (sign ? 0x80 : 0);
					} else {
						bool sign = (result & 0x8000) != 0;
						result = (result >> 1) | (sign ? 0x8000 : 0);
					}
				}
				if (new_cf)
					set_flag(cpu, FLAGS_CF);
				else
					clear_flag(cpu, FLAGS_CF);
				update_flags_szp(cpu, result, is_byte);
				/* Clear AF for all shift operations */
				clear_flag(cpu, FLAGS_AF);
				/* OF is always cleared for SAR */
				if (count == 1)
					clear_flag(cpu, FLAGS_OV);
				else
					clear_flag(cpu, FLAGS_OV);
			}
			break;
	}

	/* Write result back */
	if (is_byte) {
		if (modrm.is_memory)
			cpu_write_byte(cpu, modrm.ea, result & 0xFF);
		else
			*get_reg8_ptr(cpu, modrm.rm) = result & 0xFF;
	} else {
		if (modrm.is_memory)
			cpu_write_word(cpu, modrm.ea, result);
		else
			*get_reg16_ptr(cpu, modrm.rm) = result;
	}

	cpu->ip += 1 + modrm.length;
}

/* Stack Operations */

/* Push word onto stack */
static inline void push_word(X86Cpu *cpu, uint16_t value)
{
	cpu->sp -= 2;
	uint32_t addr = cpu_calc_addr(cpu->ss, cpu->sp);
	cpu_write_word(cpu, addr, value);
}

/* Pop word from stack */
static inline uint16_t pop_word(X86Cpu *cpu)
{
	uint32_t addr = cpu_calc_addr(cpu->ss, cpu->sp);
	uint16_t value = cpu_read_word(cpu, addr);
	cpu->sp += 2;
	return value;
}

/* PUSH r16 (0x50-0x57) - Push 16-bit register */
static inline void push_reg16(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	uint8_t reg = opcode & 0x07;
	uint16_t *reg_ptr = get_reg16_ptr(cpu, reg);
	push_word(cpu, *reg_ptr);
	cpu->ip++;
}

/* POP r16 (0x58-0x5F) - Pop to 16-bit register */
static inline void pop_reg16(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	uint8_t reg = opcode & 0x07;
	uint16_t *reg_ptr = get_reg16_ptr(cpu, reg);
	*reg_ptr = pop_word(cpu);
	cpu->ip++;
}

/* PUSH segment register */
static inline void push_seg(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	uint16_t value;

	switch (opcode) {
		case 0x06:  /* PUSH ES */
			value = cpu->es;
			break;
		case 0x0E:  /* PUSH CS */
			value = cpu->cs;
			break;
		case 0x16:  /* PUSH SS */
			value = cpu->ss;
			break;
		case 0x1E:  /* PUSH DS */
			value = cpu->ds;
			break;
		default:
			fprintf(stderr, "ERROR: Invalid PUSH segment opcode 0x%02X\n", opcode);
			cpu->running = 0;
			return;
	}

	push_word(cpu, value);
	cpu->ip++;
}

/* POP segment register */
static inline void pop_seg(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	uint16_t value = pop_word(cpu);

	switch (opcode) {
		case 0x07:  /* POP ES */
			cpu->es = value;
			break;
		case 0x0F:  /* POP CS (not recommended but valid) */
			cpu->cs = value;
			break;
		case 0x17:  /* POP SS */
			cpu->ss = value;
			break;
		case 0x1F:  /* POP DS */
			cpu->ds = value;
			break;
		default:
			fprintf(stderr, "ERROR: Invalid POP segment opcode 0x%02X\n", opcode);
			cpu->running = 0;
			return;
	}

	cpu->ip++;
}

/* PUSHF (0x9C) - Push flags register */
static inline void pushf(X86Cpu *cpu)
{
	push_word(cpu, cpu->flags);
	cpu->ip++;
}

/* POPF (0x9D) - Pop flags register */
static inline void popf(X86Cpu *cpu)
{
	cpu->flags = pop_word(cpu);
	cpu->ip++;
}

/* Control Flow Instructions */

/* CALL near (0xE8) - Call procedure (relative) */
static inline void call_near(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	int16_t offset = (int16_t)cpu_read_word(cpu, pc + 1);

	/* Push return address (IP after this instruction) */
	push_word(cpu, cpu->ip + 3);

	/* Jump to target */
	cpu->ip += offset + 3;
}

/* CALL far (0x9A) - Call far procedure */
static inline void call_far(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint16_t new_ip = cpu_read_word(cpu, pc + 1);
	uint16_t new_cs = cpu_read_word(cpu, pc + 3);

	/* Push CS and IP */
	push_word(cpu, cpu->cs);
	push_word(cpu, cpu->ip + 5);

	/* Jump to target */
	cpu->cs = new_cs;
	cpu->ip = new_ip;
}

/* ============================================================================
 * WAIT/FWAIT - Wait for FPU (0x9B)
 * ============================================================================ */
static inline void wait_op(X86Cpu *cpu)
{
	/* No-op for CPU without FPU - just advance IP */
	cpu->ip++;
}

/* RET near (0xC3) - Return from procedure */
static inline void ret_near(X86Cpu *cpu)
{
	cpu->ip = pop_word(cpu);
}

/* RET near with pop (0xC2) - Return and pop bytes */
static inline void ret_near_pop(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint16_t pop_bytes = cpu_read_word(cpu, pc + 1);

	cpu->ip = pop_word(cpu);
	cpu->sp += pop_bytes;
}

/* RET far (0xCB) - Return far from procedure */
static inline void ret_far(X86Cpu *cpu)
{
	cpu->ip = pop_word(cpu);
	cpu->cs = pop_word(cpu);
}

/* RET far with pop (0xCA) - Return far and pop bytes */
static inline void ret_far_pop(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint16_t pop_bytes = cpu_read_word(cpu, pc + 1);

	cpu->ip = pop_word(cpu);
	cpu->cs = pop_word(cpu);
	cpu->sp += pop_bytes;
}

/* INT (0xCD) - Software interrupt */
static inline void int_op(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t vector = cpu_read_byte(cpu, pc + 1);

	/* Push flags, CS, and IP */
	push_word(cpu, cpu->flags);
	push_word(cpu, cpu->cs);
	push_word(cpu, cpu->ip + 2);

	/* Clear interrupt and trap flags */
	clear_flag(cpu, FLAGS_INT);
	clear_flag(cpu, FLAGS_TF);

	/* Load interrupt vector (vector * 4 gives address in IVT) */
	uint32_t ivt_addr = vector * 4;
	cpu->ip = cpu_read_word(cpu, ivt_addr);
	cpu->cs = cpu_read_word(cpu, ivt_addr + 2);
}

/* INT 3 (0xCC) - Breakpoint interrupt */
static inline void int3(X86Cpu *cpu)
{
	/* Push flags, CS, and IP */
	push_word(cpu, cpu->flags);
	push_word(cpu, cpu->cs);
	push_word(cpu, cpu->ip + 1);

	/* Clear interrupt and trap flags */
	clear_flag(cpu, FLAGS_INT);
	clear_flag(cpu, FLAGS_TF);

	/* Load interrupt vector 3 */
	uint32_t ivt_addr = 3 * 4;
	cpu->ip = cpu_read_word(cpu, ivt_addr);
	cpu->cs = cpu_read_word(cpu, ivt_addr + 2);
}

/* INTO (0xCE) - Interrupt on overflow */
static inline void into(X86Cpu *cpu)
{
	if (FLAG_TST(FLAGS_OV)) {
		/* Push flags, CS, and IP */
		push_word(cpu, cpu->flags);
		push_word(cpu, cpu->cs);
		push_word(cpu, cpu->ip + 1);

		/* Clear interrupt and trap flags */
		clear_flag(cpu, FLAGS_INT);
		clear_flag(cpu, FLAGS_TF);

		/* Load interrupt vector 4 */
		uint32_t ivt_addr = 4 * 4;
		cpu->ip = cpu_read_word(cpu, ivt_addr);
		cpu->cs = cpu_read_word(cpu, ivt_addr + 2);
	} else {
		cpu->ip++;
	}
}

/* IRET (0xCF) - Return from interrupt */
static inline void iret(X86Cpu *cpu)
{
	cpu->ip = pop_word(cpu);
	cpu->cs = pop_word(cpu);
	cpu->flags = pop_word(cpu);
}

/* JMP short (0xEB) - Jump short (8-bit displacement) */
static inline void jmp_short(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	int8_t displacement = (int8_t)cpu_read_byte(cpu, pc + 1);
	cpu->ip += displacement + 2;
}

/* JMP near (0xE9) - Jump near (16-bit displacement) */
static inline void jmp_near(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	int16_t displacement = (int16_t)cpu_read_word(cpu, pc + 1);
	cpu->ip += displacement + 3;
}

/* LOOP (0xE2) - Loop while CX != 0 */
static inline void loop_op(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	int8_t displacement = (int8_t)cpu_read_byte(cpu, pc + 1);

	cpu->cx.w--;
	if (cpu->cx.w != 0) {
		cpu->ip += displacement + 2;
	} else {
		cpu->ip += 2;
	}
}

/* LOOPZ/LOOPE (0xE1) - Loop while zero/equal */
static inline void loopz(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	int8_t displacement = (int8_t)cpu_read_byte(cpu, pc + 1);

	cpu->cx.w--;
	if (cpu->cx.w != 0 && FLAG_TST(FLAGS_ZF)) {
		cpu->ip += displacement + 2;
	} else {
		cpu->ip += 2;
	}
}

/* LOOPNZ/LOOPNE (0xE0) - Loop while not zero/not equal */
static inline void loopnz(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	int8_t displacement = (int8_t)cpu_read_byte(cpu, pc + 1);

	cpu->cx.w--;
	if (cpu->cx.w != 0 && !FLAG_TST(FLAGS_ZF)) {
		cpu->ip += displacement + 2;
	} else {
		cpu->ip += 2;
	}
}

/* JCXZ (0xE3) - Jump if CX is zero */
static inline void jcxz(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	int8_t displacement = (int8_t)cpu_read_byte(cpu, pc + 1);

	if (cpu->cx.w == 0) {
		cpu->ip += displacement + 2;
	} else {
		cpu->ip += 2;
	}
}

/* Conditional jump instructions (0x70 - 0x7F) */
static inline void jcc(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	int8_t displacement = (int8_t)cpu_read_byte(cpu, pc + 1);
	bool condition = false;

	/* Decode condition from opcode low nibble */
	switch (opcode & 0xF) {
		case 0x0:  /* JO - Jump if overflow */
			condition = FLAG_TST(FLAGS_OV);
			break;
		case 0x1:  /* JNO - Jump if not overflow */
			condition = !FLAG_TST(FLAGS_OV);
			break;
		case 0x2:  /* JB/JC - Jump if below/carry */
			condition = FLAG_TST(FLAGS_CF);
			break;
		case 0x3:  /* JNB/JNC - Jump if not below/not carry */
			condition = !FLAG_TST(FLAGS_CF);
			break;
		case 0x4:  /* JZ/JE - Jump if zero/equal */
			condition = FLAG_TST(FLAGS_ZF);
			break;
		case 0x5:  /* JNZ/JNE - Jump if not zero/not equal */
			condition = !FLAG_TST(FLAGS_ZF);
			break;
		case 0x6:  /* JBE/JNA - Jump if below or equal */
			condition = FLAG_TST(FLAGS_CF) || FLAG_TST(FLAGS_ZF);
			break;
		case 0x7:  /* JNBE/JA - Jump if not below or equal */
			condition = !FLAG_TST(FLAGS_CF) && !FLAG_TST(FLAGS_ZF);
			break;
		case 0x8:  /* JS - Jump if sign */
			condition = FLAG_TST(FLAGS_SF);
			break;
		case 0x9:  /* JNS - Jump if not sign */
			condition = !FLAG_TST(FLAGS_SF);
			break;
		case 0xA:  /* JP/JPE - Jump if parity/parity even */
			condition = FLAG_TST(FLAGS_PF);
			break;
		case 0xB:  /* JNP/JPO - Jump if not parity/parity odd */
			condition = !FLAG_TST(FLAGS_PF);
			break;
		case 0xC:  /* JL/JNGE - Jump if less */
			condition = FLAG_TST(FLAGS_SF) != FLAG_TST(FLAGS_OV);
			break;
		case 0xD:  /* JNL/JGE - Jump if not less */
			condition = FLAG_TST(FLAGS_SF) == FLAG_TST(FLAGS_OV);
			break;
		case 0xE:  /* JLE/JNG - Jump if less or equal */
			condition = FLAG_TST(FLAGS_ZF) ||
				(FLAG_TST(FLAGS_SF) != FLAG_TST(FLAGS_OV));
			break;
		case 0xF:  /* JNLE/JG - Jump if not less or equal */
			condition = !FLAG_TST(FLAGS_ZF) &&
				(FLAG_TST(FLAGS_SF) == FLAG_TST(FLAGS_OV));
			break;
	}

	fprintf(stderr, "Jcc 0x%02X (disp: %d) %s", opcode, displacement,
		condition ? "TAKEN" : "NOT TAKEN");

	/* Update IP: +2 for instruction, +displacement if condition met */
	cpu->ip += 2;
	if (condition)
		cpu->ip += displacement;
} 
/* Flag register instructions (0x9E, 0x9F) */

/* SAHF - Store AH into flags (0x9E) */
static inline void sahf(X86Cpu *cpu)
{
	/* Load SF, ZF, AF, PF, CF from AH (bits 7,6,4,2,0) */
	/* Bit 1 is always 1, bits 5,3 are always 0 */
	cpu->flags = (cpu->flags & 0xFF00) | (cpu->ax.h & 0xD5) | 0x02;
	cpu->ip++;
}

/* LAHF - Load flags into AH (0x9F) */
static inline void lahf(X86Cpu *cpu)
{
	/* Store SF, ZF, AF, PF, CF into AH (bits 7,6,4,2,0) plus bit 1 */
	cpu->ax.h = (cpu->flags & 0xD7);  /* 0xD7 = bits 7,6,4,2,1,0 */
	cpu->ip++;
}

/* MOV with ModR/M (0x88-0x8B) */
static inline void mov_modrm(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	bool is_byte = !(opcode & 0x01);
	bool direction = opcode & 0x02;  /* 0=reg to r/m, 1=r/m to reg */

	ModRM modrm = decode_modrm(cpu, pc + 1);

	if (is_byte) {
		uint8_t *reg_ptr = get_reg8_ptr(cpu, modrm.reg);
		if (direction) {
			/* MOV reg, r/m */
			if (modrm.is_memory)
				*reg_ptr = cpu_read_byte(cpu, modrm.ea);
			else
				*reg_ptr = *get_reg8_ptr(cpu, modrm.rm);
		} else {
			/* MOV r/m, reg */
			if (modrm.is_memory)
				cpu_write_byte(cpu, modrm.ea, *reg_ptr);
			else
				*get_reg8_ptr(cpu, modrm.rm) = *reg_ptr;
		}
	} else {
		uint16_t *reg_ptr = get_reg16_ptr(cpu, modrm.reg);
		if (direction) {
			/* MOV reg, r/m */
			if (modrm.is_memory)
				*reg_ptr = cpu_read_word(cpu, modrm.ea);
			else
				*reg_ptr = *get_reg16_ptr(cpu, modrm.rm);
		} else {
			/* MOV r/m, reg */
			if (modrm.is_memory)
				cpu_write_word(cpu, modrm.ea, *reg_ptr);
			else
				*get_reg16_ptr(cpu, modrm.rm) = *reg_ptr;
		}
	}

	cpu->ip += 1 + modrm.length;
}

/* MOV r/m, imm (0xC6-0xC7) - Group 11 */
static inline void mov_rm_imm(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	bool is_byte = (opcode == 0xC6);
	ModRM modrm = decode_modrm(cpu, pc + 1);

	/* For Group 11, reg field must be 0 (MOV) */
	if (modrm.reg != 0) {
		fprintf(stderr, "MOV r/m,imm: Invalid reg field %d\n", modrm.reg);
		cpu->ip += 1 + modrm.length;
		return;
	}

	if (is_byte) {
		uint8_t imm = cpu_read_byte(cpu, pc + 1 + modrm.length);
		if (modrm.is_memory) {
			cpu_write_byte(cpu, modrm.ea, imm);
		} else {
			*get_reg8_ptr(cpu, modrm.rm) = imm;
		}
		cpu->ip += 1 + modrm.length + 1;
	} else {
		uint16_t imm = cpu_read_word(cpu, pc + 1 + modrm.length);
		if (modrm.is_memory) {
			cpu_write_word(cpu, modrm.ea, imm);
		} else {
			*get_reg16_ptr(cpu, modrm.rm) = imm;
		}
		cpu->ip += 1 + modrm.length + 2;
	}
}

/* MOV immediate to register (0xB0 - 0xBF) */
static inline void mov(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	uint8_t imm8 = cpu_read_byte(cpu, pc + 1);
	uint8_t reg = opcode & 0xF;

	fprintf(stderr, "MOV ");

	/* 0xB0-0xB7: MOV to 8-bit register */
	/* 0xB8-0xBF: MOV to 16-bit register */
	switch (reg) {
		case 0x0:  /* MOV AL, imm8 */
			cpu->ax.l = imm8;
			fprintf(stderr, "AL, 0x%02X", imm8);
			cpu->ip += 2;
			break;
		case 0x1:  /* MOV CL, imm8 */
			cpu->cx.l = imm8;
			fprintf(stderr, "CL, 0x%02X", imm8);
			cpu->ip += 2;
			break;
		case 0x2:  /* MOV DL, imm8 */
			cpu->dx.l = imm8;
			fprintf(stderr, "DL, 0x%02X", imm8);
			cpu->ip += 2;
			break;
		case 0x3:  /* MOV BL, imm8 */
			cpu->bx.l = imm8;
			fprintf(stderr, "BL, 0x%02X", imm8);
			cpu->ip += 2;
			break;
		case 0x4:  /* MOV AH, imm8 */
			cpu->ax.h = imm8;
			fprintf(stderr, "AH, 0x%02X", imm8);
			cpu->ip += 2;
			break;
		case 0x5:  /* MOV CH, imm8 */
			cpu->cx.h = imm8;
			fprintf(stderr, "CH, 0x%02X", imm8);
			cpu->ip += 2;
			break;
		case 0x6:  /* MOV DH, imm8 */
			cpu->dx.h = imm8;
			fprintf(stderr, "DH, 0x%02X", imm8);
			cpu->ip += 2;
			break;
		case 0x7:  /* MOV BH, imm8 */
			cpu->bx.h = imm8;
			fprintf(stderr, "BH, 0x%02X", imm8);
			cpu->ip += 2;
			break;
		case 0x8:  /* MOV AX, imm16 */
			cpu->ax.w = cpu_read_word(cpu, pc + 1);
			fprintf(stderr, "AX, 0x%04X", cpu->ax.w);
			cpu->ip += 3;
			break;
		case 0x9:  /* MOV CX, imm16 */
			cpu->cx.w = cpu_read_word(cpu, pc + 1);
			fprintf(stderr, "CX, 0x%04X", cpu->cx.w);
			cpu->ip += 3;
			break;
		case 0xA:  /* MOV DX, imm16 */
			cpu->dx.w = cpu_read_word(cpu, pc + 1);
			fprintf(stderr, "DX, 0x%04X", cpu->dx.w);
			cpu->ip += 3;
			break;
		case 0xB:  /* MOV BX, imm16 */
			cpu->bx.w = cpu_read_word(cpu, pc + 1);
			fprintf(stderr, "BX, 0x%04X", cpu->bx.w);
			cpu->ip += 3;
			break;
		case 0xC:  /* MOV SP, imm16 */
			cpu->sp = cpu_read_word(cpu, pc + 1);
			fprintf(stderr, "SP, 0x%04X", cpu->sp);
			cpu->ip += 3;
			break;
		case 0xD:  /* MOV BP, imm16 */
			cpu->bp = cpu_read_word(cpu, pc + 1);
			fprintf(stderr, "BP, 0x%04X", cpu->bp);
			cpu->ip += 3;
			break;
		case 0xE:  /* MOV SI, imm16 */
			cpu->si = cpu_read_word(cpu, pc + 1);
			fprintf(stderr, "SI, 0x%04X", cpu->si);
			cpu->ip += 3;
			break;
		case 0xF:  /* MOV DI, imm16 */
			cpu->di = cpu_read_word(cpu, pc + 1);
			fprintf(stderr, "DI, 0x%04X", cpu->di);
			cpu->ip += 3;
			break;
		default:
			fprintf(stderr, "ERROR: Invalid MOV opcode 0x%02X", opcode);
			cpu->running = 0;
			break;
	}
}

/* POP r/m (0x8F) - Pop word from stack to memory/register */
static inline void pop_rm(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	ModRM modrm = decode_modrm(cpu, pc + 1);

	/* Pop value from stack */
	uint16_t value = pop_word(cpu);

	/* Write to memory or register */
	if (modrm.is_memory) {
		cpu_write_word(cpu, modrm.ea, value);
	} else {
		*get_reg16_ptr(cpu, modrm.rm) = value;
	}

	cpu->ip += 1 + modrm.length;
}

/* XCHG - Exchange register/memory with register (0x86-0x87) */
static inline void xchg_modrm(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	bool is_byte = !(opcode & 0x01);

	ModRM modrm = decode_modrm(cpu, pc + 1);

	if (is_byte) {
		uint8_t *reg_ptr = get_reg8_ptr(cpu, modrm.reg);
		uint8_t temp = *reg_ptr;

		if (modrm.is_memory) {
			*reg_ptr = cpu_read_byte(cpu, modrm.ea);
			cpu_write_byte(cpu, modrm.ea, temp);
		} else {
			uint8_t *rm_ptr = get_reg8_ptr(cpu, modrm.rm);
			*reg_ptr = *rm_ptr;
			*rm_ptr = temp;
		}
	} else {
		uint16_t *reg_ptr = get_reg16_ptr(cpu, modrm.reg);
		uint16_t temp = *reg_ptr;

		if (modrm.is_memory) {
			*reg_ptr = cpu_read_word(cpu, modrm.ea);
			cpu_write_word(cpu, modrm.ea, temp);
		} else {
			uint16_t *rm_ptr = get_reg16_ptr(cpu, modrm.rm);
			*reg_ptr = *rm_ptr;
			*rm_ptr = temp;
		}
	}

	cpu->ip += 1 + modrm.length;
}

/* XCHG AX with register (0x90-0x97) */
static inline void xchg_ax(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	uint8_t reg = opcode & 0x07;

	/* 0x90 is NOP (XCHG AX, AX) */
	if (reg != 0) {
		uint16_t *reg_ptr = get_reg16_ptr(cpu, reg);
		uint16_t temp = cpu->ax.w;
		cpu->ax.w = *reg_ptr;
		*reg_ptr = temp;
	}

	cpu->ip++;
}

/* NOP (0x90) - No operation */
static inline void nop(X86Cpu *cpu)
{
	cpu->ip++;
}

/* HLT (0xF4) - Halt */
static inline void hlt(X86Cpu *cpu)
{
	cpu->running = 0;
	cpu->ip++;
}

/* STI (0xFB) - Set interrupt flag */
static inline void sti(X86Cpu *cpu)
{
	set_flag(cpu, FLAGS_INT);
	cpu->ip++;
}

/* CLD (0xFC) - Clear direction flag */
static inline void cld(X86Cpu *cpu)
{
	clear_flag(cpu, FLAGS_DF);
	cpu->ip++;
}

/* STD (0xFD) - Set direction flag */
static inline void std(X86Cpu *cpu)
{
	set_flag(cpu, FLAGS_DF);
	cpu->ip++;
}

/* ============================================================================
 * ADC - Add with Carry (0x10-0x15)
 * ============================================================================ */
static inline void adc_op(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	bool is_byte = (opcode & 1) == 0;
	uint16_t carry = (cpu->flags & FLAGS_CF) ? 1 : 0;

	switch (opcode) {
		case 0x10:  /* ADC r/m8, r8 */
		case 0x11:  /* ADC r/m16, r16 */
		case 0x12:  /* ADC r8, r/m8 */
		case 0x13:  /* ADC r16, r/m16 */
		{
			ModRM modrm = decode_modrm(cpu, pc + 1);
			uint16_t src, dst;
			uint32_t result;

			if (is_byte) {
				if (opcode == 0x10 || opcode == 0x11) {
					/* r/m is destination */
					dst = modrm.is_memory ? cpu_read_byte(cpu, modrm.ea) :
						*get_reg8_ptr(cpu, modrm.rm);
					src = *get_reg8_ptr(cpu, modrm.reg);
				} else {
					/* r is destination */
					src = modrm.is_memory ? cpu_read_byte(cpu, modrm.ea) :
						*get_reg8_ptr(cpu, modrm.rm);
					dst = *get_reg8_ptr(cpu, modrm.reg);
				}
			} else {
				if (opcode == 0x11) {
					dst = modrm.is_memory ? cpu_read_word(cpu, modrm.ea) :
						*get_reg16_ptr(cpu, modrm.rm);
					src = *get_reg16_ptr(cpu, modrm.reg);
				} else {
					src = modrm.is_memory ? cpu_read_word(cpu, modrm.ea) :
						*get_reg16_ptr(cpu, modrm.rm);
					dst = *get_reg16_ptr(cpu, modrm.reg);
				}
			}

			result = (uint32_t)dst + (uint32_t)src + (uint32_t)carry;

			/* Update flags */
			chk_carry_add(cpu, result, is_byte);
			chk_overflow_add(cpu, src + carry, dst, result, is_byte);
			chk_aux_carry_add(cpu, (uint8_t)src + carry, (uint8_t)dst);
			update_flags_szp(cpu, result, is_byte);

			/* Write result */
			if (is_byte) {
				if (opcode == 0x10) {
					if (modrm.is_memory)
						cpu_write_byte(cpu, modrm.ea, result & 0xFF);
					else
						*get_reg8_ptr(cpu, modrm.rm) = result & 0xFF;
				} else {
					*get_reg8_ptr(cpu, modrm.reg) = result & 0xFF;
				}
			} else {
				if (opcode == 0x11) {
					if (modrm.is_memory)
						cpu_write_word(cpu, modrm.ea, result & 0xFFFF);
					else
						*get_reg16_ptr(cpu, modrm.rm) = result & 0xFFFF;
				} else {
					*get_reg16_ptr(cpu, modrm.reg) = result & 0xFFFF;
				}
			}

			cpu->ip += 1 + modrm.length;
			break;
		}

		case 0x14:  /* ADC AL, imm8 */
		{
			uint8_t imm = cpu_read_byte(cpu, pc + 1);
			uint8_t result = cpu->ax.l + imm + carry;

			chk_carry_add(cpu, (uint16_t)cpu->ax.l + imm + carry, true);
			chk_overflow_add(cpu, imm + carry, cpu->ax.l, result, true);
			chk_aux_carry_add(cpu, imm + carry, cpu->ax.l);
			update_flags_szp(cpu, result, true);

			cpu->ax.l = result;
			cpu->ip += 2;
			break;
		}

		case 0x15:  /* ADC AX, imm16 */
		{
			uint16_t imm = cpu_read_word(cpu, pc + 1);
			uint32_t result = (uint32_t)cpu->ax.w + imm + carry;

			chk_carry_add(cpu, result, false);
			chk_overflow_add(cpu, imm + carry, cpu->ax.w, result, false);
			chk_aux_carry_add(cpu, (uint8_t)(imm + carry), cpu->ax.l);
			update_flags_szp(cpu, result, false);

			cpu->ax.w = result & 0xFFFF;
			cpu->ip += 3;
			break;
		}
	}
}

/* ============================================================================
 * SBB - Subtract with Borrow (0x18-0x1D)
 * ============================================================================ */
static inline void sbb_op(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	bool is_byte = (opcode & 1) == 0;
	uint16_t carry = (cpu->flags & FLAGS_CF) ? 1 : 0;

	switch (opcode) {
		case 0x18:  /* SBB r/m8, r8 */
		case 0x19:  /* SBB r/m16, r16 */
		case 0x1A:  /* SBB r8, r/m8 */
		case 0x1B:  /* SBB r16, r/m16 */
		{
			ModRM modrm = decode_modrm(cpu, pc + 1);
			uint16_t src, dst;
			uint32_t result;

			if (is_byte) {
				if (opcode == 0x18) {
					dst = modrm.is_memory ? cpu_read_byte(cpu, modrm.ea) :
						*get_reg8_ptr(cpu, modrm.rm);
					src = *get_reg8_ptr(cpu, modrm.reg);
				} else {
					src = modrm.is_memory ? cpu_read_byte(cpu, modrm.ea) :
						*get_reg8_ptr(cpu, modrm.rm);
					dst = *get_reg8_ptr(cpu, modrm.reg);
				}
			} else {
				if (opcode == 0x19) {
					dst = modrm.is_memory ? cpu_read_word(cpu, modrm.ea) :
						*get_reg16_ptr(cpu, modrm.rm);
					src = *get_reg16_ptr(cpu, modrm.reg);
				} else {
					src = modrm.is_memory ? cpu_read_word(cpu, modrm.ea) :
						*get_reg16_ptr(cpu, modrm.rm);
					dst = *get_reg16_ptr(cpu, modrm.reg);
				}
			}

			/* Perform subtraction with borrow in 32-bit to capture underflow */
			result = (uint32_t)dst - (uint32_t)src - (uint32_t)carry;

			/* Update flags */
			if (is_byte) {
				cpu->flags = (cpu->flags & ~FLAGS_CF) |
					((dst < (src + carry)) ? FLAGS_CF : 0);
			} else {
				cpu->flags = (cpu->flags & ~FLAGS_CF) |
					((dst < (src + carry)) ? FLAGS_CF : 0);
			}
			chk_overflow_sub(cpu, src + carry, dst, result, is_byte);
			chk_aux_carry_sub(cpu, (uint8_t)(src + carry), (uint8_t)dst);
			update_flags_szp(cpu, result, is_byte);

			/* Write result */
			if (is_byte) {
				if (opcode == 0x18) {
					if (modrm.is_memory)
						cpu_write_byte(cpu, modrm.ea, result & 0xFF);
					else
						*get_reg8_ptr(cpu, modrm.rm) = result & 0xFF;
				} else {
					*get_reg8_ptr(cpu, modrm.reg) = result & 0xFF;
				}
			} else {
				if (opcode == 0x19) {
					if (modrm.is_memory)
						cpu_write_word(cpu, modrm.ea, result & 0xFFFF);
					else
						*get_reg16_ptr(cpu, modrm.rm) = result & 0xFFFF;
				} else {
					*get_reg16_ptr(cpu, modrm.reg) = result & 0xFFFF;
				}
			}

			cpu->ip += 1 + modrm.length;
			break;
		}

		case 0x1C:  /* SBB AL, imm8 */
		{
			uint8_t imm = cpu_read_byte(cpu, pc + 1);
			uint8_t result = cpu->ax.l - imm - carry;

			cpu->flags = (cpu->flags & ~FLAGS_CF) |
				((cpu->ax.l < (imm + carry)) ? FLAGS_CF : 0);
			chk_overflow_sub(cpu, imm + carry, cpu->ax.l, result, true);
			chk_aux_carry_sub(cpu, imm + carry, cpu->ax.l);
			update_flags_szp(cpu, result, true);

			cpu->ax.l = result;
			cpu->ip += 2;
			break;
		}

		case 0x1D:  /* SBB AX, imm16 */
		{
			uint16_t imm = cpu_read_word(cpu, pc + 1);
			uint16_t result = cpu->ax.w - imm - carry;

			cpu->flags = (cpu->flags & ~FLAGS_CF) |
				((cpu->ax.w < (imm + carry)) ? FLAGS_CF : 0);
			chk_overflow_sub(cpu, imm + carry, cpu->ax.w, result, false);
			chk_aux_carry_sub(cpu, (uint8_t)(imm + carry), cpu->ax.l);
			update_flags_szp(cpu, result, false);

			cpu->ax.w = result;
			cpu->ip += 3;
			break;
		}
	}
}

/* ============================================================================
 * DAA - Decimal Adjust after Addition (0x27)
 * ============================================================================ */
static inline void daa(X86Cpu *cpu)
{
	uint8_t old_al = cpu->ax.l;
	bool old_cf = (cpu->flags & FLAGS_CF) != 0;

	if (((old_al & 0x0F) > 9) || (cpu->flags & FLAGS_AF)) {
		cpu->ax.l += 6;
		set_flag(cpu, FLAGS_AF);
	} else {
		clear_flag(cpu, FLAGS_AF);
	}

	if ((old_al > 0x99) || old_cf) {
		cpu->ax.l += 0x60;
		set_flag(cpu, FLAGS_CF);
	} else {
		clear_flag(cpu, FLAGS_CF);
	}

	update_flags_szp(cpu, cpu->ax.l, true);
	cpu->ip++;
}

/* ============================================================================
 * DAS - Decimal Adjust after Subtraction (0x2F)
 * ============================================================================ */
static inline void das(X86Cpu *cpu)
{
	uint8_t old_al = cpu->ax.l;
	bool old_cf = (cpu->flags & FLAGS_CF) != 0;

	if (((old_al & 0x0F) > 9) || (cpu->flags & FLAGS_AF)) {
		cpu->ax.l -= 6;
		set_flag(cpu, FLAGS_AF);
	} else {
		clear_flag(cpu, FLAGS_AF);
	}

	if ((old_al > 0x99) || old_cf) {
		cpu->ax.l -= 0x60;
		set_flag(cpu, FLAGS_CF);
	} else {
		clear_flag(cpu, FLAGS_CF);
	}

	update_flags_szp(cpu, cpu->ax.l, true);
	cpu->ip++;
}

/* ============================================================================
 * AAA - ASCII Adjust after Addition (0x37)
 * ============================================================================ */
static inline void aaa(X86Cpu *cpu)
{
	if (((cpu->ax.l & 0x0F) > 9) || (cpu->flags & FLAGS_AF)) {
		cpu->ax.l += 6;
		cpu->ax.h += 1;
		set_flag(cpu, FLAGS_AF);
		set_flag(cpu, FLAGS_CF);
	} else {
		clear_flag(cpu, FLAGS_AF);
		clear_flag(cpu, FLAGS_CF);
	}

	cpu->ax.l &= 0x0F;
	cpu->ip++;
}

/* ============================================================================
 * AAS - ASCII Adjust after Subtraction (0x3F)
 * ============================================================================ */
static inline void aas(X86Cpu *cpu)
{
	if (((cpu->ax.l & 0x0F) > 9) || (cpu->flags & FLAGS_AF)) {
		cpu->ax.l -= 6;
		cpu->ax.h -= 1;
		set_flag(cpu, FLAGS_AF);
		set_flag(cpu, FLAGS_CF);
	} else {
		clear_flag(cpu, FLAGS_AF);
		clear_flag(cpu, FLAGS_CF);
	}

	cpu->ax.l &= 0x0F;
	cpu->ip++;
}

/* ============================================================================
 * AAM - ASCII Adjust after Multiplication (0xD4)
 * ============================================================================ */
static inline void aam(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t base = cpu_read_byte(cpu, pc + 1);

	if (base == 0) {
		/* Division by zero - should trigger interrupt 0 */
		fprintf(stderr, "AAM: Division by zero\n");
		cpu->running = 0;
		return;
	}

	cpu->ax.h = cpu->ax.l / base;
	cpu->ax.l = cpu->ax.l % base;

	update_flags_szp(cpu, cpu->ax.l, true);
	cpu->ip += 2;
}

/* ============================================================================
 * AAD - ASCII Adjust before Division (0xD5)
 * ============================================================================ */
static inline void aad(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t base = cpu_read_byte(cpu, pc + 1);

	cpu->ax.l = cpu->ax.h * base + cpu->ax.l;
	cpu->ax.h = 0;

	update_flags_szp(cpu, cpu->ax.l, true);
	cpu->ip += 2;
}

/* ============================================================================
 * SALC - Set AL from Carry (0xD6) - Undocumented instruction
 * ============================================================================ */
static inline void salc(X86Cpu *cpu)
{
	/* AL = (CF == 1) ? 0xFF : 0x00 */
	cpu->ax.l = (cpu->flags & FLAGS_CF) ? 0xFF : 0x00;
	cpu->ip++;
}

/* ============================================================================
 * XLAT/XLATB - Translate Byte (0xD7)
 * ============================================================================ */
static inline void xlat(X86Cpu *cpu)
{
	/* AL = DS:[BX + AL] - Table lookup instruction */
	uint32_t addr = cpu_calc_addr(cpu->ds, cpu->bx.w + cpu->ax.l);
	cpu->ax.l = cpu_read_byte(cpu, addr);
	cpu->ip++;
}

/* ============================================================================
 * ESC - Escape to FPU (0xD8-0xDF)
 * ============================================================================ */
static inline void esc_op(X86Cpu *cpu)
{
	/* ESC instructions pass control to 8087 FPU coprocessor
	 * Since we don't have FPU, just decode ModR/M and skip instruction
	 * ESC has format: opcode + ModR/M (+ optional displacement)
	 */
	uint32_t pc = cpu_get_pc(cpu);
	ModRM modrm = decode_modrm(cpu, pc + 1);
	cpu->ip += 1 + modrm.length;  /* Skip opcode + ModR/M + displacement */
}

/* ============================================================================
 * CBW - Convert Byte to Word (0x98)
 * ============================================================================ */
static inline void cbw(X86Cpu *cpu)
{
	if (cpu->ax.l & 0x80) {
		cpu->ax.h = 0xFF;
	} else {
		cpu->ax.h = 0;
	}
	cpu->ip++;
}

/* ============================================================================
 * CWD - Convert Word to Doubleword (0x99)
 * ============================================================================ */
static inline void cwd(X86Cpu *cpu)
{
	if (cpu->ax.w & 0x8000) {
		cpu->dx.w = 0xFFFF;
	} else {
		cpu->dx.w = 0;
	}
	cpu->ip++;
}

/* ============================================================================
 * LEA - Load Effective Address (0x8D)
 * ============================================================================ */
static inline void lea(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	ModRM modrm = decode_modrm(cpu, pc + 1);

	if (!modrm.is_memory) {
		fprintf(stderr, "LEA: Invalid use with register operand\n");
		cpu->running = 0;
		return;
	}

	/* LEA loads the offset only, not the value */
	*get_reg16_ptr(cpu, modrm.reg) = modrm.ea & 0xFFFF;
	cpu->ip += modrm.length;
}

/* ============================================================================
 * LDS - Load pointer to DS (0xC5)
 * ============================================================================ */
static inline void lds(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	ModRM modrm = decode_modrm(cpu, pc + 1);

	if (!modrm.is_memory) {
		fprintf(stderr, "LDS: Invalid use with register operand\n");
		cpu->running = 0;
		return;
	}

	uint16_t offset = cpu_read_word(cpu, modrm.ea);
	uint16_t segment = cpu_read_word(cpu, modrm.ea + 2);

	*get_reg16_ptr(cpu, modrm.reg) = offset;
	cpu->ds = segment;

	cpu->ip += modrm.length;
}

/* ============================================================================
 * LES - Load pointer to ES (0xC4)
 * ============================================================================ */
static inline void les(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	ModRM modrm = decode_modrm(cpu, pc + 1);

	if (!modrm.is_memory) {
		fprintf(stderr, "LES: Invalid use with register operand\n");
		cpu->running = 0;
		return;
	}

	uint16_t offset = cpu_read_word(cpu, modrm.ea);
	uint16_t segment = cpu_read_word(cpu, modrm.ea + 2);

	*get_reg16_ptr(cpu, modrm.reg) = offset;
	cpu->es = segment;

	cpu->ip += modrm.length;
}

/* ============================================================================
 * MOV - Move to/from segment registers (0x8C, 0x8E)
 * ============================================================================ */
static inline void mov_seg(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	ModRM modrm = decode_modrm(cpu, pc + 1);

	uint16_t *seg_reg;
	switch (modrm.reg) {
		case 0: seg_reg = &cpu->es; break;
		case 1: seg_reg = &cpu->cs; break;
		case 2: seg_reg = &cpu->ss; break;
		case 3: seg_reg = &cpu->ds; break;
		default:
			fprintf(stderr, "MOV: Invalid segment register %d\n", modrm.reg);
			cpu->running = 0;
			return;
	}

	if (opcode == 0x8C) {
		/* MOV r/m16, Sreg */
		if (modrm.is_memory) {
			cpu_write_word(cpu, modrm.ea, *seg_reg);
		} else {
			*get_reg16_ptr(cpu, modrm.rm) = *seg_reg;
		}
	} else {
		/* MOV Sreg, r/m16 */
		uint16_t value;
		if (modrm.is_memory) {
			value = cpu_read_word(cpu, modrm.ea);
		} else {
			value = *get_reg16_ptr(cpu, modrm.rm);
		}
		*seg_reg = value;
	}

	cpu->ip += modrm.length;
}

/* ============================================================================
 * MOV - Move to/from memory (direct addressing) (0xA0-0xA3)
 * ============================================================================ */
static inline void mov_mem(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	uint16_t offset = cpu_read_word(cpu, pc + 1);
	uint32_t addr = (cpu->ds << 4) + offset;

	switch (opcode) {
		case 0xA0:  /* MOV AL, [offset] */
			cpu->ax.l = cpu_read_byte(cpu, addr);
			cpu->ip += 3;
			break;

		case 0xA1:  /* MOV AX, [offset] */
			cpu->ax.w = cpu_read_word(cpu, addr);
			cpu->ip += 3;
			break;

		case 0xA2:  /* MOV [offset], AL */
			cpu_write_byte(cpu, addr, cpu->ax.l);
			cpu->ip += 3;
			break;

		case 0xA3:  /* MOV [offset], AX */
			cpu_write_word(cpu, addr, cpu->ax.w);
			cpu->ip += 3;
			break;
	}
}

/* ============================================================================
 * String Operations Helper
 * ============================================================================ */
static inline void adjust_si_di(X86Cpu *cpu, bool is_byte)
{
	int delta = is_byte ? 1 : 2;
	if (cpu->flags & FLAGS_DF) {
		cpu->si -= delta;
		cpu->di -= delta;
	} else {
		cpu->si += delta;
		cpu->di += delta;
	}
}

/* ============================================================================
 * MOVS - Move String (0xA4-0xA5)
 * ============================================================================ */
static inline void movs(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	bool is_byte = (opcode == 0xA4);

	uint32_t src_addr = (cpu->ds << 4) + cpu->si;
	uint32_t dst_addr = (cpu->es << 4) + cpu->di;

	if (is_byte) {
		uint8_t value = cpu_read_byte(cpu, src_addr);
		cpu_write_byte(cpu, dst_addr, value);
	} else {
		uint16_t value = cpu_read_word(cpu, src_addr);
		cpu_write_word(cpu, dst_addr, value);
	}

	adjust_si_di(cpu, is_byte);
	cpu->ip++;
}

/* ============================================================================
 * CMPS - Compare String (0xA6-0xA7)
 * ============================================================================ */
static inline void cmps(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	bool is_byte = (opcode == 0xA6);

	uint32_t src_addr = (cpu->ds << 4) + cpu->si;
	uint32_t dst_addr = (cpu->es << 4) + cpu->di;

	if (is_byte) {
		uint8_t src = cpu_read_byte(cpu, src_addr);
		uint8_t dst = cpu_read_byte(cpu, dst_addr);
		uint8_t result = dst - src;

		cpu->flags = (cpu->flags & ~FLAGS_CF) | ((dst < src) ? FLAGS_CF : 0);
		chk_overflow_sub(cpu, src, dst, result, true);
		chk_aux_carry_sub(cpu, src, dst);
		update_flags_szp(cpu, result, true);
	} else {
		uint16_t src = cpu_read_word(cpu, src_addr);
		uint16_t dst = cpu_read_word(cpu, dst_addr);
		uint16_t result = dst - src;

		cpu->flags = (cpu->flags & ~FLAGS_CF) | ((dst < src) ? FLAGS_CF : 0);
		chk_overflow_sub(cpu, src, dst, result, false);
		chk_aux_carry_sub(cpu, (uint8_t)src, (uint8_t)dst);
		update_flags_szp(cpu, result, false);
	}

	adjust_si_di(cpu, is_byte);
	cpu->ip++;
}

/* ============================================================================
 * SCAS - Scan String (0xAE-0xAF)
 * ============================================================================ */
static inline void scas(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	bool is_byte = (opcode == 0xAE);

	uint32_t dst_addr = (cpu->es << 4) + cpu->di;

	if (is_byte) {
		uint8_t dst = cpu_read_byte(cpu, dst_addr);
		uint8_t result = cpu->ax.l - dst;

		cpu->flags = (cpu->flags & ~FLAGS_CF) | ((cpu->ax.l < dst) ? FLAGS_CF : 0);
		chk_overflow_sub(cpu, dst, cpu->ax.l, result, true);
		chk_aux_carry_sub(cpu, dst, cpu->ax.l);
		update_flags_szp(cpu, result, true);

		cpu->di += (cpu->flags & FLAGS_DF) ? -1 : 1;
	} else {
		uint16_t dst = cpu_read_word(cpu, dst_addr);
		uint16_t result = cpu->ax.w - dst;

		cpu->flags = (cpu->flags & ~FLAGS_CF) | ((cpu->ax.w < dst) ? FLAGS_CF : 0);
		chk_overflow_sub(cpu, dst, cpu->ax.w, result, false);
		chk_aux_carry_sub(cpu, (uint8_t)dst, cpu->ax.l);
		update_flags_szp(cpu, result, false);

		cpu->di += (cpu->flags & FLAGS_DF) ? -2 : 2;
	}

	cpu->ip++;
}

/* ============================================================================
 * LODS - Load String (0xAC-0xAD)
 * ============================================================================ */
static inline void lods(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	bool is_byte = (opcode == 0xAC);

	uint32_t src_addr = (cpu->ds << 4) + cpu->si;

	if (is_byte) {
		cpu->ax.l = cpu_read_byte(cpu, src_addr);
		cpu->si += (cpu->flags & FLAGS_DF) ? -1 : 1;
	} else {
		cpu->ax.w = cpu_read_word(cpu, src_addr);
		cpu->si += (cpu->flags & FLAGS_DF) ? -2 : 2;
	}

	cpu->ip++;
}

/* ============================================================================
 * STOS - Store String (0xAA-0xAB)
 * ============================================================================ */
static inline void stos(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	bool is_byte = (opcode == 0xAA);

	uint32_t dst_addr = (cpu->es << 4) + cpu->di;

	if (is_byte) {
		cpu_write_byte(cpu, dst_addr, cpu->ax.l);
		cpu->di += (cpu->flags & FLAGS_DF) ? -1 : 1;
	} else {
		cpu_write_word(cpu, dst_addr, cpu->ax.w);
		cpu->di += (cpu->flags & FLAGS_DF) ? -2 : 2;
	}

	cpu->ip++;
}

/* ============================================================================
 * MUL/IMUL/DIV/IDIV and other Grp3 instructions (0xF6-0xF7)
 * ============================================================================ */
static inline void grp3(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	bool is_byte = (opcode == 0xF6);
	ModRM modrm = decode_modrm(cpu, pc + 1);

	uint16_t operand;
	if (is_byte) {
		operand = modrm.is_memory ? cpu_read_byte(cpu, modrm.ea) :
			*get_reg8_ptr(cpu, modrm.rm);
	} else {
		operand = modrm.is_memory ? cpu_read_word(cpu, modrm.ea) :
			*get_reg16_ptr(cpu, modrm.rm);
	}

	switch (modrm.reg) {
		case 0:  /* TEST r/m, imm */
		{
			uint16_t imm;
			if (is_byte) {
				imm = cpu_read_byte(cpu, pc + 1 + modrm.length);
			} else {
				imm = cpu_read_word(cpu, pc + 1 + modrm.length);
			}
			uint16_t result = operand & imm;
			update_flags_logic(cpu, result, is_byte);
			cpu->ip += 1 + modrm.length + (is_byte ? 1 : 2);
			break;
		}

		case 2:  /* NOT r/m */
		{
			uint16_t result = ~operand;
			if (is_byte) {
				result &= 0xFF;
				if (modrm.is_memory)
					cpu_write_byte(cpu, modrm.ea, result);
				else
					*get_reg8_ptr(cpu, modrm.rm) = result;
			} else {
				if (modrm.is_memory)
					cpu_write_word(cpu, modrm.ea, result);
				else
					*get_reg16_ptr(cpu, modrm.rm) = result;
			}
			cpu->ip += 1 + modrm.length;
			break;
		}

		case 3:  /* NEG r/m */
		{
			uint16_t result = -operand;
			if (is_byte) {
				result &= 0xFF;
				cpu->flags = (cpu->flags & ~FLAGS_CF) | ((result != 0) ? FLAGS_CF : 0);
				chk_overflow_sub(cpu, operand, 0, result, true);
				chk_aux_carry_sub(cpu, operand, 0);
				update_flags_szp(cpu, result, true);

				if (modrm.is_memory)
					cpu_write_byte(cpu, modrm.ea, result);
				else
					*get_reg8_ptr(cpu, modrm.rm) = result;
			} else {
				cpu->flags = (cpu->flags & ~FLAGS_CF) | ((result != 0) ? FLAGS_CF : 0);
				chk_overflow_sub(cpu, operand, 0, result, false);
				chk_aux_carry_sub(cpu, (uint8_t)operand, 0);
				update_flags_szp(cpu, result, false);

				if (modrm.is_memory)
					cpu_write_word(cpu, modrm.ea, result);
				else
					*get_reg16_ptr(cpu, modrm.rm) = result;
			}
			cpu->ip += 1 + modrm.length;
			break;
		}

		case 4:  /* MUL r/m */
		{
			if (is_byte) {
				uint16_t result = cpu->ax.l * operand;
				cpu->ax.w = result;
				if (cpu->ax.h == 0) {
					clear_flag(cpu, FLAGS_CF);
					clear_flag(cpu, FLAGS_OV);
				} else {
					set_flag(cpu, FLAGS_CF);
					set_flag(cpu, FLAGS_OV);
				}
			} else {
				uint32_t result = (uint32_t)cpu->ax.w * operand;
				cpu->ax.w = result & 0xFFFF;
				cpu->dx.w = (result >> 16) & 0xFFFF;
				if (cpu->dx.w == 0) {
					clear_flag(cpu, FLAGS_CF);
					clear_flag(cpu, FLAGS_OV);
				} else {
					set_flag(cpu, FLAGS_CF);
					set_flag(cpu, FLAGS_OV);
				}
			}
			cpu->ip += 1 + modrm.length;
			break;
		}

		case 5:  /* IMUL r/m */
		{
			if (is_byte) {
				int16_t result = (int8_t)cpu->ax.l * (int8_t)operand;
				cpu->ax.w = result;
				if ((int8_t)cpu->ax.h == 0 || (int8_t)cpu->ax.h == -1) {
					clear_flag(cpu, FLAGS_CF);
					clear_flag(cpu, FLAGS_OV);
				} else {
					set_flag(cpu, FLAGS_CF);
					set_flag(cpu, FLAGS_OV);
				}
			} else {
				int32_t result = (int16_t)cpu->ax.w * (int16_t)operand;
				cpu->ax.w = result & 0xFFFF;
				cpu->dx.w = (result >> 16) & 0xFFFF;
				if ((int16_t)cpu->dx.w == 0 || (int16_t)cpu->dx.w == -1) {
					clear_flag(cpu, FLAGS_CF);
					clear_flag(cpu, FLAGS_OV);
				} else {
					set_flag(cpu, FLAGS_CF);
					set_flag(cpu, FLAGS_OV);
				}
			}
			cpu->ip += 1 + modrm.length;
			break;
		}

		case 6:  /* DIV r/m */
		{
			if (operand == 0) {
				fprintf(stderr, "DIV: Division by zero\n");
				cpu->running = 0;
				return;
			}

			if (is_byte) {
				uint16_t dividend = cpu->ax.w;
				uint16_t quotient_check = dividend / operand;

				if (quotient_check > 0xFF) {
					fprintf(stderr, "DIV: Quotient overflow\n");
					cpu->running = 0;
					return;
				}

				cpu->ax.l = (uint8_t)quotient_check;
				cpu->ax.h = (uint8_t)(dividend % operand);
			} else {
				uint32_t dividend = ((uint32_t)cpu->dx.w << 16) | cpu->ax.w;
				uint32_t quotient_check = dividend / operand;

				if (quotient_check > 0xFFFF) {
					fprintf(stderr, "DIV: Quotient overflow\n");
					cpu->running = 0;
					return;
				}

				cpu->ax.w = (uint16_t)quotient_check;
				cpu->dx.w = (uint16_t)(dividend % operand);
			}
			cpu->ip += 1 + modrm.length;
			break;
		}

		case 7:  /* IDIV r/m */
		{
			if (operand == 0) {
				fprintf(stderr, "IDIV: Division by zero\n");
				cpu->running = 0;
				return;
			}

			if (is_byte) {
				int16_t dividend = (int16_t)cpu->ax.w;
				int8_t divisor = (int8_t)operand;
				int16_t quotient_check = dividend / divisor;

				if (quotient_check > 127 || quotient_check < -128) {
					fprintf(stderr, "IDIV: Quotient overflow\n");
					cpu->running = 0;
					return;
				}

				cpu->ax.l = (uint8_t)(int8_t)quotient_check;
				cpu->ax.h = (uint8_t)(int8_t)(dividend % divisor);
			} else {
				int32_t dividend = ((int32_t)cpu->dx.w << 16) | cpu->ax.w;
				int16_t divisor = (int16_t)operand;
				int32_t quotient_check = dividend / divisor;

				if (quotient_check > 32767 || quotient_check < -32768) {
					fprintf(stderr, "IDIV: Quotient overflow\n");
					cpu->running = 0;
					return;
				}

				cpu->ax.w = (uint16_t)(int16_t)quotient_check;
				cpu->dx.w = (uint16_t)(int16_t)(dividend % divisor);
			}
			cpu->ip += 1 + modrm.length;
			break;
		}

		default:
			fprintf(stderr, "Grp3: Invalid reg field %d\n", modrm.reg);
			cpu->running = 0;
			break;
	}
}

/* ============================================================================
 * INC/DEC/CALL/JMP with ModR/M (0xFE-0xFF)
 * ============================================================================ */
static inline void grp4_5(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	bool is_byte = (opcode == 0xFE);
	ModRM modrm = decode_modrm(cpu, pc + 1);

	switch (modrm.reg) {
		case 0:  /* INC r/m */
		{
			uint16_t operand, result;
			if (is_byte) {
				operand = modrm.is_memory ? cpu_read_byte(cpu, modrm.ea) :
					*get_reg8_ptr(cpu, modrm.rm);
				result = operand + 1;
				result &= 0xFF;

				chk_overflow_add(cpu, 1, operand, result, true);
				chk_aux_carry_add(cpu, 1, operand);
				update_flags_szp(cpu, result, true);

				if (modrm.is_memory)
					cpu_write_byte(cpu, modrm.ea, result);
				else
					*get_reg8_ptr(cpu, modrm.rm) = result;
			} else {
				operand = modrm.is_memory ? cpu_read_word(cpu, modrm.ea) :
					*get_reg16_ptr(cpu, modrm.rm);
				result = operand + 1;

				chk_overflow_add(cpu, 1, operand, result, false);
				chk_aux_carry_add(cpu, 1, (uint8_t)operand);
				update_flags_szp(cpu, result, false);

				if (modrm.is_memory)
					cpu_write_word(cpu, modrm.ea, result);
				else
					*get_reg16_ptr(cpu, modrm.rm) = result;
			}
			cpu->ip += 1 + modrm.length;  /* +1 for opcode byte */
			break;
		}

		case 1:  /* DEC r/m */
		{
			uint16_t operand, result;
			if (is_byte) {
				operand = modrm.is_memory ? cpu_read_byte(cpu, modrm.ea) :
					*get_reg8_ptr(cpu, modrm.rm);
				result = operand - 1;
				result &= 0xFF;

				chk_overflow_sub(cpu, 1, operand, result, true);
				chk_aux_carry_sub(cpu, 1, operand);
				update_flags_szp(cpu, result, true);

				if (modrm.is_memory)
					cpu_write_byte(cpu, modrm.ea, result);
				else
					*get_reg8_ptr(cpu, modrm.rm) = result;
			} else {
				operand = modrm.is_memory ? cpu_read_word(cpu, modrm.ea) :
					*get_reg16_ptr(cpu, modrm.rm);
				result = operand - 1;

				chk_overflow_sub(cpu, 1, operand, result, false);
				chk_aux_carry_sub(cpu, 1, (uint8_t)operand);
				update_flags_szp(cpu, result, false);

				if (modrm.is_memory)
					cpu_write_word(cpu, modrm.ea, result);
				else
					*get_reg16_ptr(cpu, modrm.rm) = result;
			}
			cpu->ip += 1 + modrm.length;  /* +1 for opcode byte */
			break;
		}

		case 2:  /* CALL r/m (near) */
		{
			if (is_byte) {
				fprintf(stderr, "CALL: Invalid byte operand\n");
				cpu->running = 0;
				return;
			}

			uint16_t target;
			if (modrm.is_memory) {
				target = cpu_read_word(cpu, modrm.ea);
			} else {
				target = *get_reg16_ptr(cpu, modrm.rm);
			}

			/* Push return address */
			push_word(cpu, cpu->ip + 1 + modrm.length);  /* +1 for opcode byte */
			cpu->ip = target;
			break;
		}

		case 3:  /* CALL m16:16 (far) */
		{
			if (is_byte || !modrm.is_memory) {
				fprintf(stderr, "CALL far: Invalid operand\n");
				cpu->running = 0;
				return;
			}

			uint16_t offset = cpu_read_word(cpu, modrm.ea);
			uint16_t segment = cpu_read_word(cpu, modrm.ea + 2);

			/* Push CS and IP */
			push_word(cpu, cpu->cs);
			push_word(cpu, cpu->ip + 1 + modrm.length);  /* +1 for opcode byte */

			cpu->ip = offset;
			cpu->cs = segment;
			break;
		}

		case 4:  /* JMP r/m (near) */
		{
			if (is_byte) {
				fprintf(stderr, "JMP: Invalid byte operand\n");
				cpu->running = 0;
				return;
			}

			if (modrm.is_memory) {
				cpu->ip = cpu_read_word(cpu, modrm.ea);
			} else {
				cpu->ip = *get_reg16_ptr(cpu, modrm.rm);
			}
			break;
		}

		case 5:  /* JMP m16:16 (far) */
		{
			if (is_byte || !modrm.is_memory) {
				fprintf(stderr, "JMP far: Invalid operand\n");
				cpu->running = 0;
				return;
			}

			uint16_t offset = cpu_read_word(cpu, modrm.ea);
			uint16_t segment = cpu_read_word(cpu, modrm.ea + 2);

			cpu->ip = offset;
			cpu->cs = segment;
			break;
		}

		case 6:  /* PUSH r/m */
		{
			if (is_byte) {
				fprintf(stderr, "PUSH: Invalid byte operand\n");
				cpu->running = 0;
				return;
			}

			uint16_t value;
			if (modrm.is_memory) {
				value = cpu_read_word(cpu, modrm.ea);
			} else {
				value = *get_reg16_ptr(cpu, modrm.rm);
			}

			push_word(cpu, value);
			cpu->ip += 1 + modrm.length;  /* +1 for opcode byte */
			break;
		}

		default:
			fprintf(stderr, "Grp4/5: Invalid reg field %d\n", modrm.reg);
			cpu->running = 0;
			break;
	}
}

/* ============================================================================
 * Group 1 Immediate ALU operations (0x80-0x83)
 * ADD/OR/ADC/SBB/AND/SUB/XOR/CMP with immediate operand
 * ============================================================================ */
static inline void grp1_imm(X86Cpu *cpu)
{
	uint32_t pc = cpu_get_pc(cpu);
	uint8_t opcode = cpu_read_byte(cpu, pc);
	bool is_byte = (opcode == 0x80 || opcode == 0x82);  /* 0x80/0x82 = byte, 0x81 = word, 0x83 = sign-extended byte */
	bool is_sign_extend = (opcode == 0x83);  /* 0x83 = sign-extend byte to word */

	ModRM modrm = decode_modrm(cpu, pc + 1);
	uint16_t operand, imm, result;
	uint8_t imm_size;

	/* Read the r/m operand */
	if (is_byte && !is_sign_extend) {
		operand = modrm.is_memory ? cpu_read_byte(cpu, modrm.ea) :
		          *get_reg8_ptr(cpu, modrm.rm);
		imm = cpu_read_byte(cpu, pc + 1 + modrm.length);
		imm_size = 1;
	} else {
		operand = modrm.is_memory ? cpu_read_word(cpu, modrm.ea) :
		          *get_reg16_ptr(cpu, modrm.rm);
		if (is_sign_extend) {
			/* Sign-extend byte immediate to word */
			imm = (uint16_t)(int16_t)(int8_t)cpu_read_byte(cpu, pc + 1 + modrm.length);
			imm_size = 1;
		} else {
			imm = cpu_read_word(cpu, pc + 1 + modrm.length);
			imm_size = 2;
		}
	}

	/* Perform the operation based on the reg field */
	switch (modrm.reg) {
		case 0:  /* ADD */
			result = operand + imm;
			if (is_byte && !is_sign_extend) {
				result &= 0xFF;
				chk_carry_add(cpu, result, true);
				chk_overflow_add(cpu, imm, operand, result, true);
				chk_aux_carry_add(cpu, imm & 0xFF, operand & 0xFF);
				update_flags_szp(cpu, result, true);
				if (modrm.is_memory)
					cpu_write_byte(cpu, modrm.ea, result);
				else
					*get_reg8_ptr(cpu, modrm.rm) = result;
			} else {
				chk_carry_add(cpu, result, false);
				chk_overflow_add(cpu, imm, operand, result, false);
				chk_aux_carry_add(cpu, imm & 0xFF, operand & 0xFF);
				update_flags_szp(cpu, result, false);
				if (modrm.is_memory)
					cpu_write_word(cpu, modrm.ea, result);
				else
					*get_reg16_ptr(cpu, modrm.rm) = result;
			}
			break;

		case 1:  /* OR */
			result = operand | imm;
			if (is_byte && !is_sign_extend) {
				result &= 0xFF;
				clear_flag(cpu, FLAGS_CF);
				clear_flag(cpu, FLAGS_OV);
				update_flags_szp(cpu, result, true);
				if (modrm.is_memory)
					cpu_write_byte(cpu, modrm.ea, result);
				else
					*get_reg8_ptr(cpu, modrm.rm) = result;
			} else {
				clear_flag(cpu, FLAGS_CF);
				clear_flag(cpu, FLAGS_OV);
				update_flags_szp(cpu, result, false);
				if (modrm.is_memory)
					cpu_write_word(cpu, modrm.ea, result);
				else
					*get_reg16_ptr(cpu, modrm.rm) = result;
			}
			break;

		case 2:  /* ADC */
		{
			uint8_t carry = (cpu->flags & FLAGS_CF) ? 1 : 0;
			result = operand + imm + carry;
			if (is_byte && !is_sign_extend) {
				result &= 0xFF;
				chk_carry_add(cpu, operand + imm + carry, true);
				chk_overflow_add(cpu, imm, operand, result, true);
				chk_aux_carry_add(cpu, (imm & 0xFF) + carry, operand & 0xFF);
				update_flags_szp(cpu, result, true);
				if (modrm.is_memory)
					cpu_write_byte(cpu, modrm.ea, result);
				else
					*get_reg8_ptr(cpu, modrm.rm) = result;
			} else {
				chk_carry_add(cpu, operand + imm + carry, false);
				chk_overflow_add(cpu, imm, operand, result, false);
				chk_aux_carry_add(cpu, (imm & 0xFF) + carry, operand & 0xFF);
				update_flags_szp(cpu, result, false);
				if (modrm.is_memory)
					cpu_write_word(cpu, modrm.ea, result);
				else
					*get_reg16_ptr(cpu, modrm.rm) = result;
			}
			break;
		}

		case 3:  /* SBB */
		{
			uint8_t carry = (cpu->flags & FLAGS_CF) ? 1 : 0;
			result = operand - imm - carry;
			if (is_byte && !is_sign_extend) {
				result &= 0xFF;
				if ((operand < (imm + carry)) || ((operand == imm) && carry))
					set_flag(cpu, FLAGS_CF);
				else
					clear_flag(cpu, FLAGS_CF);
				chk_overflow_sub(cpu, imm, operand, result, true);
				chk_aux_carry_sub(cpu, (imm & 0xFF) + carry, operand & 0xFF);
				update_flags_szp(cpu, result, true);
				if (modrm.is_memory)
					cpu_write_byte(cpu, modrm.ea, result);
				else
					*get_reg8_ptr(cpu, modrm.rm) = result;
			} else {
				if ((operand < (imm + carry)) || ((operand == imm) && carry))
					set_flag(cpu, FLAGS_CF);
				else
					clear_flag(cpu, FLAGS_CF);
				chk_overflow_sub(cpu, imm, operand, result, false);
				chk_aux_carry_sub(cpu, (imm & 0xFF) + carry, operand & 0xFF);
				update_flags_szp(cpu, result, false);
				if (modrm.is_memory)
					cpu_write_word(cpu, modrm.ea, result);
				else
					*get_reg16_ptr(cpu, modrm.rm) = result;
			}
			break;
		}

		case 4:  /* AND */
			result = operand & imm;
			if (is_byte && !is_sign_extend) {
				result &= 0xFF;
				clear_flag(cpu, FLAGS_CF);
				clear_flag(cpu, FLAGS_OV);
				update_flags_szp(cpu, result, true);
				if (modrm.is_memory)
					cpu_write_byte(cpu, modrm.ea, result);
				else
					*get_reg8_ptr(cpu, modrm.rm) = result;
			} else {
				clear_flag(cpu, FLAGS_CF);
				clear_flag(cpu, FLAGS_OV);
				update_flags_szp(cpu, result, false);
				if (modrm.is_memory)
					cpu_write_word(cpu, modrm.ea, result);
				else
					*get_reg16_ptr(cpu, modrm.rm) = result;
			}
			break;

		case 5:  /* SUB */
			result = operand - imm;
			if (is_byte && !is_sign_extend) {
				result &= 0xFF;
				if (operand < imm)
					set_flag(cpu, FLAGS_CF);
				else
					clear_flag(cpu, FLAGS_CF);
				chk_overflow_sub(cpu, imm, operand, result, true);
				chk_aux_carry_sub(cpu, imm & 0xFF, operand & 0xFF);
				update_flags_szp(cpu, result, true);
				if (modrm.is_memory)
					cpu_write_byte(cpu, modrm.ea, result);
				else
					*get_reg8_ptr(cpu, modrm.rm) = result;
			} else {
				if (operand < imm)
					set_flag(cpu, FLAGS_CF);
				else
					clear_flag(cpu, FLAGS_CF);
				chk_overflow_sub(cpu, imm, operand, result, false);
				chk_aux_carry_sub(cpu, imm & 0xFF, operand & 0xFF);
				update_flags_szp(cpu, result, false);
				if (modrm.is_memory)
					cpu_write_word(cpu, modrm.ea, result);
				else
					*get_reg16_ptr(cpu, modrm.rm) = result;
			}
			break;

		case 6:  /* XOR */
			result = operand ^ imm;
			if (is_byte && !is_sign_extend) {
				result &= 0xFF;
				clear_flag(cpu, FLAGS_CF);
				clear_flag(cpu, FLAGS_OV);
				update_flags_szp(cpu, result, true);
				if (modrm.is_memory)
					cpu_write_byte(cpu, modrm.ea, result);
				else
					*get_reg8_ptr(cpu, modrm.rm) = result;
			} else {
				clear_flag(cpu, FLAGS_CF);
				clear_flag(cpu, FLAGS_OV);
				update_flags_szp(cpu, result, false);
				if (modrm.is_memory)
					cpu_write_word(cpu, modrm.ea, result);
				else
					*get_reg16_ptr(cpu, modrm.rm) = result;
			}
			break;

		case 7:  /* CMP */
			result = operand - imm;
			if (is_byte && !is_sign_extend) {
				result &= 0xFF;
				if (operand < imm)
					set_flag(cpu, FLAGS_CF);
				else
					clear_flag(cpu, FLAGS_CF);
				chk_overflow_sub(cpu, imm, operand, result, true);
				chk_aux_carry_sub(cpu, imm & 0xFF, operand & 0xFF);
				update_flags_szp(cpu, result, true);
			} else {
				if (operand < imm)
					set_flag(cpu, FLAGS_CF);
				else
					clear_flag(cpu, FLAGS_CF);
				chk_overflow_sub(cpu, imm, operand, result, false);
				chk_aux_carry_sub(cpu, imm & 0xFF, operand & 0xFF);
				update_flags_szp(cpu, result, false);
			}
			/* CMP doesn't write back the result */
			break;

		default:
			fprintf(stderr, "Grp1: Invalid reg field %d\n", modrm.reg);
			cpu->running = 0;
			return;
	}

	cpu->ip += 1 + modrm.length + imm_size;
}

/* I/O Port Operations - Return 0xFF/0xFFFF for unconnected ports */

/* IN AL, imm8 (0xE4) - Input byte from immediate port */
static inline void in_al_imm(X86Cpu *cpu)
{
	/* uint8_t port = cpu_read_byte(cpu, pc + 1); */  /* Port address (unused) */
	cpu->ax.l = 0xFF;  /* Return 0xFF for unconnected port */
	cpu->ip += 2;
}

/* IN AX, imm8 (0xE5) - Input word from immediate port */
static inline void in_ax_imm(X86Cpu *cpu)
{
	/* uint8_t port = cpu_read_byte(cpu, pc + 1); */  /* Port address (unused) */
	cpu->ax.w = 0xFFFF;  /* Return 0xFFFF for unconnected port */
	cpu->ip += 2;
}

/* OUT imm8, AL (0xE6) - Output byte to immediate port */
static inline void out_imm_al(X86Cpu *cpu)
{
	/* uint8_t port = cpu_read_byte(cpu, pc + 1); */  /* Port address (unused) */
	/* uint8_t value = cpu->ax.l; */  /* Value to output (unused) */
	/* No-op: just advance IP */
	cpu->ip += 2;
}

/* OUT imm8, AX (0xE7) - Output word to immediate port */
static inline void out_imm_ax(X86Cpu *cpu)
{
	/* uint8_t port = cpu_read_byte(cpu, pc + 1); */  /* Port address (unused) */
	/* uint16_t value = cpu->ax.w; */  /* Value to output (unused) */
	/* No-op: just advance IP */
	cpu->ip += 2;
}

/* IN AL, DX (0xEC) - Input byte from DX port */
static inline void in_al_dx(X86Cpu *cpu)
{
	/* uint16_t port = cpu->dx.w; */  /* Port address in DX (unused) */
	cpu->ax.l = 0xFF;  /* Return 0xFF for unconnected port */
	cpu->ip += 1;
}

/* IN AX, DX (0xED) - Input word from DX port */
static inline void in_ax_dx(X86Cpu *cpu)
{
	/* uint16_t port = cpu->dx.w; */  /* Port address in DX (unused) */
	cpu->ax.w = 0xFFFF;  /* Return 0xFFFF for unconnected port */
	cpu->ip += 1;
}

/* OUT DX, AL (0xEE) - Output byte to DX port */
static inline void out_dx_al(X86Cpu *cpu)
{
	/* uint16_t port = cpu->dx.w; */  /* Port address in DX (unused) */
	/* uint8_t value = cpu->ax.l; */  /* Value to output (unused) */
	/* No-op: just advance IP */
	cpu->ip += 1;
}

/* OUT DX, AX (0xEF) - Output word to DX port */
static inline void out_dx_ax(X86Cpu *cpu)
{
	/* uint16_t port = cpu->dx.w; */  /* Port address in DX (unused) */
	/* uint16_t value = cpu->ax.w; */  /* Value to output (unused) */
	/* No-op: just advance IP */
	cpu->ip += 1;
}
