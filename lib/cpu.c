#include "cpu.h"
#include "instruction.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <ulog.h>


static uint8_t fgb_cpu_fetch(fgb_cpu* cpu);
static uint16_t fgb_cpu_fetch_u16(fgb_cpu* cpu);
static void fgb_cpu_handle_interrupts(fgb_cpu* cpu);
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


fgb_cpu* fgb_cpu_create(fgb_cart* cart) {
    fgb_cpu* cpu = malloc(sizeof(fgb_cpu));
    if (!cpu) {
        log_error("Failed to allocate CPU");
        return NULL;
    }

    ulog_set_quiet(true);

    memset(cpu, 0, sizeof(fgb_cpu));

    fgb_timer_init(&cpu->timer, cpu);
    fgb_io_init(&cpu->io, cpu);
    fgb_mmu_init(&cpu->mmu, cart, cpu, NULL);
    fgb_cpu_reset(cpu);

    return cpu;
}

fgb_cpu* fgb_cpu_create_with(fgb_cart* cart, const fgb_mmu_ops* mmu_ops) {
    fgb_cpu* cpu = malloc(sizeof(fgb_cpu));
    if (!cpu) {
        log_error("Failed to allocate CPU");
        return NULL;
    }

    memset(cpu, 0, sizeof(fgb_cpu));

    fgb_timer_init(&cpu->timer, cpu);
    fgb_io_init(&cpu->io, cpu);
    fgb_mmu_init(&cpu->mmu, cart, cpu, mmu_ops);
    fgb_cpu_reset(cpu);

    return cpu;
}

void fgb_cpu_destroy(fgb_cpu* cpu) {
    free(cpu);
}

void fgb_cpu_reset(fgb_cpu* cpu) {
    memset(&cpu->regs, 0, sizeof(cpu->regs));
    cpu->halted = false;

    cpu->regs.pc = 0x0100;
    cpu->regs.sp = 0xFFFE;

    cpu->regs.af = 0x01B0;
    cpu->regs.bc = 0x0013;
    cpu->regs.de = 0x00D8;
    cpu->regs.hl = 0x014D;
    
    for (size_t i = 0; i < sizeof(fgb_init_table) / sizeof(fgb_init_table[0]); i++) {
        fgb_mmu_write(cpu, fgb_init_table[i].addr, fgb_init_table[i].value);
    }
}

void fgb_cpu_step(fgb_cpu* cpu) {
    int cycles = 0;

    while (cycles < FGB_CYCLES_PER_FRAME) {
        cycles += fgb_cpu_execute(cpu);
    }
}

int fgb_cpu_execute(fgb_cpu* cpu) {
    // If the CPU is halted, just return max cycles so
    // the caller isn't blocked
    int cycles = FGB_CYCLES_PER_FRAME;

    if (!cpu->halted) {
        const uint8_t opcode = fgb_cpu_fetch(cpu);
        const fgb_instruction* instruction = fgb_instruction_get(opcode);
        assert(instruction->opcode == opcode);

        uint8_t op8 = 0;
        uint16_t op16 = 0;

        switch (instruction->operand_size) {
        case 0:
            instruction->exec_0(cpu, instruction);
            break;

        case 1:
            op8 = fgb_cpu_fetch(cpu);
            instruction->exec_1(cpu, instruction, op8);
            break;

        case 2:
            op16 = fgb_cpu_fetch_u16(cpu);
            instruction->exec_2(cpu, instruction, op16);
            break;

        default:
            log_error("Invalid operand size: %d", instruction->operand_size);
            cpu->halted = true;
            return FGB_CYCLES_PER_FRAME;
        }

        if (cpu->trace) {
            switch (instruction->operand_size) {
            case 0:
                log_info("0x%04X: %s", cpu->regs.pc, instruction->fmt_0(instruction));
                break;
            case 1:
                log_info("0x%04X: %s", cpu->regs.pc, instruction->fmt_1(instruction, op8));
                break;
            case 2:
                log_info("0x%04X: %s", cpu->regs.pc, instruction->fmt_2(instruction, op16));
                break;
            default:
                log_info("UNKNOWN");
                break;
            }
        }

        cycles = instruction->cycles != 255 ? instruction->cycles : fgb_instruction_get_cb_cycles(op8);
    }

    // Update the timer
    for (int i = 0; i < cycles; i++) {
        fgb_timer_tick(&cpu->timer);
    }

    // Handle interrupts
    fgb_cpu_handle_interrupts(cpu);

    return cycles;
}

