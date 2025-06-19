#include "cpu.h"
#include "instruction.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <ulog.h>


static uint8_t fgb_cpu_fetch(fgb_cpu* cpu);
static uint16_t fgb_cpu_fetch_u16(fgb_cpu* cpu);
#define fgb_mmu_write(cpu, addr, value) (cpu)->mmu.write_u8(&(cpu)->mmu, addr, value)
#define fgb_mmu_read_u8(cpu, addr) (cpu)->mmu.read_u8(&(cpu)->mmu, addr)
#define fgb_mmu_read_u16(cpu, addr) (cpu)->mmu.read_u16(&(cpu)->mmu, addr)

struct fgb_init_value {
    uint16_t addr;
    uint8_t value;
};
static const struct fgb_init_value fgb_init_table[] = {
    { 0xFF00, 0xCF }, // P1
    { 0xFF01, 0x00 }, // SB
    { 0xFF02, 0x7E }, // SC
    { 0xFF04, 0x18 }, // DIV
    { 0xFF05, 0x00 }, // TIMA
    { 0xFF06, 0x00 }, // TMA
    { 0xFF07, 0xF8 }, // TAC
    { 0xFF0F, 0xE1 }, // IF
    { 0xFF10, 0x80 }, // NR10
    { 0xFF11, 0xBF }, // NR11
    { 0xFF12, 0xF3 }, // NR12
    { 0xFF13, 0xFF }, // NR13
    { 0xFF14, 0xBF }, // NR14
    { 0xFF16, 0x3F }, // NR21
    { 0xFF17, 0x00 }, // NR22
    { 0xFF18, 0xFF }, // NR23
    { 0xFF19, 0xBF }, // NR24
    { 0xFF1A, 0x7F }, // NR30
    { 0xFF1B, 0xFF }, // NR31
    { 0xFF1C, 0x9F }, // NR32
    { 0xFF1D, 0xFF }, // NR33
    { 0xFF1E, 0xBF }, // NR34
    { 0xFF20, 0xFF }, // NR41
    { 0xFF21, 0x00 }, // NR42
    { 0xFF22, 0x00 }, // NR43
    { 0xFF23, 0xBF }, // NR44
    { 0xFF24, 0x77 }, // NR50
    { 0xFF25, 0xF3 }, // NR51
    { 0xFF26, 0xF1 }, // NR52
    { 0xFF40, 0x91 }, // LCDC
    { 0xFF41, 0x81 }, // STAT
    { 0xFF42, 0x00 }, // SCY
    { 0xFF43, 0x00 }, // SCX
    { 0xFF44, 0x00 }, // LY
    { 0xFF45, 0x00 }, // LYC
    { 0xFF46, 0xFF }, // DMA
    { 0xFF47, 0xFC }, // BGP
    { 0xFF48, 0xFF }, // OBP0
    { 0xFF49, 0xFF }, // OBP1
    { 0xFF4A, 0x00 }, // WY
    { 0xFF4B, 0x00 }, // WX
    { 0xFFFF, 0x00 }, // IE
};


fgb_cpu* fgb_cpu_create(void) {
    fgb_cpu* cpu = malloc(sizeof(fgb_cpu));
    if (!cpu) {
        return NULL;
    }

    memset(cpu, 0, sizeof(fgb_cpu));

    fgb_mmu_init(&cpu->mmu, NULL);
    fgb_cpu_reset(cpu);

    return cpu;
}

fgb_cpu* fgb_cpu_create_with(const fgb_mmu_ops* mmu_ops) {
    fgb_cpu* cpu = malloc(sizeof(fgb_cpu));
    if (!cpu) {
        return NULL;
    }

    memset(cpu, 0, sizeof(fgb_cpu));

    fgb_mmu_init(&cpu->mmu, mmu_ops);
    fgb_cpu_reset(cpu);

    return cpu;
}

void fgb_cpu_destroy(fgb_cpu* cpu) {
    free(cpu);
}

