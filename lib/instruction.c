#include "instruction.h"

#include <string.h>

#include "cpu.h"

#include <ulog.h>

// cycles are multiplied by 4 to convert CPU cycles to clock cycles
#define INS_DEF(disasm, opcode, op_size, cycles, exec) { disasm, opcode, op_size, (cycles) * 4, 0, { (void*)(exec) }, { (void*)(fgb_fmt_##op_size) } }
#define INS_ALT(disasm, opcode, op_size, cycles, alt_cycles, exec) { disasm, opcode, op_size, (cycles) * 4, (alt_cycles) * 4, { (void*)(exec) }, { (void*)(fgb_fmt_##op_size) } }


static void fgb_unimplemented_0(fgb_cpu* cpu, const fgb_instruction* ins) {
    log_error("Unimplemented instruction: %s (0x%02X) at 0x%04X", ins->disassembly, ins->opcode, cpu->regs.pc);
    cpu->halted = true;
}

static void fgb_unimplemented_1(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    char disasm[64];
    snprintf(disasm, sizeof(disasm), ins->disassembly, operand);
    log_error("Unimplemented instruction: %s (0x%02X) at 0x%04X", disasm, ins->opcode, cpu->regs.pc);
    cpu->halted = true;
}

static void fgb_unimplemented_2(fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand) {
    char disasm[64];
    snprintf(disasm, sizeof(disasm), ins->disassembly, operand);
    log_error("Unimplemented instruction: %s (0x%02X) at 0x%04X", disasm, ins->opcode, cpu->regs.pc);
    cpu->halted = true;
}

static char fmt_buffer[64];
static inline char* fgb_fmt_0(const fgb_instruction* ins) {
    if (ins->disassembly == NULL) {
        snprintf(fmt_buffer, sizeof(fmt_buffer), "DB %02X", ins->opcode);
        return fmt_buffer;
    }

    strcpy_s(fmt_buffer, sizeof(fmt_buffer), ins->disassembly);
    return fmt_buffer;
}

static inline char* fgb_fmt_1(const fgb_instruction* ins, uint8_t operand) {
    if (ins->disassembly == NULL) {
        snprintf(fmt_buffer, sizeof(fmt_buffer), "DB %02X %02X", ins->opcode, operand);
        return fmt_buffer;
    }

    snprintf(fmt_buffer, sizeof(fmt_buffer), ins->disassembly, operand);
    return fmt_buffer;
}

static inline char* fgb_fmt_2(const fgb_instruction* ins, uint16_t operand) {
    if (ins->disassembly == NULL) {
        snprintf(fmt_buffer, sizeof(fmt_buffer), "DB %02X %02X %02X", ins->opcode, operand & 0xFF, (operand >> 8) & 0xFF);
        return fmt_buffer;
    }

    snprintf(fmt_buffer, sizeof(fmt_buffer), ins->disassembly, operand);
    return fmt_buffer;
}


