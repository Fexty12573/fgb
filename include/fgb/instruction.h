#ifndef FGB_INSTRUCTION_H
#define FGB_INSTRUCTION_H

#include <stdint.h>

#define FGB_INSTRUCTION_COUNT 256


struct fgb_cpu;
struct fgb_instruction;

typedef void (*fgb_instruction_exec_0)(struct fgb_cpu* cpu, const struct fgb_instruction* ins);
typedef void (*fgb_instruction_exec_1)(struct fgb_cpu* cpu, const struct fgb_instruction* ins, uint8_t operand);
typedef void (*fgb_instruction_exec_2)(struct fgb_cpu* cpu, const struct fgb_instruction* ins, uint16_t operand);

typedef char* (*fgb_instruction_fmt_0)(const struct fgb_instruction* ins);
typedef char* (*fgb_instruction_fmt_1)(const struct fgb_instruction* ins, uint8_t operand);
typedef char* (*fgb_instruction_fmt_2)(const struct fgb_instruction* ins, uint16_t operand);

typedef struct fgb_instruction {
    const char* disassembly;
    uint8_t opcode;
    uint8_t operand_size; // 0, 1, or 2 bytes
    uint8_t cycles; // Number of cycles the instruction takes to execute
	uint8_t alt_cycles; // Number of cycles for alternate paths (e.g., taken branches)
    union {
        void* exec;
        fgb_instruction_exec_0 exec_0;
        fgb_instruction_exec_1 exec_1;
        fgb_instruction_exec_2 exec_2;
    };
    union {
        void* fmt;
        fgb_instruction_fmt_0 fmt_0;
        fgb_instruction_fmt_1 fmt_1;
        fgb_instruction_fmt_2 fmt_2;
    };
} fgb_instruction;

extern const fgb_instruction fgb_instruction_table[FGB_INSTRUCTION_COUNT];
extern const uint8_t fgb_cb_instruction_cycles[FGB_INSTRUCTION_COUNT];

static inline const fgb_instruction* fgb_instruction_get(uint8_t opcode) {
    return fgb_instruction_table + opcode;
}

static inline uint8_t fgb_instruction_get_cb_cycles(uint8_t opcode) {
    return fgb_cb_instruction_cycles[opcode] * 4; // Convert to clock cycles
}