void fgb_cpu_reset(fgb_cpu* cpu) {
    memset(&cpu->regs, 0, sizeof(cpu->regs));
    cpu->halted = false;
    cpu->stopped = false;

    cpu->regs.pc = 0x0100;
    cpu->regs.sp = 0xFFFE;

    cpu->regs.af = 0x01B0;
    cpu->regs.bc = 0x0013;
    cpu->regs.de = 0x00D8;
    cpu->regs.hl = 0x014D;
    
    for (size_t i = 0; i < sizeof(fgb_init_table) / sizeof(fgb_init_table[0]); i++) {
        cpu->mmu.write_u8(&cpu->mmu, fgb_init_table[i].addr, fgb_init_table[i].value);
    }
}

void fgb_cpu_step(fgb_cpu* cpu) {
    int cycles = 0;

    while (cycles < FGB_CYCLES_PER_FRAME) {
        cycles += fgb_cpu_execute(cpu);
    }
}

int fgb_cpu_execute(fgb_cpu* cpu) {
    const uint8_t opcode = fgb_cpu_fetch(cpu);
    const fgb_instruction* instruction = fgb_instruction_get(opcode);
    assert(instruction->opcode == opcode);

    switch (instruction->operand_size) {
    case 0:
        instruction->exec_0(cpu, instruction);
        break;

    case 1:
        instruction->exec_1(cpu, instruction, fgb_cpu_fetch(cpu));
        break;

    case 2:
        instruction->exec_2(cpu, instruction, fgb_cpu_fetch_u16(cpu));
        break;

    default:
        log_error("Invalid operand size: %d", instruction->operand_size);
        cpu->halted = true;
        return FGB_CYCLES_PER_FRAME;
    }

    return instruction->cycles;
}

uint8_t fgb_cpu_fetch(fgb_cpu* cpu) {
    return fgb_mmu_read_u8(cpu, cpu->regs.pc++);
}

uint16_t fgb_cpu_fetch_u16(fgb_cpu* cpu) {
    const uint16_t value = fgb_mmu_read_u16(cpu, cpu->regs.pc);
    cpu->regs.pc += 2;
    return value;
}


// --------------------------------------------------------------
// Instruction execution functions
// --------------------------------------------------------------

static inline uint8_t fgb_inc_u8(fgb_cpu* cpu, uint8_t value) {
    cpu->regs.flags.h = (value & 0xF) == 0xF;
    value++;
    cpu->regs.flags.z = value == 0;
    cpu->regs.flags.n = 0;

    return value;
}

static inline uint8_t fgb_dec_u8(fgb_cpu* cpu, uint8_t value) {
    cpu->regs.flags.h = (value & 0xF) == 0;
    value--;
    cpu->regs.flags.z = value == 0;
    cpu->regs.flags.n = 1;
    return value;
}

static inline uint8_t fgb_add_u8(fgb_cpu* cpu, uint8_t a, uint8_t b) {
    const uint16_t result = a + b;
    cpu->regs.flags.z = (result & 0xFF) == 0;
    cpu->regs.flags.n = 0;
    cpu->regs.flags.h = (a & 0xF) + (b & 0xF) > 0xF;
    cpu->regs.flags.c = result > 0xFF;
    return result & 0xFF;
}

static inline uint16_t fgb_add_u16(fgb_cpu* cpu, uint16_t a, uint16_t b) {
    const uint32_t result = a + b;
    cpu->regs.flags.c = result > 0xFFFF;
    cpu->regs.flags.h = ((a & 0xFFF) + (b & 0xFFF)) > 0xFFF;
    cpu->regs.flags.n = 0;
    return result & 0xFFFF;
}

static inline uint8_t fgb_adc_u8(fgb_cpu* cpu, uint8_t a, uint8_t b) {
    const uint16_t result = a + b + cpu->regs.flags.c;
    cpu->regs.flags.z = (result & 0xFF) == 0;
    cpu->regs.flags.n = 0;
    cpu->regs.flags.h = (a & 0xF) + (b & 0xF) + cpu->regs.flags.c > 0xF;
    cpu->regs.flags.c = result > 0xFF;
    return result & 0xFF;
}

static inline uint8_t fgb_sub_u8(fgb_cpu* cpu, uint8_t a, uint8_t b) {
    cpu->regs.flags.c = b > a;
    cpu->regs.flags.h = (b & 0xF) > (a & 0xF);
    cpu->regs.flags.n = 1;
    a -= b;
    cpu->regs.flags.z = a == 0;
    return a;
}