const fgb_instruction fgb_instruction_table[FGB_INSTRUCTION_COUNT] = {
    INS_DEF("NOP", 0x00, 0, 1, fgb_nop),
    INS_DEF("LD BC,0x%04X", 0x01, 2, 3, fgb_ld_bc_imm),
    INS_DEF("LD (BC),A", 0x02, 0, 2, fgb_ld_p_bc_a),
    INS_DEF("INC BC", 0x03, 0, 2, fgb_inc_bc),
    INS_DEF("INC B", 0x04, 0, 1, fgb_inc_b),
    INS_DEF("DEC B", 0x05, 0, 1, fgb_dec_b),
    INS_DEF("LD B,0x%02X", 0x06, 1, 2, fgb_ld_b_imm),
    INS_DEF("RLCA", 0x07, 0, 1, fgb_rlca),
    INS_DEF("LD (0x%04X),SP", 0x08, 2, 5, fgb_ld_p_imm_sp),
    INS_DEF("ADD HL,BC", 0x09, 0, 2, fgb_add_hl_bc),
    INS_DEF("LD A,(BC)", 0x0A, 0, 2, fgb_ld_a_p_bc),
    INS_DEF("DEC BC", 0x0B, 0, 2, fgb_dec_bc),
    INS_DEF("INC C", 0x0C, 0, 1, fgb_inc_c),
    INS_DEF("DEC C", 0x0D, 0, 1, fgb_dec_c),
    INS_DEF("LD C,0x%02X", 0x0E, 1, 2, fgb_ld_c_imm),
    INS_DEF("RRCA", 0x0F, 0, 1, fgb_rrca),

    INS_DEF("STOP", 0x10, 0, 2, fgb_stop),
    INS_DEF("LD DE,0x%04X", 0x11, 2, 3, fgb_ld_de_imm),
    INS_DEF("LD (DE),A", 0x12, 0, 2, fgb_ld_p_de_a),
    INS_DEF("INC DE", 0x13, 0, 2, fgb_inc_de),
    INS_DEF("INC D", 0x14, 0, 1, fgb_inc_d),
    INS_DEF("DEC D", 0x15, 0, 1, fgb_dec_d),
    INS_DEF("LD D,0x%02X", 0x16, 1, 2, fgb_ld_d_imm),
    INS_DEF("RLA", 0x17, 0, 1, fgb_rla),
    INS_DEF("JR 0x%02X", 0x18, 1, 3, fgb_jr),
    INS_DEF("ADD HL,DE", 0x19, 0, 2, fgb_add_hl_de),
    INS_DEF("LD A,(DE)", 0x1A, 0, 2, fgb_ld_a_p_de),
    INS_DEF("DEC DE", 0x1B, 0, 2, fgb_dec_de),
    INS_DEF("INC E", 0x1C, 0, 1, fgb_inc_e),
    INS_DEF("DEC E", 0x1D, 0, 1, fgb_dec_e),
    INS_DEF("LD E,0x%02X", 0x1E, 1, 2, fgb_ld_e_imm),
    INS_DEF("RRA", 0x1F, 0, 1, fgb_rra),

    INS_ALT("JR NZ,0x%02X", 0x20, 1, 2, 3, fgb_jr_nz),
    INS_DEF("LD HL,0x%04X", 0x21, 2, 3, fgb_ld_hl_imm),
    INS_DEF("LD (HL+),A", 0x22, 0, 2, fgb_ld_p_hli_a),
    INS_DEF("INC HL", 0x23, 0, 2, fgb_inc_hl),
    INS_DEF("INC H", 0x24, 0, 1, fgb_inc_h),
    INS_DEF("DEC H", 0x25, 0, 1, fgb_dec_h),
    INS_DEF("LD H,0x%02X", 0x26, 1, 2, fgb_ld_h_imm),
    INS_DEF("DAA", 0x27, 0, 1, fgb_daa),
    INS_ALT("JR Z,0x%02X", 0x28, 1, 2, 3, fgb_jr_z),
    INS_DEF("ADD HL,HL", 0x29, 0, 2, fgb_add_hl_hl),
    INS_DEF("LD A,(HL+)", 0x2A, 0, 2, fgb_ld_a_p_hli),
    INS_DEF("DEC HL", 0x2B, 0, 2, fgb_dec_hl),
    INS_DEF("INC L", 0x2C, 0, 1, fgb_inc_l),
    INS_DEF("DEC L", 0x2D, 0, 1, fgb_dec_l),
    INS_DEF("LD L,0x%02X", 0x2E, 1, 2, fgb_ld_l_imm),
    INS_DEF("CPL", 0x2F, 0, 1, fgb_cpl),

    INS_ALT("JR NC,0x%02X", 0x30, 1, 2, 3, fgb_jr_nc),
    INS_DEF("LD SP,0x%04X", 0x31, 2, 3, fgb_ld_sp_imm),
    INS_DEF("LD (HL-),A", 0x32, 0, 2, fgb_ld_p_hld_a),
    INS_DEF("INC SP", 0x33, 0, 2, fgb_inc_sp),
    INS_DEF("INC (HL)", 0x34, 0, 3, fgb_inc_p_hl),
    INS_DEF("DEC (HL)", 0x35, 0, 3, fgb_dec_p_hl),
    INS_DEF("LD (HL),0x%02X", 0x36, 1, 3, fgb_ld_p_hl_imm),
    INS_DEF("SCF", 0x37, 0, 1, fgb_scf),
    INS_ALT("JR C,0x%02X", 0x38, 1, 2, 3, fgb_jr_c),
    INS_DEF("ADD HL,SP", 0x39, 0, 2, fgb_add_hl_sp),
    INS_DEF("LD A,(HL-)", 0x3A, 0, 2, fgb_ld_a_p_hld),
    INS_DEF("DEC SP", 0x3B, 0, 2, fgb_dec_sp),
    INS_DEF("INC A", 0x3C, 0, 1, fgb_inc_a),
    INS_DEF("DEC A", 0x3D, 0, 1, fgb_dec_a),
    INS_DEF("LD A,0x%02X", 0x3E, 1, 2, fgb_ld_a_imm),
    INS_DEF("CCF", 0x3F, 0, 1, fgb_ccf),

    INS_DEF("LD B,B", 0x40, 0, 1, fgb_ld_b_b),
    INS_DEF("LD B,C", 0x41, 0, 1, fgb_ld_b_c),
    INS_DEF("LD B,D", 0x42, 0, 1, fgb_ld_b_d),
    INS_DEF("LD B,E", 0x43, 0, 1, fgb_ld_b_e),
    INS_DEF("LD B,H", 0x44, 0, 1, fgb_ld_b_h),
    INS_DEF("LD B,L", 0x45, 0, 1, fgb_ld_b_l),
    INS_DEF("LD B,(HL)", 0x46, 0, 2, fgb_ld_b_p_hl),
    INS_DEF("LD B,A", 0x47, 0, 1, fgb_ld_b_a),
    INS_DEF("LD C,B", 0x48, 0, 1, fgb_ld_c_b),
    INS_DEF("LD C,C", 0x49, 0, 1, fgb_ld_c_c),
    INS_DEF("LD C,D", 0x4A, 0, 1, fgb_ld_c_d),
    INS_DEF("LD C,E", 0x4B, 0, 1, fgb_ld_c_e),
    INS_DEF("LD C,H", 0x4C, 0, 1, fgb_ld_c_h),
    INS_DEF("LD C,L", 0x4D, 0, 1, fgb_ld_c_l),
    INS_DEF("LD C,(HL)", 0x4E, 0, 2, fgb_ld_c_p_hl),
    INS_DEF("LD C,A", 0x4F, 0, 1, fgb_ld_c_a),

    INS_DEF("LD D,B", 0x50, 0, 1, fgb_ld_d_b),
    INS_DEF("LD D,C", 0x51, 0, 1, fgb_ld_d_c),
    INS_DEF("LD D,D", 0x52, 0, 1, fgb_ld_d_d),
    INS_DEF("LD D,E", 0x53, 0, 1, fgb_ld_d_e),
    INS_DEF("LD D,H", 0x54, 0, 1, fgb_ld_d_h),
    INS_DEF("LD D,L", 0x55, 0, 1, fgb_ld_d_l),
    INS_DEF("LD D,(HL)", 0x56, 0, 2, fgb_ld_d_p_hl),
    INS_DEF("LD D,A", 0x57, 0, 1, fgb_ld_d_a),
    INS_DEF("LD E,B", 0x58, 0, 1, fgb_ld_e_b),
    INS_DEF("LD E,C", 0x59, 0, 1, fgb_ld_e_c),
    INS_DEF("LD E,D", 0x5A, 0, 1, fgb_ld_e_d),
    INS_DEF("LD E,E", 0x5B, 0, 1, fgb_ld_e_e),
    INS_DEF("LD E,H", 0x5C, 0, 1, fgb_ld_e_h),
    INS_DEF("LD E,L", 0x5D, 0, 1, fgb_ld_e_l),
    INS_DEF("LD E,(HL)", 0x5E, 0, 2, fgb_ld_e_p_hl),
    INS_DEF("LD E,A", 0x5F, 0, 1, fgb_ld_e_a),

    INS_DEF("LD H,B", 0x60, 0, 1, fgb_ld_h_b),
    INS_DEF("LD H,C", 0x61, 0, 1, fgb_ld_h_c),
    INS_DEF("LD H,D", 0x62, 0, 1, fgb_ld_h_d),
    INS_DEF("LD H,E", 0x63, 0, 1, fgb_ld_h_e),
    INS_DEF("LD H,H", 0x64, 0, 1, fgb_ld_h_h),
    INS_DEF("LD H,L", 0x65, 0, 1, fgb_ld_h_l),
    INS_DEF("LD H,(HL)", 0x66, 0, 2, fgb_ld_h_p_hl),
    INS_DEF("LD H,A", 0x67, 0, 1, fgb_ld_h_a),
    INS_DEF("LD L,B", 0x68, 0, 1, fgb_ld_l_b),
    INS_DEF("LD L,C", 0x69, 0, 1, fgb_ld_l_c),
    INS_DEF("LD L,D", 0x6A, 0, 1, fgb_ld_l_d),
    INS_DEF("LD L,E", 0x6B, 0, 1, fgb_ld_l_e),
    INS_DEF("LD L,H", 0x6C, 0, 1, fgb_ld_l_h),
    INS_DEF("LD L,L", 0x6D, 0, 1, fgb_ld_l_l),
    INS_DEF("LD L,(HL)", 0x6E, 0, 2, fgb_ld_l_p_hl),
    INS_DEF("LD L,A", 0x6F, 0, 1, fgb_ld_l_a),

    INS_DEF("LD (HL),B", 0x70, 0, 2, fgb_ld_p_hl_b),
    INS_DEF("LD (HL),C", 0x71, 0, 2, fgb_ld_p_hl_c),
    INS_DEF("LD (HL),D", 0x72, 0, 2, fgb_ld_p_hl_d),
    INS_DEF("LD (HL),E", 0x73, 0, 2, fgb_ld_p_hl_e),
    INS_DEF("LD (HL),H", 0x74, 0, 2, fgb_ld_p_hl_h),
    INS_DEF("LD (HL),L", 0x75, 0, 2, fgb_ld_p_hl_l),
    INS_DEF("HALT", 0x76, 0, 1, fgb_halt),
    INS_DEF("LD (HL),A", 0x77, 0, 2, fgb_ld_p_hl_a),
    INS_DEF("LD A,B", 0x78, 0, 1, fgb_ld_a_b),
    INS_DEF("LD A,C", 0x79, 0, 1, fgb_ld_a_c),
    INS_DEF("LD A,D", 0x7A, 0, 1, fgb_ld_a_d),
    INS_DEF("LD A,E", 0x7B, 0, 1, fgb_ld_a_e),
    INS_DEF("LD A,H", 0x7C, 0, 1, fgb_ld_a_h),
    INS_DEF("LD A,L", 0x7D, 0, 1, fgb_ld_a_l),
    INS_DEF("LD A,(HL)", 0x7E, 0, 2, fgb_ld_a_p_hl),
    INS_DEF("LD A,A", 0x7F, 0, 1, fgb_ld_a_a),

    INS_DEF("ADD A,B", 0x80, 0, 1, fgb_add_a_b),
    INS_DEF("ADD A,C", 0x81, 0, 1, fgb_add_a_c),
    INS_DEF("ADD A,D", 0x82, 0, 1, fgb_add_a_d),
    INS_DEF("ADD A,E", 0x83, 0, 1, fgb_add_a_e),
    INS_DEF("ADD A,H", 0x84, 0, 1, fgb_add_a_h),
    INS_DEF("ADD A,L", 0x85, 0, 1, fgb_add_a_l),
    INS_DEF("ADD A,(HL)", 0x86, 0, 2, fgb_add_a_p_hl),
    INS_DEF("ADD A,A", 0x87, 0, 1, fgb_add_a_a),
    INS_DEF("ADC A,B", 0x88, 0, 1, fgb_adc_a_b),
    INS_DEF("ADC A,C", 0x89, 0, 1, fgb_adc_a_c),
    INS_DEF("ADC A,D", 0x8A, 0, 1, fgb_adc_a_d),
    INS_DEF("ADC A,E", 0x8B, 0, 1, fgb_adc_a_e),
    INS_DEF("ADC A,H", 0x8C, 0, 1, fgb_adc_a_h),
    INS_DEF("ADC A,L", 0x8D, 0, 1, fgb_adc_a_l),
    INS_DEF("ADC A,(HL)", 0x8E, 0, 2, fgb_adc_a_p_hl),
    INS_DEF("ADC A,A", 0x8F, 0, 1, fgb_adc_a_a),

    INS_DEF("SUB B", 0x90, 0, 1, fgb_sub_a_b),
    INS_DEF("SUB C", 0x91, 0, 1, fgb_sub_a_c),
    INS_DEF("SUB D", 0x92, 0, 1, fgb_sub_a_d),
    INS_DEF("SUB E", 0x93, 0, 1, fgb_sub_a_e),
    INS_DEF("SUB H", 0x94, 0, 1, fgb_sub_a_h),
    INS_DEF("SUB L", 0x95, 0, 1, fgb_sub_a_l),
    INS_DEF("SUB (HL)", 0x96, 0, 2, fgb_sub_a_p_hl),
    INS_DEF("SUB A", 0x97, 0, 1, fgb_sub_a_a),
    INS_DEF("SBC A,B", 0x98, 0, 1, fgb_sbc_a_b),
    INS_DEF("SBC A,C", 0x99, 0, 1, fgb_sbc_a_c),
    INS_DEF("SBC A,D", 0x9A, 0, 1, fgb_sbc_a_d),
    INS_DEF("SBC A,E", 0x9B, 0, 1, fgb_sbc_a_e),
    INS_DEF("SBC A,H", 0x9C, 0, 1, fgb_sbc_a_h),
    INS_DEF("SBC A,L", 0x9D, 0, 1, fgb_sbc_a_l),
    INS_DEF("SBC A,(HL)", 0x9E, 0, 2, fgb_sbc_a_p_hl),
    INS_DEF("SBC A,A", 0x9F, 0, 1, fgb_sbc_a_a),

    INS_DEF("AND B", 0xA0, 0, 1, fgb_and_a_b),
    INS_DEF("AND C", 0xA1, 0, 1, fgb_and_a_c),
    INS_DEF("AND D", 0xA2, 0, 1, fgb_and_a_d),
    INS_DEF("AND E", 0xA3, 0, 1, fgb_and_a_e),
    INS_DEF("AND H", 0xA4, 0, 1, fgb_and_a_h),
    INS_DEF("AND L", 0xA5, 0, 1, fgb_and_a_l),
    INS_DEF("AND (HL)", 0xA6, 0, 2, fgb_and_a_p_hl),
    INS_DEF("AND A", 0xA7, 0, 1, fgb_and_a_a),
    INS_DEF("XOR B", 0xA8, 0, 1, fgb_xor_a_b),
    INS_DEF("XOR C", 0xA9, 0, 1, fgb_xor_a_c),
    INS_DEF("XOR D", 0xAA, 0, 1, fgb_xor_a_d),
    INS_DEF("XOR E", 0xAB, 0, 1, fgb_xor_a_e),
    INS_DEF("XOR H", 0xAC, 0, 1, fgb_xor_a_h),
    INS_DEF("XOR L", 0xAD, 0, 1, fgb_xor_a_l),
    INS_DEF("XOR (HL)", 0xAE, 0, 2, fgb_xor_a_p_hl),
    INS_DEF("XOR A", 0xAF, 0, 1, fgb_xor_a_a),

    INS_DEF("OR B", 0xB0, 0, 1, fgb_or_a_b),
    INS_DEF("OR C", 0xB1, 0, 1, fgb_or_a_c),
    INS_DEF("OR D", 0xB2, 0, 1, fgb_or_a_d),
    INS_DEF("OR E", 0xB3, 0, 1, fgb_or_a_e),
    INS_DEF("OR H", 0xB4, 0, 1, fgb_or_a_h),
    INS_DEF("OR L", 0xB5, 0, 1, fgb_or_a_l),
    INS_DEF("OR (HL)", 0xB6, 0, 2, fgb_or_a_p_hl),
    INS_DEF("OR A", 0xB7, 0, 1, fgb_or_a_a),
    INS_DEF("CP B", 0xB8, 0, 1, fgb_cp_a_b),
    INS_DEF("CP C", 0xB9, 0, 1, fgb_cp_a_c),
    INS_DEF("CP D", 0xBA, 0, 1, fgb_cp_a_d),
    INS_DEF("CP E", 0xBB, 0, 1, fgb_cp_a_e),
    INS_DEF("CP H", 0xBC, 0, 1, fgb_cp_a_h),
    INS_DEF("CP L", 0xBD, 0, 1, fgb_cp_a_l),
    INS_DEF("CP (HL)", 0xBE, 0, 2, fgb_cp_a_p_hl),
    INS_DEF("CP A", 0xBF, 0, 1, fgb_cp_a_a),

    INS_ALT("RET NZ", 0xC0, 0, 2, 5, fgb_ret_nz),
    INS_DEF("POP BC", 0xC1, 0, 3, fgb_pop_bc),
    INS_ALT("JP NZ,0x%04X", 0xC2, 2, 3, 4, fgb_jp_nz_imm16),
    INS_DEF("JP 0x%04X", 0xC3, 2, 4, fgb_jp_imm16),
    INS_ALT("CALL NZ,0x%04X", 0xC4, 2, 3, 6, fgb_call_nz_imm16),
    INS_DEF("PUSH BC", 0xC5, 0, 4, fgb_push_bc),
    INS_DEF("ADD A,0x%02X", 0xC6, 1, 2, fgb_add_a_imm),
    INS_DEF("RST 0", 0xC7, 0, 4, fgb_rst_0),
    INS_ALT("RET Z", 0xC8, 0, 2, 5, fgb_ret_z),
    INS_DEF("RET", 0xC9, 0, 4, fgb_ret),
    INS_ALT("JP Z,0x%04X", 0xCA, 2, 3, 4, fgb_jp_z_imm16),
    INS_DEF("CB-", 0xCB, 1, 255, fgb_cb),
    INS_ALT("CALL Z,0x%04X", 0xCC, 2, 3, 6, fgb_call_z_imm16),
    INS_DEF("CALL 0x%04X", 0xCD, 2, 6, fgb_call_imm16),
    INS_DEF("ADC A,0x%02X", 0xCE, 1, 2, fgb_adc_a_imm),
    INS_DEF("RST 1", 0xCF, 0, 4, fgb_rst_1),

    INS_ALT("RET NC", 0xD0, 0, 2, 5, fgb_ret_nc),
    INS_DEF("POP DE", 0xD1, 0, 3, fgb_pop_de),
    INS_ALT("JP NC,0x%04X", 0xD2, 2, 3, 4, fgb_jp_nc_imm16),
    INS_DEF(NULL, 0xD3, 1, 255, fgb_unimplemented_0),
    INS_ALT("CALL NC,0x%04X", 0xD4, 2, 3, 6, fgb_call_nc_imm16),
    INS_DEF("PUSH DE", 0xD5, 0, 4, fgb_push_de),
    INS_DEF("SUB 0x%02X", 0xD6, 1, 2, fgb_sub_a_imm),
    INS_DEF("RST 2", 0xD7, 0, 4, fgb_rst_2),
    INS_ALT("RET C", 0xD8, 0, 2, 5, fgb_ret_c),
    INS_DEF("RETI", 0xD9, 0, 4, fgb_reti),
    INS_ALT("JP C,0x%04X", 0xDA, 2, 3, 4, fgb_jp_c_imm16),
    INS_DEF(NULL, 0xDB, 0, 0, fgb_unimplemented_0),
    INS_ALT("CALL C,0x%04X", 0xDC, 2, 3, 6, fgb_call_c_imm16),
    INS_DEF(NULL, 0xDD, 0, 0, fgb_unimplemented_0),
    INS_DEF("SBC A,0x%02X", 0xDE, 1, 2, fgb_sbc_a_imm),
    INS_DEF("RST 3", 0xDF, 0, 4, fgb_rst_3),

    INS_DEF("LDH (0x%02X),A", 0xE0, 1, 3, fgb_ld_p_imm_a),
    INS_DEF("POP HL", 0xE1, 0, 3, fgb_pop_hl),
    INS_DEF("LD (C),A", 0xE2, 0, 2, fgb_ld_p_c_a),
    INS_DEF(NULL, 0xE3, 0, 0, fgb_unimplemented_0),
    INS_DEF(NULL, 0xE4, 0, 0, fgb_unimplemented_0),
    INS_DEF("PUSH HL", 0xE5, 0, 4, fgb_push_hl),
    INS_DEF("AND 0x%02X", 0xE6, 1, 2, fgb_and_a_imm),
    INS_DEF("RST 4", 0xE7, 0, 4, fgb_rst_4),
    INS_DEF("ADD SP,0x%02X", 0xE8, 1, 4, fgb_add_sp_imm),
    INS_DEF("JP HL", 0xE9, 0, 1, fgb_jp_hl),
    INS_DEF("LD (0x%04X),A", 0xEA, 2, 4, fgb_ld_p_imm16_a),
    INS_DEF(NULL, 0xEB, 0, 0, fgb_unimplemented_0),
    INS_DEF(NULL, 0xEC, 0, 0, fgb_unimplemented_0),
    INS_DEF(NULL, 0xED, 0, 0, fgb_unimplemented_0),
    INS_DEF("XOR 0x%02X", 0xEE, 1, 2, fgb_xor_a_imm),
    INS_DEF("RST 5", 0xEF, 0, 4, fgb_rst_5),

    INS_DEF("LDH A,(0x%02X)", 0xF0, 1, 3, fgb_ld_a_p_imm),
    INS_DEF("POP AF", 0xF1, 0, 3, fgb_pop_af),
    INS_DEF("LD A,(C)", 0xF2, 0, 2, fgb_ld_a_p_c),
    INS_DEF("DI", 0xF3, 0, 0, fgb_di),
    INS_DEF(NULL, 0xF4, 0, 0, fgb_unimplemented_0),
    INS_DEF("PUSH AF", 0xF5, 0, 4, fgb_push_af),
    INS_DEF("OR 0x%02X", 0xF6, 1, 2, fgb_or_a_imm),
    INS_DEF("RST 6", 0xF7, 0, 4, fgb_rst_6),
    INS_DEF("LD HL,SP+0x%02X", 0xF8, 1, 3, fgb_ld_hl_sp_imm),
    INS_DEF("LD SP,HL", 0xF9, 0, 2, fgb_ld_sp_hl),
    INS_DEF("LD A,(0x%04X)", 0xFA, 2, 4, fgb_ld_a_p_imm16),
    INS_DEF("EI", 0xFB, 0, 1, fgb_ei),
    INS_DEF(NULL, 0xFC, 0, 0, fgb_unimplemented_0),
    INS_DEF(NULL, 0xFD, 0, 0, fgb_unimplemented_0),
    INS_DEF("CP 0x%02X", 0xFE, 1, 2, fgb_cp_a_imm),
    INS_DEF("RST 7", 0xFF, 0, 4, fgb_rst_7),
};


const uint8_t fgb_cb_instruction_cycles[FGB_INSTRUCTION_COUNT] = {
    2, 2, 2, 2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 4, 2,
    2, 2, 2, 2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 4, 2,
    2, 2, 2, 2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 4, 2,
    2, 2, 2, 2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 4, 2,
    2, 2, 2, 2, 2, 2, 3, 2, 2, 2, 2, 2, 2, 2, 3, 2,
    2, 2, 2, 2, 2, 2, 3, 2, 2, 2, 2, 2, 2, 2, 3, 2,
    2, 2, 2, 2, 2, 2, 3, 2, 2, 2, 2, 2, 2, 2, 3, 2,
    2, 2, 2, 2, 2, 2, 3, 2, 2, 2, 2, 2, 2, 2, 3, 2,
    2, 2, 2, 2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 4, 2,
    2, 2, 2, 2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 4, 2,
    2, 2, 2, 2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 4, 2,
    2, 2, 2, 2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 4, 2,
    2, 2, 2, 2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 4, 2,
    2, 2, 2, 2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 4, 2,
    2, 2, 2, 2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 4, 2,
    2, 2, 2, 2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 4, 2,
};