void fgb_cpu_request_interrupt(fgb_cpu* cpu, enum fgb_cpu_interrupt interrupt) {
    cpu->interrupt.flags |= (uint8_t)interrupt;
}

void fgb_cpu_write(fgb_cpu* cpu, uint16_t addr, uint8_t value) {
    switch (addr) {
    case 0xFFFF:
        cpu->interrupt.enable = value;
        return;

    case 0xFF0F:
        cpu->interrupt.flags = value;
        break;

    default:
        log_warn("Unknown address for CPU write: 0x%04X", addr);
        break;
    }
}

uint8_t fgb_cpu_read(const fgb_cpu* cpu, uint16_t addr) {
    switch (addr) {
    case 0xFFFF:
        return cpu->interrupt.enable;

    case 0xFF0F:
        return cpu->interrupt.flags;

    default:
        log_warn("Unknown address for CPU read: 0x%04X", addr);
        return 0xAA;
    }
}

uint8_t fgb_cpu_fetch(fgb_cpu* cpu) {
    return fgb_mmu_read_u8(cpu, cpu->regs.pc++);
}

uint16_t fgb_cpu_fetch_u16(fgb_cpu* cpu) {
    const uint16_t value = fgb_mmu_read_u16(cpu, cpu->regs.pc);
    cpu->regs.pc += 2;
    return value;
}

static inline void fgb_call(fgb_cpu* cpu, uint16_t dest);

static const uint16_t fgb_interrupt_vector[] = {
    [IRQ_VBLANK] = 0x0040,
    [IRQ_LCD]    = 0x0048,
    [IRQ_TIMER]  = 0x0050,
    [IRQ_SERIAL] = 0x0058,
    [IRQ_JOYPAD] = 0x0060,
};

static inline bool fgb_cpu_handle_irq(fgb_cpu* cpu, enum fgb_cpu_interrupt irq) {
    if (!(cpu->interrupt.enable & cpu->interrupt.flags & irq)) {
        return false;
    }

    // Interrupt is only serviced if IME is set
    if (cpu->ime) {
        fgb_call(cpu, fgb_interrupt_vector[irq]);
        cpu->interrupt.flags &= ~irq; // Clear the interrupt flag
    }

    cpu->ime = false; // Disable interrupts after handling
    cpu->halted = false; // Clear halted state

    return true;
}