static inline uint8_t fgb_sbc_u8(fgb_cpu* cpu, uint8_t a, uint8_t b) {
    cpu->regs.flags.h = (b & 0xF) + cpu->regs.flags.c > (a & 0xF);
    const uint8_t new_c = b + cpu->regs.flags.c > a;
    cpu->regs.flags.n = 1;
    a -= b + cpu->regs.flags.c;
    cpu->regs.flags.c = new_c;
    cpu->regs.flags.z = a == 0;
    return a;
}

static inline uint8_t fgb_and_u8(fgb_cpu* cpu, uint8_t a, uint8_t b) {
    a &= b;
    cpu->regs.flags.z = a == 0;
    cpu->regs.flags.n = 0;
    cpu->regs.flags.h = 1;
    cpu->regs.flags.c = 0;
    return a;
}

static inline uint8_t fgb_xor_u8(fgb_cpu* cpu, uint8_t a, uint8_t b) {
    a ^= b;
    cpu->regs.flags.z = a == 0;
    cpu->regs.flags.n = 0;
    cpu->regs.flags.h = 0;
    cpu->regs.flags.c = 0;
    return a;
}

static inline uint8_t fgb_or_u8(fgb_cpu* cpu, uint8_t a, uint8_t b) {
    a |= b;
    cpu->regs.flags.z = a == 0;
    cpu->regs.flags.n = 0;
    cpu->regs.flags.h = 0;
    cpu->regs.flags.c = 0;
    return a;
}

void fgb_nop(fgb_cpu* cpu, const fgb_instruction* ins) {
    (void)cpu;
    (void)ins;
}

void fgb_stop(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    cpu->stopped = true;
    cpu->halted = true;
}

void fgb_halt(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->halted = true;
}

void fgb_ld_bc_imm(fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand) {
    cpu->regs.bc = operand;
}

void fgb_ld_de_imm(fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand) {
    cpu->regs.de = operand;
}

void fgb_ld_hl_imm(fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand) {
    cpu->regs.hl = operand;
}

void fgb_ld_sp_imm(fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand) {
    cpu->regs.sp = operand;
}

void fgb_ld_p_bc_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_mmu_write(cpu, cpu->regs.bc, cpu->regs.a);
}

void fgb_ld_p_de_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_mmu_write(cpu, cpu->regs.de, cpu->regs.a);
}

void fgb_ld_p_hli_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_mmu_write(cpu, cpu->regs.hl++, cpu->regs.a);
}

void fgb_ld_p_hld_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_mmu_write(cpu, cpu->regs.hl--, cpu->regs.a);
}

void fgb_ld_b_imm(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    cpu->regs.b = operand;
}

void fgb_ld_d_imm(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    cpu->regs.d = operand;
}

void fgb_ld_h_imm(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    cpu->regs.h = operand;
}

void fgb_ld_c_imm(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    cpu->regs.c = operand;
}

void fgb_ld_e_imm(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    cpu->regs.e = operand;
}

void fgb_ld_l_imm(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    cpu->regs.l = operand;
}

void fgb_ld_a_imm(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    cpu->regs.a = operand;
}

void fgb_ld_p_hl_imm(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    fgb_mmu_write(cpu, cpu->regs.hl, operand);
}

void fgb_ld_a_p_bc(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_mmu_read_u8(cpu, cpu->regs.bc);
}

void fgb_ld_a_p_de(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_mmu_read_u8(cpu, cpu->regs.de);
}

void fgb_ld_a_p_hli(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_mmu_read_u8(cpu, cpu->regs.hl);
    cpu->regs.hl++;
}

void fgb_ld_a_p_hld(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_mmu_read_u8(cpu, cpu->regs.hl);
    cpu->regs.hl--;
}

void fgb_inc_bc(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.bc++;
}

void fgb_inc_de(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.de++;
}

void fgb_inc_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.hl++;
}

void fgb_inc_sp(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.sp++;
}

void fgb_dec_bc(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.bc--;
}

void fgb_dec_de(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.de--;
}

void fgb_dec_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.hl--;
}

void fgb_dec_sp(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.sp--;
}

void fgb_inc_b(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.b = fgb_inc_u8(cpu, cpu->regs.b);
}

void fgb_inc_d(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.d = fgb_inc_u8(cpu, cpu->regs.d);
}

void fgb_inc_h(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.h = fgb_inc_u8(cpu, cpu->regs.h);
}

