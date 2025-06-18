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

void fgb_nop(struct fgb_cpu* cpu, const fgb_instruction* ins);

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

// INC reg16
void fgb_inc_bc(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_inc_de(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_inc_hl(struct fgb_cpu* cpu, const fgb_instruction* ins);
void fgb_inc_sp(struct fgb_cpu* cpu, const fgb_instruction* ins);

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


#endif // FGB_INSTRUCTION_H
