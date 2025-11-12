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

	/* Mask to 16 bits and calculate physical address */
	ea &= 0xFFFF;
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
		uint16_t src, dst, result;

		if (is_byte) {
			if (direction) {
				/* reg = reg + r/m */
				uint8_t *reg_ptr = get_reg8_ptr(cpu, modrm.reg);
				dst = *reg_ptr;
				src = modrm.is_memory ? cpu_read_byte(cpu, modrm.ea) :
				      *get_reg8_ptr(cpu, modrm.rm);
				result = dst + src;
				*reg_ptr = result & 0xFF;
			} else {
				/* r/m = r/m + reg */
				uint8_t *reg_ptr = get_reg8_ptr(cpu, modrm.reg);
				src = *reg_ptr;
				if (modrm.is_memory) {
					dst = cpu_read_byte(cpu, modrm.ea);
					result = dst + src;
					cpu_write_byte(cpu, modrm.ea, result & 0xFF);
				} else {
					uint8_t *rm_ptr = get_reg8_ptr(cpu, modrm.rm);
					dst = *rm_ptr;
					result = dst + src;
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
				result = dst + src;
				*reg_ptr = result;
			} else {
				/* r/m = r/m + reg */
				uint16_t *reg_ptr = get_reg16_ptr(cpu, modrm.reg);
				src = *reg_ptr;
				if (modrm.is_memory) {
					dst = cpu_read_word(cpu, modrm.ea);
					result = dst + src;
					cpu_write_word(cpu, modrm.ea, result);
				} else {
					uint16_t *rm_ptr = get_reg16_ptr(cpu, modrm.rm);
					dst = *rm_ptr;
					result = dst + src;
					*rm_ptr = result;
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

/* Update flags for logical operations (CF=0, OF=0, SF/ZF/PF set) */
static inline void update_flags_logic(X86Cpu *cpu, uint16_t result, bool is_byte)
{
	clear_flag(cpu, FLAGS_CF);
	clear_flag(cpu, FLAGS_OV);
	/* AF is undefined after logical operations, we'll leave it unchanged */
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
				if (count == 1) {
					bool msb = is_byte ? (result & 0x80) != 0 : (result & 0x8000) != 0;
					if (msb != new_cf)
						set_flag(cpu, FLAGS_OV);
					else
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
				/* OF is always cleared for SAR */
				if (count == 1)
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
	printf("SAHF");
	/* Load SF, ZF, AF, PF, CF from AH (bits 7,6,4,2,0) */
	cpu->flags = (cpu->flags & 0xFF00) | cpu->ax.h;
	cpu->ip++;
}

/* LAHF - Load flags into AH (0x9F) */
static inline void lahf(X86Cpu *cpu)
{
	printf("LAHF");
	/* Store SF, ZF, AF, PF, CF into AH */
	cpu->ax.h = (cpu->flags & 0xFF);
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