void fgb_cpu_handle_interrupts(fgb_cpu* cpu) {
    if (fgb_cpu_handle_irq(cpu, IRQ_VBLANK)) return;
    if (fgb_cpu_handle_irq(cpu, IRQ_LCD)) return;
    if (fgb_cpu_handle_irq(cpu, IRQ_TIMER)) return;
    if (fgb_cpu_handle_irq(cpu, IRQ_SERIAL)) return;
    fgb_cpu_handle_irq(cpu, IRQ_JOYPAD);
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

static inline void fgb_call(fgb_cpu* cpu, uint16_t dest) {
    fgb_mmu_write(cpu, --cpu->regs.sp, (cpu->regs.pc >> 8) & 0xFF);
    fgb_mmu_write(cpu, --cpu->regs.sp, (cpu->regs.pc >> 0) & 0xFF);
    cpu->regs.pc = dest;
}

static inline void fgb_ret_(fgb_cpu* cpu) {
    cpu->regs.pc = fgb_mmu_read_u16(cpu, cpu->regs.sp);
    cpu->regs.sp += 2;
}

void fgb_nop(fgb_cpu* cpu, const fgb_instruction* ins) {
    (void)cpu;
    (void)ins;
}

void fgb_stop(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    // Just gonna interpret STOP as a HALT because I cba
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

void fgb_add_sp_imm(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    const int sp = cpu->regs.sp + (int8_t)operand;
    cpu->regs.flags.c = (cpu->regs.sp & 0xFF) + (operand & 0xFF) > 0xFF;
    cpu->regs.flags.h = (cpu->regs.sp & 0xF) + (operand & 0xF) > 0xF;
    cpu->regs.flags.n = 0;
    cpu->regs.flags.z = 0;
    cpu->regs.sp = sp & 0xFFFF;
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

void fgb_cp_a_b(fgb_cpu* cpu, const fgb_instruction* ins) {
    (void)fgb_sub_u8(cpu, cpu->regs.a, cpu->regs.b);
}

void fgb_cp_a_c(fgb_cpu* cpu, const fgb_instruction* ins) {
    (void)fgb_sub_u8(cpu, cpu->regs.a, cpu->regs.c);
}

void fgb_cp_a_d(fgb_cpu* cpu, const fgb_instruction* ins) {
    (void)fgb_sub_u8(cpu, cpu->regs.a, cpu->regs.d);
}

void fgb_cp_a_e(fgb_cpu* cpu, const fgb_instruction* ins) {
    (void)fgb_sub_u8(cpu, cpu->regs.a, cpu->regs.e);
}

void fgb_cp_a_h(fgb_cpu* cpu, const fgb_instruction* ins) {
    (void)fgb_sub_u8(cpu, cpu->regs.a, cpu->regs.h);
}

void fgb_cp_a_l(fgb_cpu* cpu, const fgb_instruction* ins) {
    (void)fgb_sub_u8(cpu, cpu->regs.a, cpu->regs.l);
}

void fgb_cp_a_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    (void)fgb_sub_u8(cpu, cpu->regs.a, cpu->regs.a);
}

void fgb_cp_a_p_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    (void)fgb_sub_u8(cpu, cpu->regs.a, fgb_mmu_read_u8(cpu, cpu->regs.hl));
}

void fgb_add_a_imm(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    cpu->regs.a = fgb_add_u8(cpu, cpu->regs.a, operand);
}

void fgb_adc_a_imm(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    cpu->regs.a = fgb_adc_u8(cpu, cpu->regs.a, operand);
}

void fgb_sub_a_imm(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    cpu->regs.a = fgb_sub_u8(cpu, cpu->regs.a, operand);
}

void fgb_sbc_a_imm(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    cpu->regs.a = fgb_sbc_u8(cpu, cpu->regs.a, operand);
}

void fgb_and_a_imm(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    cpu->regs.a = fgb_and_u8(cpu, cpu->regs.a, operand);
}

void fgb_xor_a_imm(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    cpu->regs.a = fgb_xor_u8(cpu, cpu->regs.a, operand);
}

void fgb_or_a_imm(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    cpu->regs.a = fgb_or_u8(cpu, cpu->regs.a, operand);
}

void fgb_cp_a_imm(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    (void)fgb_sub_u8(cpu, cpu->regs.a, operand);
}

void fgb_push_bc(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_mmu_write(cpu, --cpu->regs.sp, cpu->regs.b);
    fgb_mmu_write(cpu, --cpu->regs.sp, cpu->regs.c);
}

void fgb_push_de(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_mmu_write(cpu, --cpu->regs.sp, cpu->regs.d);
    fgb_mmu_write(cpu, --cpu->regs.sp, cpu->regs.e);
}

void fgb_push_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_mmu_write(cpu, --cpu->regs.sp, cpu->regs.h);
    fgb_mmu_write(cpu, --cpu->regs.sp, cpu->regs.l);
}

void fgb_push_af(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_mmu_write(cpu, --cpu->regs.sp, cpu->regs.a);
    fgb_mmu_write(cpu, --cpu->regs.sp, cpu->regs.f);
}

void fgb_pop_bc(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.c = fgb_mmu_read_u8(cpu, cpu->regs.sp++);
    cpu->regs.b = fgb_mmu_read_u8(cpu, cpu->regs.sp++);
}

void fgb_pop_de(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.e = fgb_mmu_read_u8(cpu, cpu->regs.sp++);
    cpu->regs.d = fgb_mmu_read_u8(cpu, cpu->regs.sp++);
}