void fgb_inc_c(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.c = fgb_inc_u8(cpu, cpu->regs.c);
}

void fgb_inc_e(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.e = fgb_inc_u8(cpu, cpu->regs.e);
}

void fgb_inc_l(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.l = fgb_inc_u8(cpu, cpu->regs.l);
}

void fgb_inc_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_inc_u8(cpu, cpu->regs.a);
}

void fgb_inc_p_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    uint8_t value = fgb_mmu_read_u8(cpu, cpu->regs.hl);
    value = fgb_inc_u8(cpu, value);
    fgb_mmu_write(cpu, cpu->regs.hl, value);
}

void fgb_dec_b(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.b = fgb_dec_u8(cpu, cpu->regs.b);
}

void fgb_dec_d(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.d = fgb_dec_u8(cpu, cpu->regs.d);
}

void fgb_dec_h(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.h = fgb_dec_u8(cpu, cpu->regs.h);
}

void fgb_dec_c(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.c = fgb_dec_u8(cpu, cpu->regs.c);
}

void fgb_dec_e(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.e = fgb_dec_u8(cpu, cpu->regs.e);
}

void fgb_dec_l(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.l = fgb_dec_u8(cpu, cpu->regs.l);
}

void fgb_dec_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_dec_u8(cpu, cpu->regs.a);
}

void fgb_dec_p_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    uint8_t value = fgb_mmu_read_u8(cpu, cpu->regs.hl);
    value = fgb_dec_u8(cpu, value);
    fgb_mmu_write(cpu, cpu->regs.hl, value);
}

void fgb_rlca(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.flags.c = cpu->regs.a >> 7;
    cpu->regs.a <<= 1;
    cpu->regs.a |= cpu->regs.flags.c;

    cpu->regs.flags.z = 0;
    cpu->regs.flags.n = 0;
    cpu->regs.flags.h = 0;
}

void fgb_rrca(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.flags.c = cpu->regs.a & 1;
    cpu->regs.a >>= 1;
    cpu->regs.a |= cpu->regs.flags.c << 7;

    cpu->regs.flags.z = 0;
    cpu->regs.flags.n = 0;
    cpu->regs.flags.h = 0;
}

void fgb_rla(fgb_cpu* cpu, const fgb_instruction* ins) {
    const uint8_t c = cpu->regs.flags.c;
    cpu->regs.flags.c = cpu->regs.a >> 7;
    cpu->regs.a <<= 1;
    cpu->regs.a |= c;

    cpu->regs.flags.z = 0;
    cpu->regs.flags.n = 0;
    cpu->regs.flags.h = 0;
}

void fgb_rra(fgb_cpu* cpu, const fgb_instruction* ins) {
    const uint8_t c = cpu->regs.flags.c;
    cpu->regs.flags.c = cpu->regs.a & 1;
    cpu->regs.a >>= 1;
    cpu->regs.a |= c << 7;

    cpu->regs.flags.z = 0;
    cpu->regs.flags.n = 0;
    cpu->regs.flags.h = 0;
}

void fgb_daa(fgb_cpu* cpu, const fgb_instruction* ins) {
    uint8_t adj = 0;
    uint16_t a = cpu->regs.a;
    if (cpu->regs.flags.n) {
        if (cpu->regs.flags.h) adj += 0x6;
        if (cpu->regs.flags.c) adj += 0x60;
        a -= adj;
    } else {
        if (cpu->regs.flags.h || (a & 0xF) > 0x9) adj += 0x6;
        if (cpu->regs.flags.c || a > 0x99) { adj += 0x60; cpu->regs.flags.c = 1; }
        a += adj;
    }

    cpu->regs.a = a;
    cpu->regs.flags.z = cpu->regs.a == 0;
    cpu->regs.flags.h = 0;
}

void fgb_cpl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a ^= 0xFF;
    cpu->regs.flags.n = 1;
    cpu->regs.flags.h = 1;
}

void fgb_scf(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.flags.n = 0;
    cpu->regs.flags.h = 0;
    cpu->regs.flags.c = 1;
}

void fgb_ccf(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.flags.n = 0;
    cpu->regs.flags.h = 0;
    cpu->regs.flags.c ^= 1;
}

