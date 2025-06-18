#ifndef FGB_INSTRUCTION_H
#define FGB_INSTRUCTION_H

#include <stdint.h>

#define FGB_INSTRUCTION_COUNT 256


struct fgb_cpu;
struct fgb_instruction;

typedef void (*fgb_instruction_exec_0)(struct fgb_cpu* cpu, const struct fgb_instruction* ins);
typedef void (*fgb_instruction_exec_1)(struct fgb_cpu* cpu, const struct fgb_instruction* ins, uint8_t operand);
typedef void (*fgb_instruction_exec_2)(struct fgb_cpu* cpu, const struct fgb_instruction* ins, uint16_t operand);

typedef struct fgb_instruction {
    const char* disassembly;
    uint8_t opcode;
    uint8_t operand_size; // 0, 1, or 2 bytes
    uint8_t cycles; // Number of cycles the instruction takes to execute
    union {
        void* exec;
        fgb_instruction_exec_0 exec_0;
        fgb_instruction_exec_1 exec_1;
        fgb_instruction_exec_2 exec_2;
    };
} fgb_instruction;

extern fgb_instruction fgb_instruction_table[FGB_INSTRUCTION_COUNT];

static inline fgb_instruction* fgb_instruction_get(uint8_t opcode) {
    return fgb_instruction_table + opcode;
}

// x0
void fgb_nop(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_stop(struct fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand);

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


#endif // FGB_INSTRUCTION_H