// x0
void fgb_nop(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_stop(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);
void fgb_halt(struct fgb_cpu* cpu, const fgb_instruction* ins);

// LD reg16, imm
void fgb_ld_bc_imm(struct fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand);
void fgb_ld_de_imm(struct fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand);
void fgb_ld_hl_imm(struct fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand);
void fgb_ld_sp_imm(struct fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand);

// LD (reg16), A
void fgb_ld_p_bc_a(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_p_de_a(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_p_hli_a(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_p_hld_a(struct fgb_cpu* cpu, const fgb_instruction* ins);

// LD reg8/(HL), imm
void fgb_ld_b_imm(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);
void fgb_ld_d_imm(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);
void fgb_ld_h_imm(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);
void fgb_ld_c_imm(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);
void fgb_ld_e_imm(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);
void fgb_ld_l_imm(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);
void fgb_ld_a_imm(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);
void fgb_ld_p_hl_imm(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);

// LD A, (reg16)
void fgb_ld_a_p_bc(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_a_p_de(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_a_p_hli(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_a_p_hld(struct fgb_cpu* cpu, const fgb_instruction* ins);

// INC reg16
void fgb_inc_bc(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_inc_de(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_inc_hl(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_inc_sp(struct fgb_cpu* cpu, const fgb_instruction* ins);

// DEC reg16
void fgb_dec_bc(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_dec_de(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_dec_hl(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_dec_sp(struct fgb_cpu* cpu, const fgb_instruction* ins);

// INC reg8/(HL)
void fgb_inc_b(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_inc_d(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_inc_h(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_inc_c(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_inc_e(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_inc_l(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_inc_a(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_inc_p_hl(struct fgb_cpu* cpu, const fgb_instruction* ins);

// DEC reg8/(HL)
void fgb_dec_b(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_dec_d(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_dec_h(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_dec_c(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_dec_e(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_dec_l(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_dec_a(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_dec_p_hl(struct fgb_cpu* cpu, const fgb_instruction* ins);

// Rot A
void fgb_rlca(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_rrca(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_rla(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_rra(struct fgb_cpu* cpu, const fgb_instruction* ins);

// Various
void fgb_daa(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_cpl(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_scf(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ccf(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_p_imm_sp(struct fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand);

// JR rel8
void fgb_jr(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);
void fgb_jr_nz(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);
void fgb_jr_z(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);
void fgb_jr_nc(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);
void fgb_jr_c(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);

// ADD HL, reg16
void fgb_add_hl_bc(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_add_hl_de(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_add_hl_hl(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_add_hl_sp(struct fgb_cpu* cpu, const fgb_instruction* ins);

// ADD SP, imm
void fgb_add_sp_imm(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);

// LD B, reg8/(HL)
void fgb_ld_b_b(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_b_c(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_b_d(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_b_e(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_b_h(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_b_l(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_b_a(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_b_p_hl(struct fgb_cpu* cpu, const fgb_instruction* ins);

// LD C, reg8/(HL)
void fgb_ld_c_b(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_c_c(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_c_d(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_c_e(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_c_h(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_c_l(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_c_a(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_c_p_hl(struct fgb_cpu* cpu, const fgb_instruction* ins);

// LD D, reg8/(HL)
void fgb_ld_d_b(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_d_c(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_d_d(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_d_e(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_d_h(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_d_l(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_d_a(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_d_p_hl(struct fgb_cpu* cpu, const fgb_instruction* ins);

// LD E, reg8/(HL)
void fgb_ld_e_b(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_e_c(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_e_d(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_e_e(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_e_h(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_e_l(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_e_a(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_e_p_hl(struct fgb_cpu* cpu, const fgb_instruction* ins);

// LD H, reg8/(HL)
void fgb_ld_h_b(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_h_c(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_h_d(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_h_e(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_h_h(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_h_l(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_h_a(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_h_p_hl(struct fgb_cpu* cpu, const fgb_instruction* ins);

// LD L, reg8/(HL)
void fgb_ld_l_b(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_l_c(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_l_d(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_l_e(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_l_h(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_l_l(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_l_a(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_l_p_hl(struct fgb_cpu* cpu, const fgb_instruction* ins);

// LD A, reg8/(HL)
void fgb_ld_a_b(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_a_c(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_a_d(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_a_e(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_a_h(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_a_l(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_a_a(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_a_p_hl(struct fgb_cpu* cpu, const fgb_instruction* ins);

// LD (HL), reg8
void fgb_ld_p_hl_b(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_p_hl_c(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_p_hl_d(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_p_hl_e(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_p_hl_h(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_p_hl_l(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_p_hl_a(struct fgb_cpu* cpu, const fgb_instruction* ins);

// ADD A, reg8/(HL)
void fgb_add_a_b(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_add_a_c(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_add_a_d(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_add_a_e(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_add_a_h(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_add_a_l(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_add_a_a(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_add_a_p_hl(struct fgb_cpu* cpu, const fgb_instruction* ins);

// ADC A, reg8/(HL)
void fgb_adc_a_b(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_adc_a_c(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_adc_a_d(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_adc_a_e(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_adc_a_h(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_adc_a_l(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_adc_a_a(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_adc_a_p_hl(struct fgb_cpu* cpu, const fgb_instruction* ins);

// SUB A, reg8/(HL)
void fgb_sub_a_b(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_sub_a_c(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_sub_a_d(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_sub_a_e(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_sub_a_h(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_sub_a_l(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_sub_a_a(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_sub_a_p_hl(struct fgb_cpu* cpu, const fgb_instruction* ins);

// SBC A, reg8/(HL)
void fgb_sbc_a_b(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_sbc_a_c(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_sbc_a_d(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_sbc_a_e(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_sbc_a_h(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_sbc_a_l(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_sbc_a_a(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_sbc_a_p_hl(struct fgb_cpu* cpu, const fgb_instruction* ins);

// AND A, reg8/(HL)
void fgb_and_a_b(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_and_a_c(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_and_a_d(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_and_a_e(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_and_a_h(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_and_a_l(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_and_a_a(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_and_a_p_hl(struct fgb_cpu* cpu, const fgb_instruction* ins);

// XOR A, reg8/(HL)
void fgb_xor_a_b(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_xor_a_c(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_xor_a_d(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_xor_a_e(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_xor_a_h(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_xor_a_l(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_xor_a_a(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_xor_a_p_hl(struct fgb_cpu* cpu, const fgb_instruction* ins);

// OR A, reg8/(HL)
void fgb_or_a_b(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_or_a_c(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_or_a_d(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_or_a_e(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_or_a_h(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_or_a_l(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_or_a_a(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_or_a_p_hl(struct fgb_cpu* cpu, const fgb_instruction* ins);

// CP A, reg8/(HL)
void fgb_cp_a_b(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_cp_a_c(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_cp_a_d(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_cp_a_e(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_cp_a_h(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_cp_a_l(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_cp_a_a(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_cp_a_p_hl(struct fgb_cpu* cpu, const fgb_instruction* ins);

// OP a, imm
void fgb_add_a_imm(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);
void fgb_adc_a_imm(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);
void fgb_sub_a_imm(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);
void fgb_sbc_a_imm(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);
void fgb_and_a_imm(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);
void fgb_xor_a_imm(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);
void fgb_or_a_imm(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);
void fgb_cp_a_imm(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);

// PUSH/POP reg16
void fgb_push_bc(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_push_de(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_push_hl(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_push_af(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_pop_bc(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_pop_de(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_pop_hl(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_pop_af(struct fgb_cpu* cpu, const fgb_instruction* ins);

// Various LD ...
void fgb_ld_p_imm_a(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);
void fgb_ld_a_p_imm(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);
void fgb_ld_p_c_a(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_a_p_c(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ld_p_imm16_a(struct fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand);
void fgb_ld_a_p_imm16(struct fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand);
void fgb_ld_hl_sp_imm(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);
void fgb_ld_sp_hl(struct fgb_cpu* cpu, const fgb_instruction* ins);

// JP [cond], imm16/HL
void fgb_jp_imm16(struct fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand);
void fgb_jp_z_imm16(struct fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand);
void fgb_jp_c_imm16(struct fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand);
void fgb_jp_nz_imm16(struct fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand);
void fgb_jp_nc_imm16(struct fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand);
void fgb_jp_hl(struct fgb_cpu* cpu, const fgb_instruction* ins);

// CALL [cond], imm16
void fgb_call_imm16(struct fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand);
void fgb_call_z_imm16(struct fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand);
void fgb_call_c_imm16(struct fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand);
void fgb_call_nz_imm16(struct fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand);
void fgb_call_nc_imm16(struct fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand);

// RET [cond]
void fgb_ret(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ret_z(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ret_c(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ret_nz(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_ret_nc(struct fgb_cpu* cpu, const fgb_instruction* ins);

// EI/DI
void fgb_ei(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_di(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_reti(struct fgb_cpu* cpu, const fgb_instruction* ins);

// RST n
void fgb_rst_0(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_rst_1(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_rst_2(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_rst_3(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_rst_4(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_rst_5(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_rst_6(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_rst_7(struct fgb_cpu* cpu, const fgb_instruction* ins);

// CB Prefix
void fgb_cb(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t opcode);

#endif // FGB_INSTRUCTION_H