void fgb_ld_p_imm_sp(fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand) {
    fgb_mmu_write(cpu, operand + 0, (cpu->regs.sp >> 0) & 0xFF);
    fgb_mmu_write(cpu, operand + 1, (cpu->regs.sp >> 8) & 0xFF);
}

void fgb_jr(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    cpu->regs.pc += (int8_t)operand;
}

void fgb_jr_nz(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    if (!cpu->regs.flags.z) {
        cpu->regs.pc += (int8_t)operand;
    }
}

void fgb_jr_z(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    if (cpu->regs.flags.z) {
        cpu->regs.pc += (int8_t)operand;
    }
}

void fgb_jr_nc(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    if (!cpu->regs.flags.c) {
        cpu->regs.pc += (int8_t)operand;
    }
}

void fgb_jr_c(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    if (cpu->regs.flags.c) {
        cpu->regs.pc += (int8_t)operand;
    }
}

void fgb_add_hl_bc(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.hl = fgb_add_u16(cpu, cpu->regs.hl, cpu->regs.bc);
}

void fgb_add_hl_de(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.hl = fgb_add_u16(cpu, cpu->regs.hl, cpu->regs.de);
}

void fgb_add_hl_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.hl = fgb_add_u16(cpu, cpu->regs.hl, cpu->regs.hl);
}

void fgb_add_hl_sp(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.hl = fgb_add_u16(cpu, cpu->regs.hl, cpu->regs.sp);
}

void fgb_ld_b_b(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.b = cpu->regs.b;
}

void fgb_ld_b_c(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.b = cpu->regs.c;
}

void fgb_ld_b_d(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.b = cpu->regs.d;
}

void fgb_ld_b_e(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.b = cpu->regs.e;
}

void fgb_ld_b_h(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.b = cpu->regs.h;
}

void fgb_ld_b_l(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.b = cpu->regs.l;
}

void fgb_ld_b_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.b = cpu->regs.a;
}

void fgb_ld_b_p_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.b = fgb_mmu_read_u8(cpu, cpu->regs.hl);
}

void fgb_ld_c_b(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.c = cpu->regs.b;
}

void fgb_ld_c_c(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.c = cpu->regs.c;
}

void fgb_ld_c_d(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.c = cpu->regs.d;
}

void fgb_ld_c_e(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.c = cpu->regs.e;
}

void fgb_ld_c_h(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.c = cpu->regs.h;
}

void fgb_ld_c_l(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.c = cpu->regs.l;
}

void fgb_ld_c_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.c = cpu->regs.a;
}

void fgb_ld_c_p_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.c = fgb_mmu_read_u8(cpu, cpu->regs.hl);
}

void fgb_ld_d_b(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.d = cpu->regs.b;
}

void fgb_ld_d_c(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.d = cpu->regs.c;
}

void fgb_ld_d_d(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.d = cpu->regs.d;
}

void fgb_ld_d_e(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.d = cpu->regs.e;
}

void fgb_ld_d_h(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.d = cpu->regs.h;
}

void fgb_ld_d_l(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.d = cpu->regs.l;
}

void fgb_ld_d_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.d = cpu->regs.a;
}

void fgb_ld_d_p_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.d = fgb_mmu_read_u8(cpu, cpu->regs.hl);
}

void fgb_ld_e_b(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.e = cpu->regs.b;
}

void fgb_ld_e_c(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.e = cpu->regs.c;
}

void fgb_ld_e_d(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.e = cpu->regs.d;
}

void fgb_ld_e_e(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.e = cpu->regs.e;
}

void fgb_ld_e_h(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.e = cpu->regs.h;
}

void fgb_ld_e_l(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.e = cpu->regs.l;
}

void fgb_ld_e_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.e = cpu->regs.a;
}

void fgb_ld_e_p_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.e = fgb_mmu_read_u8(cpu, cpu->regs.hl);
}

void fgb_ld_h_b(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.h = cpu->regs.b;
}

void fgb_ld_h_c(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.h = cpu->regs.c;
}

void fgb_ld_h_d(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.h = cpu->regs.d;
}

void fgb_ld_h_e(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.h = cpu->regs.e;
}

void fgb_ld_h_h(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.h = cpu->regs.h;
}

void fgb_ld_h_l(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.h = cpu->regs.l;
}

void fgb_ld_h_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.h = cpu->regs.a;
}

void fgb_ld_h_p_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.h = fgb_mmu_read_u8(cpu, cpu->regs.hl);
}

