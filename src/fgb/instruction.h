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

#endif // FGB_INSTRUCTION_H