void fgb_pop_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.l = fgb_mmu_read_u8(cpu, cpu->regs.sp++);
    cpu->regs.h = fgb_mmu_read_u8(cpu, cpu->regs.sp++);
}

void fgb_pop_af(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.f = fgb_mmu_read_u8(cpu, cpu->regs.sp++) & 0xF0;
    cpu->regs.a = fgb_mmu_read_u8(cpu, cpu->regs.sp++);
}

void fgb_ld_p_imm_a(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    fgb_mmu_write(cpu, 0xFF00 + operand, cpu->regs.a);
}

void fgb_ld_a_p_imm(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    cpu->regs.a = fgb_mmu_read_u8(cpu, 0xFF00 + operand);
}

void fgb_ld_p_c_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_mmu_write(cpu, 0xFF00 + cpu->regs.c, cpu->regs.a);
}

void fgb_ld_a_p_c(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_mmu_read_u8(cpu, 0xFF00 + cpu->regs.c);
}

void fgb_ld_p_imm16_a(fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand) {
    fgb_mmu_write(cpu, operand, cpu->regs.a);
}

void fgb_ld_a_p_imm16(fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand) {
    cpu->regs.a = fgb_mmu_read_u8(cpu, operand);
}

void fgb_ld_hl_sp_imm(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t operand) {
    const int hl = cpu->regs.sp + (int8_t)operand;
    cpu->regs.flags.c = (cpu->regs.sp & 0xFF) + (operand & 0xFF) > 0xFF;
    cpu->regs.flags.h = (cpu->regs.sp & 0xF) + (operand & 0xF) > 0xF;
    cpu->regs.flags.n = 0;
    cpu->regs.flags.z = 0;
    cpu->regs.hl = hl & 0xFFFF;
}

void fgb_ld_sp_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.sp = cpu->regs.hl;
}

void fgb_jp_imm16(fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand) {
    cpu->regs.pc = operand;
}

void fgb_jp_z_imm16(fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand) {
    if (cpu->regs.flags.z) {
        cpu->regs.pc = operand;
    }
}

void fgb_jp_c_imm16(fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand) {
    if (cpu->regs.flags.c) {
        cpu->regs.pc = operand;
    }
}

void fgb_jp_nz_imm16(fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand) {
    if (!cpu->regs.flags.z) {
        cpu->regs.pc = operand;
    }
}

void fgb_jp_nc_imm16(fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand) {
    if (!cpu->regs.flags.c) {
        cpu->regs.pc = operand;
    }
}

void fgb_jp_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.pc = cpu->regs.hl;
}

void fgb_call_imm16(fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand) {
    fgb_call(cpu, operand);
}

void fgb_call_z_imm16(fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand) {
    if (cpu->regs.flags.z) {
        fgb_call(cpu, operand);
    }
}

void fgb_call_c_imm16(fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand) {
    if (cpu->regs.flags.c) {
        fgb_call(cpu, operand);
    }
}

void fgb_call_nz_imm16(fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand) {
    if (!cpu->regs.flags.z) {
        fgb_call(cpu, operand);
    }
}

void fgb_call_nc_imm16(fgb_cpu* cpu, const fgb_instruction* ins, uint16_t operand) {
    if (!cpu->regs.flags.c) {
        fgb_call(cpu, operand);
    }
}

void fgb_ret(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_ret_(cpu);
}

void fgb_ret_z(fgb_cpu* cpu, const fgb_instruction* ins) {
    if (cpu->regs.flags.z) {
        fgb_ret_(cpu);
    }
}

void fgb_ret_c(fgb_cpu* cpu, const fgb_instruction* ins) {
    if (cpu->regs.flags.c) {
        fgb_ret_(cpu);
    }
}

void fgb_ret_nz(fgb_cpu* cpu, const fgb_instruction* ins) {
    if (!cpu->regs.flags.z) {
        fgb_ret_(cpu);
    }
}

void fgb_ret_nc(fgb_cpu* cpu, const fgb_instruction* ins) {
    if (!cpu->regs.flags.c) {
        fgb_ret_(cpu);
    }
}