void fgb_ld_l_b(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.l = cpu->regs.b;
}

void fgb_ld_l_c(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.l = cpu->regs.c;
}

void fgb_ld_l_d(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.l = cpu->regs.d;
}

void fgb_ld_l_e(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.l = cpu->regs.e;
}

void fgb_ld_l_h(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.l = cpu->regs.h;
}

void fgb_ld_l_l(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.l = cpu->regs.l;
}

void fgb_ld_l_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.l = cpu->regs.a;
}

void fgb_ld_l_p_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.l = fgb_mmu_read_u8(cpu, cpu->regs.hl);
}

void fgb_ld_a_b(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = cpu->regs.b;
}

void fgb_ld_a_c(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = cpu->regs.c;
}

void fgb_ld_a_d(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = cpu->regs.d;
}

void fgb_ld_a_e(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = cpu->regs.e;
}

void fgb_ld_a_h(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = cpu->regs.h;
}

void fgb_ld_a_l(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = cpu->regs.l;
}

void fgb_ld_a_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = cpu->regs.a;
}

void fgb_ld_a_p_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_mmu_read_u8(cpu, cpu->regs.hl);
}

void fgb_ld_p_hl_b(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_mmu_write(cpu, cpu->regs.hl, cpu->regs.b);
}

void fgb_ld_p_hl_c(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_mmu_write(cpu, cpu->regs.hl, cpu->regs.c);
}

void fgb_ld_p_hl_d(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_mmu_write(cpu, cpu->regs.hl, cpu->regs.d);
}

void fgb_ld_p_hl_e(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_mmu_write(cpu, cpu->regs.hl, cpu->regs.e);
}

void fgb_ld_p_hl_h(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_mmu_write(cpu, cpu->regs.hl, cpu->regs.h);
}

void fgb_ld_p_hl_l(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_mmu_write(cpu, cpu->regs.hl, cpu->regs.l);
}

void fgb_ld_p_hl_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_mmu_write(cpu, cpu->regs.hl, cpu->regs.a);
}

void fgb_add_a_b(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_add_u8(cpu, cpu->regs.a, cpu->regs.b);
}

void fgb_add_a_c(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_add_u8(cpu, cpu->regs.a, cpu->regs.c);
}

void fgb_add_a_d(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_add_u8(cpu, cpu->regs.a, cpu->regs.d);
}

void fgb_add_a_e(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_add_u8(cpu, cpu->regs.a, cpu->regs.e);
}

void fgb_add_a_h(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_add_u8(cpu, cpu->regs.a, cpu->regs.h);
}

void fgb_add_a_l(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_add_u8(cpu, cpu->regs.a, cpu->regs.l);
}

void fgb_add_a_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_add_u8(cpu, cpu->regs.a, cpu->regs.a);
}

void fgb_add_a_p_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_add_u8(cpu, cpu->regs.a, fgb_mmu_read_u8(cpu, cpu->regs.hl));
}

void fgb_adc_a_b(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_adc_u8(cpu, cpu->regs.a, cpu->regs.b);
}

void fgb_adc_a_c(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_adc_u8(cpu, cpu->regs.a, cpu->regs.c);
}

void fgb_adc_a_d(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_adc_u8(cpu, cpu->regs.a, cpu->regs.d);
}

void fgb_adc_a_e(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_adc_u8(cpu, cpu->regs.a, cpu->regs.e);
}

void fgb_adc_a_h(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_adc_u8(cpu, cpu->regs.a, cpu->regs.h);
}

void fgb_adc_a_l(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_adc_u8(cpu, cpu->regs.a, cpu->regs.l);
}

void fgb_adc_a_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_adc_u8(cpu, cpu->regs.a, cpu->regs.a);
}

void fgb_adc_a_p_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_adc_u8(cpu, cpu->regs.a, fgb_mmu_read_u8(cpu, cpu->regs.hl));
}

void fgb_sub_a_b(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_sub_u8(cpu, cpu->regs.a, cpu->regs.b);
}

void fgb_sub_a_c(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_sub_u8(cpu, cpu->regs.a, cpu->regs.c);
}

void fgb_sub_a_d(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_sub_u8(cpu, cpu->regs.a, cpu->regs.d);
}