void fgb_ei(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->ime = true;
}

void fgb_di(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->ime = false;
}

void fgb_reti(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_ret_(cpu);
    cpu->ime = true;
}

void fgb_rst_0(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_call(cpu, 0x0000);
}

void fgb_rst_1(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_call(cpu, 0x0008);
}

void fgb_rst_2(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_call(cpu, 0x0010);
}

void fgb_rst_3(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_call(cpu, 0x0018);
}

void fgb_rst_4(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_call(cpu, 0x0020);
}

void fgb_rst_5(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_call(cpu, 0x0028);
}

void fgb_rst_6(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_call(cpu, 0x0030);
}

void fgb_rst_7(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_call(cpu, 0x0038);
}

#define fgb_cb_bit_index(opcode) (((opcode) >> 3) & 7)

static inline uint8_t fgb_cb_rlc(fgb_cpu* cpu, uint8_t value) {
    cpu->regs.flags.c = value >> 7;
    value <<= 1;
    value |= cpu->regs.flags.c;
    cpu->regs.flags.z = value == 0;
    cpu->regs.flags.n = 0;
    cpu->regs.flags.h = 0;
    return value;
}

static inline uint8_t fgb_cb_rrc(fgb_cpu* cpu, uint8_t value) {
    cpu->regs.flags.c = value & 1;
    value >>= 1;
    value |= cpu->regs.flags.c << 7;
    cpu->regs.flags.z = value == 0;
    cpu->regs.flags.n = 0;
    cpu->regs.flags.h = 0;
    return value;
}

static inline uint8_t fgb_cb_rl(fgb_cpu* cpu, uint8_t value) {
    const uint8_t c = cpu->regs.flags.c;
    cpu->regs.flags.c = value >> 7;
    value <<= 1;
    value |= c;
    cpu->regs.flags.z = value == 0;
    cpu->regs.flags.n = 0;
    cpu->regs.flags.h = 0;
    return value;
}

static inline uint8_t fgb_cb_rr(fgb_cpu* cpu, uint8_t value) {
    const uint8_t c = cpu->regs.flags.c;
    cpu->regs.flags.c = value & 1;
    value >>= 1;
    value |= c << 7;
    cpu->regs.flags.z = value == 0;
    cpu->regs.flags.n = 0;
    cpu->regs.flags.h = 0;
    return value;
}

static inline uint8_t fgb_cb_sla(fgb_cpu* cpu, uint8_t value) {
    cpu->regs.flags.c = value >> 7;
    value <<= 1;
    cpu->regs.flags.z = value == 0;
    cpu->regs.flags.n = 0;
    cpu->regs.flags.h = 0;
    return value;
}

static inline uint8_t fgb_cb_sra(fgb_cpu* cpu, uint8_t value) {
    cpu->regs.flags.c = value & 1;
    value = (int8_t)value >> 1;
    cpu->regs.flags.z = value == 0;
    cpu->regs.flags.n = 0;
    cpu->regs.flags.h = 0;
    return value;
}

static inline uint8_t fgb_cb_swap(fgb_cpu* cpu, uint8_t value) {
    value = (value & 0x0F) << 4 | (value & 0xF0) >> 4;
    cpu->regs.flags.z = value == 0;
    cpu->regs.flags.n = 0;
    cpu->regs.flags.h = 0;
    cpu->regs.flags.c = 0;
    return value;
}

static inline uint8_t fgb_cb_srl(fgb_cpu* cpu, uint8_t value) {
    cpu->regs.flags.c = value & 1;
    value >>= 1;
    cpu->regs.flags.z = value == 0;
    cpu->regs.flags.n = 0;
    cpu->regs.flags.h = 0;
    return value;
}

static inline void fgb_cb_bit(fgb_cpu* cpu, uint8_t bit, uint8_t value) {
    cpu->regs.flags.z = ((value >> bit) & 1) == 0;
    cpu->regs.flags.n = 0;
    cpu->regs.flags.h = 1;
}

#define fgb_cb_res(val, bit) ((val) & ~(1 << (bit)))
#define fgb_cb_set(val, bit) ((val) | (1 << (bit)))

#define fgb_cb_cases(start) \
    case start + 0x00: \
    case start + 0x08: \
    case start + 0x10: \
    case start + 0x18: \
    case start + 0x20: \
    case start + 0x28: \
    case start + 0x30: \
    case start + 0x38

void fgb_cb(fgb_cpu* cpu, const fgb_instruction* ins, uint8_t opcode) {
    switch (opcode) {
        // RLC reg8
    case 0x00: cpu->regs.b = fgb_cb_rlc(cpu, cpu->regs.b); break;
    case 0x01: cpu->regs.c = fgb_cb_rlc(cpu, cpu->regs.c); break;
    case 0x02: cpu->regs.d = fgb_cb_rlc(cpu, cpu->regs.d); break;
    case 0x03: cpu->regs.e = fgb_cb_rlc(cpu, cpu->regs.e); break;
    case 0x04: cpu->regs.h = fgb_cb_rlc(cpu, cpu->regs.h); break;
    case 0x05: cpu->regs.l = fgb_cb_rlc(cpu, cpu->regs.l); break;
    case 0x06: fgb_mmu_write(cpu, cpu->regs.hl, fgb_cb_rlc(cpu, fgb_mmu_read_u8(cpu, cpu->regs.hl))); break;
    case 0x07: cpu->regs.a = fgb_cb_rlc(cpu, cpu->regs.a); break;

        // RRC reg8
    case 0x08: cpu->regs.b = fgb_cb_rrc(cpu, cpu->regs.b); break;
    case 0x09: cpu->regs.c = fgb_cb_rrc(cpu, cpu->regs.c); break;
    case 0x0A: cpu->regs.d = fgb_cb_rrc(cpu, cpu->regs.d); break;
    case 0x0B: cpu->regs.e = fgb_cb_rrc(cpu, cpu->regs.e); break;
    case 0x0C: cpu->regs.h = fgb_cb_rrc(cpu, cpu->regs.h); break;
    case 0x0D: cpu->regs.l = fgb_cb_rrc(cpu, cpu->regs.l); break;
    case 0x0E: fgb_mmu_write(cpu, cpu->regs.hl, fgb_cb_rrc(cpu, fgb_mmu_read_u8(cpu, cpu->regs.hl))); break;
    case 0x0F: cpu->regs.a = fgb_cb_rrc(cpu, cpu->regs.a); break;

        // RL reg8
    case 0x10: cpu->regs.b = fgb_cb_rl(cpu, cpu->regs.b); break;
    case 0x11: cpu->regs.c = fgb_cb_rl(cpu, cpu->regs.c); break;
    case 0x12: cpu->regs.d = fgb_cb_rl(cpu, cpu->regs.d); break;
    case 0x13: cpu->regs.e = fgb_cb_rl(cpu, cpu->regs.e); break;
    case 0x14: cpu->regs.h = fgb_cb_rl(cpu, cpu->regs.h); break;
    case 0x15: cpu->regs.l = fgb_cb_rl(cpu, cpu->regs.l); break;
    case 0x16: fgb_mmu_write(cpu, cpu->regs.hl, fgb_cb_rl(cpu, fgb_mmu_read_u8(cpu, cpu->regs.hl))); break;
    case 0x17: cpu->regs.a = fgb_cb_rl(cpu, cpu->regs.a); break;

        // RR reg8
    case 0x18: cpu->regs.b = fgb_cb_rr(cpu, cpu->regs.b); break;
    case 0x19: cpu->regs.c = fgb_cb_rr(cpu, cpu->regs.c); break;
    case 0x1A: cpu->regs.d = fgb_cb_rr(cpu, cpu->regs.d); break;
    case 0x1B: cpu->regs.e = fgb_cb_rr(cpu, cpu->regs.e); break;
    case 0x1C: cpu->regs.h = fgb_cb_rr(cpu, cpu->regs.h); break;
    case 0x1D: cpu->regs.l = fgb_cb_rr(cpu, cpu->regs.l); break;
    case 0x1E: fgb_mmu_write(cpu, cpu->regs.hl, fgb_cb_rr(cpu, fgb_mmu_read_u8(cpu, cpu->regs.hl))); break;
    case 0x1F: cpu->regs.a = fgb_cb_rr(cpu, cpu->regs.a); break;

        // SLA reg8
    case 0x20: cpu->regs.b = fgb_cb_sla(cpu, cpu->regs.b); break;
    case 0x21: cpu->regs.c = fgb_cb_sla(cpu, cpu->regs.c); break;
    case 0x22: cpu->regs.d = fgb_cb_sla(cpu, cpu->regs.d); break;
    case 0x23: cpu->regs.e = fgb_cb_sla(cpu, cpu->regs.e); break;
    case 0x24: cpu->regs.h = fgb_cb_sla(cpu, cpu->regs.h); break;
    case 0x25: cpu->regs.l = fgb_cb_sla(cpu, cpu->regs.l); break;
    case 0x26: fgb_mmu_write(cpu, cpu->regs.hl, fgb_cb_sla(cpu, fgb_mmu_read_u8(cpu, cpu->regs.hl))); break;
    case 0x27: cpu->regs.a = fgb_cb_sla(cpu, cpu->regs.a); break;

        // SRA reg8
    case 0x28: cpu->regs.b = fgb_cb_sra(cpu, cpu->regs.b); break;
    case 0x29: cpu->regs.c = fgb_cb_sra(cpu, cpu->regs.c); break;
    case 0x2A: cpu->regs.d = fgb_cb_sra(cpu, cpu->regs.d); break;
    case 0x2B: cpu->regs.e = fgb_cb_sra(cpu, cpu->regs.e); break;
    case 0x2C: cpu->regs.h = fgb_cb_sra(cpu, cpu->regs.h); break;
    case 0x2D: cpu->regs.l = fgb_cb_sra(cpu, cpu->regs.l); break;
    case 0x2E: fgb_mmu_write(cpu, cpu->regs.hl, fgb_cb_sra(cpu, fgb_mmu_read_u8(cpu, cpu->regs.hl))); break;
    case 0x2F: cpu->regs.a = fgb_cb_sra(cpu, cpu->regs.a); break;

        // SWAP reg8
    case 0x30: cpu->regs.b = fgb_cb_swap(cpu, cpu->regs.b); break;
    case 0x31: cpu->regs.c = fgb_cb_swap(cpu, cpu->regs.c); break;
    case 0x32: cpu->regs.d = fgb_cb_swap(cpu, cpu->regs.d); break;
    case 0x33: cpu->regs.e = fgb_cb_swap(cpu, cpu->regs.e); break;
    case 0x34: cpu->regs.h = fgb_cb_swap(cpu, cpu->regs.h); break;
    case 0x35: cpu->regs.l = fgb_cb_swap(cpu, cpu->regs.l); break;
    case 0x36: fgb_mmu_write(cpu, cpu->regs.hl, fgb_cb_swap(cpu, fgb_mmu_read_u8(cpu, cpu->regs.hl))); break;
    case 0x37: cpu->regs.a = fgb_cb_swap(cpu, cpu->regs.a); break;

        // SRL reg8
    case 0x38: cpu->regs.b = fgb_cb_srl(cpu, cpu->regs.b); break;
    case 0x39: cpu->regs.c = fgb_cb_srl(cpu, cpu->regs.c); break;
    case 0x3A: cpu->regs.d = fgb_cb_srl(cpu, cpu->regs.d); break;
    case 0x3B: cpu->regs.e = fgb_cb_srl(cpu, cpu->regs.e); break;
    case 0x3C: cpu->regs.h = fgb_cb_srl(cpu, cpu->regs.h); break;
    case 0x3D: cpu->regs.l = fgb_cb_srl(cpu, cpu->regs.l); break;
    case 0x3E: fgb_mmu_write(cpu, cpu->regs.hl, fgb_cb_srl(cpu, fgb_mmu_read_u8(cpu, cpu->regs.hl))); break;
    case 0x3F: cpu->regs.a = fgb_cb_srl(cpu, cpu->regs.a); break;

        // BIT n, reg8
    fgb_cb_cases(0x40): fgb_cb_bit(cpu, fgb_cb_bit_index(opcode), cpu->regs.b); break;
    fgb_cb_cases(0x41): fgb_cb_bit(cpu, fgb_cb_bit_index(opcode), cpu->regs.c); break;
    fgb_cb_cases(0x42): fgb_cb_bit(cpu, fgb_cb_bit_index(opcode), cpu->regs.d); break;
    fgb_cb_cases(0x43): fgb_cb_bit(cpu, fgb_cb_bit_index(opcode), cpu->regs.e); break;
    fgb_cb_cases(0x44): fgb_cb_bit(cpu, fgb_cb_bit_index(opcode), cpu->regs.h); break;
    fgb_cb_cases(0x45): fgb_cb_bit(cpu, fgb_cb_bit_index(opcode), cpu->regs.l); break;
    fgb_cb_cases(0x46): fgb_cb_bit(cpu, fgb_cb_bit_index(opcode), fgb_mmu_read_u8(cpu, cpu->regs.hl)); break;
    fgb_cb_cases(0x47): fgb_cb_bit(cpu, fgb_cb_bit_index(opcode), cpu->regs.a); break;

        // RES n, reg8
    fgb_cb_cases(0x80): cpu->regs.b = fgb_cb_res(cpu->regs.b, fgb_cb_bit_index(opcode)); break;
    fgb_cb_cases(0x81): cpu->regs.c = fgb_cb_res(cpu->regs.c, fgb_cb_bit_index(opcode)); break;
    fgb_cb_cases(0x82): cpu->regs.d = fgb_cb_res(cpu->regs.d, fgb_cb_bit_index(opcode)); break;
    fgb_cb_cases(0x83): cpu->regs.e = fgb_cb_res(cpu->regs.e, fgb_cb_bit_index(opcode)); break;
    fgb_cb_cases(0x84): cpu->regs.h = fgb_cb_res(cpu->regs.h, fgb_cb_bit_index(opcode)); break;
    fgb_cb_cases(0x85): cpu->regs.l = fgb_cb_res(cpu->regs.l, fgb_cb_bit_index(opcode)); break;
    fgb_cb_cases(0x86): fgb_mmu_write(cpu, cpu->regs.hl, fgb_cb_res(fgb_mmu_read_u8(cpu, cpu->regs.hl), fgb_cb_bit_index(opcode))); break;
    fgb_cb_cases(0x87): cpu->regs.a = fgb_cb_res(cpu->regs.a, fgb_cb_bit_index(opcode)); break;

        // SET n, reg8
    fgb_cb_cases(0xC0): cpu->regs.b = fgb_cb_set(cpu->regs.b, fgb_cb_bit_index(opcode)); break;
    fgb_cb_cases(0xC1): cpu->regs.c = fgb_cb_set(cpu->regs.c, fgb_cb_bit_index(opcode)); break;
    fgb_cb_cases(0xC2): cpu->regs.d = fgb_cb_set(cpu->regs.d, fgb_cb_bit_index(opcode)); break;
    fgb_cb_cases(0xC3): cpu->regs.e = fgb_cb_set(cpu->regs.e, fgb_cb_bit_index(opcode)); break;
    fgb_cb_cases(0xC4): cpu->regs.h = fgb_cb_set(cpu->regs.h, fgb_cb_bit_index(opcode)); break;
    fgb_cb_cases(0xC5): cpu->regs.l = fgb_cb_set(cpu->regs.l, fgb_cb_bit_index(opcode)); break;
    fgb_cb_cases(0xC6): fgb_mmu_write(cpu, cpu->regs.hl, fgb_cb_set(fgb_mmu_read_u8(cpu, cpu->regs.hl), fgb_cb_bit_index(opcode))); break;
    fgb_cb_cases(0xC7): cpu->regs.a = fgb_cb_set(cpu->regs.a, fgb_cb_bit_index(opcode)); break;

        // Shouldn't ever happen but whatever
    default: log_warn("Unknown instruction: CB %02X", opcode); break;
    }
}