void fgb_sub_a_e(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_sub_u8(cpu, cpu->regs.a, cpu->regs.e);
}

void fgb_sub_a_h(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_sub_u8(cpu, cpu->regs.a, cpu->regs.h);
}

void fgb_sub_a_l(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_sub_u8(cpu, cpu->regs.a, cpu->regs.l);
}

void fgb_sub_a_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_sub_u8(cpu, cpu->regs.a, cpu->regs.a);
}

void fgb_sub_a_p_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_sub_u8(cpu, cpu->regs.a, fgb_mmu_read_u8(cpu, cpu->regs.hl));
}

void fgb_sbc_a_b(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_sbc_u8(cpu, cpu->regs.a, cpu->regs.b);
}

void fgb_sbc_a_c(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_sbc_u8(cpu, cpu->regs.a, cpu->regs.c);
}

void fgb_sbc_a_d(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_sbc_u8(cpu, cpu->regs.a, cpu->regs.d);
}

void fgb_sbc_a_e(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_sbc_u8(cpu, cpu->regs.a, cpu->regs.e);
}

void fgb_sbc_a_h(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_sbc_u8(cpu, cpu->regs.a, cpu->regs.h);
}

void fgb_sbc_a_l(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_sbc_u8(cpu, cpu->regs.a, cpu->regs.l);
}

void fgb_sbc_a_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_sbc_u8(cpu, cpu->regs.a, cpu->regs.a);
}

void fgb_sbc_a_p_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_sbc_u8(cpu, cpu->regs.a, fgb_mmu_read_u8(cpu, cpu->regs.hl));
}

void fgb_and_a_b(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_and_u8(cpu, cpu->regs.a, cpu->regs.b);
}

void fgb_and_a_c(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_and_u8(cpu, cpu->regs.a, cpu->regs.c);
}

void fgb_and_a_d(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_and_u8(cpu, cpu->regs.a, cpu->regs.d);
}

void fgb_and_a_e(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_and_u8(cpu, cpu->regs.a, cpu->regs.e);
}

void fgb_and_a_h(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_and_u8(cpu, cpu->regs.a, cpu->regs.h);
}

void fgb_and_a_l(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_and_u8(cpu, cpu->regs.a, cpu->regs.l);
}

void fgb_and_a_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_and_u8(cpu, cpu->regs.a, cpu->regs.a);
}

void fgb_and_a_p_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_and_u8(cpu, cpu->regs.a, fgb_mmu_read_u8(cpu, cpu->regs.hl));
}

void fgb_xor_a_b(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_xor_u8(cpu, cpu->regs.a, cpu->regs.b);
}

void fgb_xor_a_c(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_xor_u8(cpu, cpu->regs.a, cpu->regs.c);
}

void fgb_xor_a_d(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_xor_u8(cpu, cpu->regs.a, cpu->regs.d);
}

void fgb_xor_a_e(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_xor_u8(cpu, cpu->regs.a, cpu->regs.e);
}

void fgb_xor_a_h(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_xor_u8(cpu, cpu->regs.a, cpu->regs.h);
}

void fgb_xor_a_l(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_xor_u8(cpu, cpu->regs.a, cpu->regs.l);
}

void fgb_xor_a_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_xor_u8(cpu, cpu->regs.a, cpu->regs.a);
}

void fgb_xor_a_p_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_xor_u8(cpu, cpu->regs.a, fgb_mmu_read_u8(cpu, cpu->regs.hl));
}

void fgb_or_a_b(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_or_u8(cpu, cpu->regs.a, cpu->regs.b);
}

void fgb_or_a_c(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_or_u8(cpu, cpu->regs.a, cpu->regs.c);
}

void fgb_or_a_d(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_or_u8(cpu, cpu->regs.a, cpu->regs.d);
}

void fgb_or_a_e(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_or_u8(cpu, cpu->regs.a, cpu->regs.e);
}

void fgb_or_a_h(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_or_u8(cpu, cpu->regs.a, cpu->regs.h);
}

void fgb_or_a_l(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_or_u8(cpu, cpu->regs.a, cpu->regs.l);
}

void fgb_or_a_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_or_u8(cpu, cpu->regs.a, cpu->regs.a);
}

void fgb_or_a_p_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_or_u8(cpu, cpu->regs.a, fgb_mmu_read_u8(cpu, cpu->regs.hl));
}
