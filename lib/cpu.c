#include "cpu.h"
#include "instruction.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <ulog.h>


// These functions automatically tick components
static const fgb_instruction* fgb_cpu_fetch_instruction(fgb_cpu* cpu);
static uint8_t fgb_cpu_fetch(fgb_cpu* cpu);
static uint16_t fgb_cpu_fetch_u16(fgb_cpu* cpu);
static uint8_t fgb_cpu_read_u8(fgb_cpu* cpu, uint16_t addr);
static uint16_t fgb_cpu_read_u16(fgb_cpu* cpu, uint16_t addr);
static void fgb_cpu_write_u8(fgb_cpu* cpu, uint16_t addr, uint8_t value);
static void fgb_cpu_write_u16(fgb_cpu* cpu, uint16_t addr, uint16_t value);
static void fgb_cpu_run_instruction(fgb_cpu* cpu, const fgb_instruction* instr);

static void fgb_cpu_handle_interrupts(fgb_cpu* cpu);
#define fgb_mmu_write(cpu, addr, value) (cpu)->mmu.write_u8(&(cpu)->mmu, addr, value)
#define fgb_mmu_read_u8(cpu, addr) (cpu)->mmu.read_u8(&(cpu)->mmu, addr)
#define fgb_mmu_read_u16(cpu, addr) (cpu)->mmu.read_u16(&(cpu)->mmu, addr)

#define FGB_BP_ADDR_NONE 0xFFFF

#define set_flag(flag, value) fgb_cpu_set_flag(cpu, CPU_FLAG_##flag, value)
#define toggle_flag(flag) fgb_cpu_toggle_flag(cpu, CPU_FLAG_##flag)
#define clear_flag(flag) fgb_cpu_clear_flag(cpu, CPU_FLAG_##flag)
#define get_flag(flag) fgb_cpu_get_flag(cpu, CPU_FLAG_##flag)

struct fgb_init_value {
    uint16_t addr;
    uint8_t value;
};
static const struct fgb_init_value fgb_init_table[] = {
    { 0xFF00, 0xCF }, // P1
    { 0xFF01, 0x00 }, // SB
    { 0xFF02, 0x7E }, // SC
};


fgb_cpu* fgb_cpu_create_ex(fgb_cart* cart, fgb_ppu* ppu, fgb_apu* apu, fgb_model model, const fgb_mmu_ops* mmu_ops) {
    fgb_cpu* cpu = malloc(sizeof(fgb_cpu));
    if (!cpu) {
        log_error("Failed to allocate CPU");
        return NULL;
    }

    ulog_set_quiet(true);

    memset(cpu, 0, sizeof(fgb_cpu));

    cpu->apu = apu;
    cpu->ppu = ppu;
    cpu->model = model;
    fgb_ppu_set_cpu(ppu, cpu);

    fgb_timer_init(&cpu->timer, cpu);
    fgb_io_init(&cpu->io, cpu);
    fgb_mmu_init(&cpu->mmu, cart, cpu, mmu_ops);
    cpu->mmu.model = model;
    fgb_cpu_reset(cpu);
    fgb_ppu_reset(ppu);
    fgb_apu_reset(apu);

    return cpu;
}

fgb_cpu* fgb_cpu_create(fgb_cart* cart, fgb_ppu* ppu, fgb_apu* apu) {
    return fgb_cpu_create_ex(cart, ppu, apu, FGB_MODEL_DMG, NULL);
}

fgb_cpu* fgb_cpu_create_with(fgb_cart* cart, fgb_ppu* ppu, fgb_apu* apu, const fgb_mmu_ops* mmu_ops) {
    return fgb_cpu_create_ex(cart, ppu, apu, FGB_MODEL_DMG, mmu_ops);
}

void fgb_cpu_destroy(fgb_cpu* cpu) {
    free(cpu);
}

void fgb_cpu_tick(fgb_cpu *cpu) {
    cpu->cycles_this_frame++;
    cpu->total_cycles++;

    if (cpu->test_mode) {
        // Don't tick peripherals in test mode
        return;
    }

    fgb_timer_tick(&cpu->timer);
    fgb_ppu_tick(cpu->ppu);
    fgb_apu_tick(cpu->apu);
    fgb_cart_tick(cpu->mmu.cart);

    // TODO:
    // - Maybe extract DMA handling from PPU tick?
    // - Maybe tick serial?
}

void fgb_cpu_m_tick(fgb_cpu *cpu) {
    fgb_cpu_tick(cpu);
    fgb_cpu_tick(cpu);
    fgb_cpu_tick(cpu);
    fgb_cpu_tick(cpu);
}

void fgb_cpu_reset(fgb_cpu* cpu) {
    memset(&cpu->regs, 0, sizeof(cpu->regs));
    cpu->mode = CPU_MODE_NORMAL;
    cpu->total_cycles = 0;
    cpu->cycles_this_frame = 0;

    cpu->regs.pc = 0x0000; // Starting at $0000 to run Bootrom
    cpu->regs.sp = 0xFFFE;

    cpu->regs.af = 0x01B0;
    cpu->regs.bc = 0x0013;
    cpu->regs.de = 0x00D8;
    cpu->regs.hl = 0x014D;

    cpu->interrupt.flags = 0xE1;
    cpu->interrupt.enable = 0x00;

    cpu->mmu.bootrom_mapped = true;
    
    for (size_t i = 0; i < sizeof(fgb_init_table) / sizeof(fgb_init_table[0]); i++) {
        fgb_mmu_write(cpu, fgb_init_table[i].addr, fgb_init_table[i].value);
    }

    for (int i = 0; i < FGB_CPU_MAX_BREAKPOINTS; i++) {
        cpu->breakpoints[i] = FGB_BP_ADDR_NONE;
    }

    fgb_timer_reset(&cpu->timer);
}

void fgb_cpu_run_frame(fgb_cpu* cpu) {
    if (cpu->debugging && !cpu->do_step) {
        return;
    }

    cpu->cycles_this_frame = 0;

    while (cpu->cycles_this_frame < FGB_CYCLES_PER_FRAME) {
        fgb_cpu_step(cpu);

        for (size_t i = 0; i < FGB_CPU_MAX_BREAKPOINTS; i++) {
            if (cpu->breakpoints[i] != FGB_BP_ADDR_NONE && cpu->regs.pc == cpu->breakpoints[i]) {
                log_info("Breakpoint hit at 0x%04X", cpu->regs.pc);
                cpu->debugging = true;
                cpu->do_step = false;

                if (cpu->bp_callback) {
                    cpu->bp_callback(cpu, i, cpu->regs.pc);
                }
            }
        }

        if (cpu->debugging) {
            cpu->do_step = false; // Reset step flag after stepping

            if (cpu->step_callback) {
                cpu->step_callback(cpu);
            }

            break;
        }
    }

    if (cpu->cycles_this_frame >= FGB_CYCLES_PER_FRAME) {
        cpu->frames++;

        if (cpu->frames != cpu->ppu->frames_rendered) {
            log_trace("CPU frames (%d) and PPU frames (%d) are out of sync", cpu->frames, cpu->ppu->frames_rendered);
        }
    }
}

uint32_t fgb_cpu_step(fgb_cpu* cpu) {
    const uint32_t start_cycles = cpu->cycles_this_frame;

    switch (cpu->mode) {
    case CPU_MODE_NORMAL:
        fgb_cpu_run_instruction(cpu, fgb_cpu_fetch_instruction(cpu));
        break;

    case CPU_MODE_STOP:
    case CPU_MODE_HALT:
        fgb_cpu_m_tick(cpu);
        break;

    case CPU_MODE_HALT_BUG: {
        const fgb_instruction* instr = fgb_cpu_fetch_instruction(cpu);

        // Revert PC increment
        cpu->regs.pc--;

        fgb_cpu_run_instruction(cpu, instr);
        cpu->mode = CPU_MODE_NORMAL;
    } break;

    case CPU_MODE_HALT_DI:
        fgb_cpu_m_tick(cpu);
        if (fgb_cpu_has_pending_interrupts(cpu)) {
            cpu->mode = CPU_MODE_NORMAL;
        }
        break;

    case CPU_MODE_EI:
        // Enable interrupts after the next instruction
        cpu->ime = true;
        cpu->mode = CPU_MODE_NORMAL;

        fgb_cpu_run_instruction(cpu, fgb_cpu_fetch_instruction(cpu));
        break;
    }

    if (fgb_cpu_has_pending_interrupts(cpu)) {
        fgb_cpu_handle_interrupts(cpu);
    }

    return cpu->cycles_this_frame - start_cycles;
}

void fgb_cpu_run_instruction(fgb_cpu *cpu, const fgb_instruction* instr) {
    const uint16_t addr = cpu->regs.pc - 1; // Address of the fetched opcode
    const uint32_t depth = cpu->call_depth;

    instr->exec_0(cpu, instr);

    if (cpu->trace_callback && (cpu->trace_count > 0 || cpu->trace_count < 0)) {
        if (cpu->trace_count > 0) {
            cpu->trace_count--;
        }

        switch (instr->operand_size) {
        case 0:
            cpu->trace_callback(cpu, addr, depth, instr->fmt_0(instr));
            break;
        case 1:
            cpu->trace_callback(cpu, addr, depth, instr->fmt_1(instr, fgb_mmu_read_u8(cpu, addr + 1)));
            break;
        case 2:
            cpu->trace_callback(cpu, addr, depth, instr->fmt_2(instr, fgb_mmu_read_u16(cpu, addr + 1)));
            break;
        default:
            cpu->trace_callback(cpu, addr, depth, "UNKNOWN");
            break;
        }
    }
}

void fgb_cpu_request_interrupt(fgb_cpu* cpu, enum fgb_cpu_interrupt interrupt) {
    cpu->interrupt.flags |= (uint8_t)interrupt;
}

bool fgb_cpu_has_pending_interrupts(const fgb_cpu *cpu) {
    return cpu->interrupt.enable & cpu->interrupt.flags & IRQ_MASK;
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
        return cpu->interrupt.flags | 0xE0; // Upper 3 bits are always 1

    default:
        log_warn("Unknown address for CPU read: 0x%04X", addr);
        return 0xFF;
    }
}

void fgb_cpu_dump_state(const fgb_cpu* cpu) {
    log_info("CPU State -----------------------");
    log_info("Registers:");
    log_info("  AF: 0x%04X", cpu->regs.af);
    log_info("  BC: 0x%04X", cpu->regs.bc);
    log_info("  DE: 0x%04X", cpu->regs.de);
    log_info("  HL: 0x%04X", cpu->regs.hl);
    log_info("  SP: 0x%04X", cpu->regs.sp);
    log_info("  PC: 0x%04X", cpu->regs.pc);
    log_info("Flags:");
    log_info("  Z: %d", get_flag(z));
    log_info("  N: %d", get_flag(n));
    log_info("  H: %d", get_flag(h));
    log_info("  C: %d", get_flag(c));
    log_info("IME: %d", cpu->ime);
    log_info("Halted: %d", cpu->mode == CPU_MODE_HALT);
    log_info("Interrupts:");
    log_info("  Enable: 0x%02X", cpu->interrupt.enable);
    log_info("  Flags: 0x%02X", cpu->interrupt.flags);
    log_info("Breakpoints:");
    for (size_t i = 0; i < FGB_CPU_MAX_BREAKPOINTS; i++) {
        if (cpu->breakpoints[i] != FGB_BP_ADDR_NONE) {
            log_info("  Breakpoint %zu: 0x%04X", i, cpu->breakpoints[i]);
        }
    }
    log_info("Debugging: %d", cpu->debugging);
    log_info("PPU State: -----------------------");
    log_info("LY: %d", cpu->ppu->ly);
    log_info("-------------------------------");
}

void fgb_cpu_disassemble(const fgb_cpu* cpu, uint16_t addr, int count) {
    log_info("Disassembling from 0x%04X for %d instructions:", addr, count);
    int offset = 0;
    for (int i = 0; i < count; i++) {
        const uint8_t opcode = fgb_mmu_read_u8(cpu, addr + offset);
        const fgb_instruction* instruction = fgb_instruction_get(opcode);
        assert(instruction->opcode == opcode);
        uint8_t op8 = 0;
        uint16_t op16 = 0;
        switch (instruction->operand_size) {
        case 0:
            log_info("0x%04X: %s", addr + offset, instruction->fmt_0(instruction));
            break;
        case 1:
            op8 = fgb_mmu_read_u8(cpu, addr + offset + 1);
            log_info("0x%04X: %s", addr + offset, instruction->fmt_1(instruction, op8));
            break;
        case 2:
            op16 = fgb_mmu_read_u16(cpu, addr + offset + 1);
            log_info("0x%04X: %s", addr + offset, instruction->fmt_2(instruction, op16));
            break;
        default:
            log_error("Invalid operand size: %d", instruction->operand_size);
            return;
        }

        offset += instruction->operand_size + 1; // +1 for the opcode byte
    }
}

void fgb_cpu_disassemble_to(const fgb_cpu* cpu, uint16_t addr, int count, char** dest) {
    int offset = 0;
    for (int i = 0; i < count; i++) {
        const uint8_t opcode = fgb_mmu_read_u8(cpu, addr + offset);
        const fgb_instruction* instruction = fgb_instruction_get(opcode);
        assert(instruction->opcode == opcode);
        uint8_t op8 = 0;
        uint16_t op16 = 0;
        switch (instruction->operand_size) {
        case 0:
            sprintf(dest[i], "0x%04X: %s", addr + offset, instruction->fmt_0(instruction));
            break;
        case 1:
            op8 = fgb_mmu_read_u8(cpu, addr + offset + 1);
            sprintf(dest[i], "0x%04X: %s", addr + offset, instruction->fmt_1(instruction, op8));
            break;
        case 2:
            op16 = fgb_mmu_read_u16(cpu, addr + offset + 1);
            sprintf(dest[i], "0x%04X: %s", addr + offset, instruction->fmt_2(instruction, op16));
            break;
        default:
            log_error("Invalid operand size: %d", instruction->operand_size);
            return;
        }

        offset += instruction->operand_size + 1; // +1 for the opcode byte
    }
}

uint16_t fgb_cpu_disassemble_one(const fgb_cpu* cpu, uint16_t addr, char* dest, size_t dest_size) {
    const uint8_t opcode = fgb_mmu_read_u8(cpu, addr);
    const fgb_instruction* instruction = fgb_instruction_get(opcode);
    assert(instruction->opcode == opcode);

    if (dest == NULL || dest_size == 0) {
        return addr + instruction->operand_size + 1;
    }

    uint8_t op8 = 0;
    uint16_t op16 = 0;
    switch (instruction->operand_size) {
    case 0:
        strncpy(dest, instruction->fmt_0(instruction), dest_size);
        break;
    case 1:
        op8 = fgb_mmu_read_u8(cpu, addr + 1);
        strncpy(dest, instruction->fmt_1(instruction, op8), dest_size);
        break;
    case 2:
        op16 = fgb_mmu_read_u16(cpu, addr + 1);
        strncpy(dest, instruction->fmt_2(instruction, op16), dest_size);
        break;
    default:
        log_error("Invalid operand size: %d", instruction->operand_size);
        break;
    }

    return addr + instruction->operand_size + 1; // +1 for the opcode byte
}

void fgb_cpu_set_bp(fgb_cpu* cpu, uint16_t addr) {
    for (size_t i = 0; i < FGB_CPU_MAX_BREAKPOINTS; i++) {
        if (cpu->breakpoints[i] == addr) {
            return;
        }
    }

    for (size_t i = 0; i < FGB_CPU_MAX_BREAKPOINTS; i++) {
        if (cpu->breakpoints[i] == FGB_BP_ADDR_NONE) {
            cpu->breakpoints[i] = addr;
            return;
        }
    }

    log_error("No free breakpoint slots available");
}

void fgb_cpu_clear_bp(fgb_cpu* cpu, uint16_t addr) {
    for (size_t i = 0; i < FGB_CPU_MAX_BREAKPOINTS; i++) {
        if (cpu->breakpoints[i] == addr) {
            cpu->breakpoints[i] = FGB_BP_ADDR_NONE;
            return;
        }
    }

    log_warn("Breakpoint not found: 0x%04X", addr);
}

int fgb_cpu_get_bp_at(const fgb_cpu* cpu, uint16_t addr) {
    for (size_t i = 0; i < FGB_CPU_MAX_BREAKPOINTS; i++) {
        if (cpu->breakpoints[i] == addr) {
            return (int)i;
        }
    }

    return -1;
}

void fgb_cpu_set_bp_callback(fgb_cpu* cpu, fgb_cpu_bp_callback callback) {
    cpu->bp_callback = callback;
}

void fgb_cpu_set_step_callback(fgb_cpu* cpu, fgb_cpu_step_callback callback) {
    cpu->step_callback = callback;
}

void fgb_cpu_set_trace_callback(fgb_cpu *cpu, fgb_cpu_trace_callback callback) {
    cpu->trace_callback = callback;
}

const fgb_instruction* fgb_cpu_fetch_instruction(fgb_cpu *cpu) {
    const uint8_t opcode = fgb_cpu_fetch(cpu);
    const fgb_instruction* ins = fgb_instruction_get(opcode);
    assert(ins->opcode == opcode);

    return ins;
}

uint8_t fgb_cpu_fetch(fgb_cpu *cpu) {
    return fgb_cpu_read_u8(cpu, cpu->regs.pc++);
}

uint16_t fgb_cpu_fetch_u16(fgb_cpu* cpu) {
    const uint16_t value = fgb_cpu_read_u16(cpu, cpu->regs.pc);
    cpu->regs.pc += 2;
    return value;
}

uint8_t fgb_cpu_read_u8(fgb_cpu *cpu, uint16_t addr) {
    fgb_cpu_tick(cpu);
    fgb_cpu_tick(cpu);
    fgb_cpu_tick(cpu);
    const uint8_t val = fgb_mmu_read_u8(cpu, addr);
    fgb_cpu_tick(cpu);

    return val;
}

uint16_t fgb_cpu_read_u16(fgb_cpu *cpu, uint16_t addr) {
    const uint16_t low = fgb_cpu_read_u8(cpu, addr);
    const uint16_t high = fgb_cpu_read_u8(cpu, addr + 1);

    return (high << 8 | low) & 0xFFFF;
}

void fgb_cpu_write_u8(fgb_cpu *cpu, uint16_t addr, uint8_t value) {
    fgb_cpu_tick(cpu);
    fgb_cpu_tick(cpu);
    fgb_cpu_tick(cpu);
    fgb_mmu_write(cpu, addr, value);
    fgb_cpu_tick(cpu);
}

void fgb_cpu_write_u16(fgb_cpu *cpu, uint16_t addr, uint16_t value) {
    fgb_cpu_write_u8(cpu, addr + 0, (value >> 0) & 0xFF);
    fgb_cpu_write_u8(cpu, addr + 1, (value >> 8) & 0xFF);
}

static inline void fgb_call(fgb_cpu* cpu, uint16_t dest);
static inline void fgb_push(fgb_cpu* cpu, uint16_t value);

static const uint16_t fgb_interrupt_vector[] = {
    [IRQ_VBLANK] = 0x0040,
    [IRQ_LCD]    = 0x0048,
    [IRQ_TIMER]  = 0x0050,
    [IRQ_SERIAL] = 0x0058,
    [IRQ_JOYPAD] = 0x0060,
};

void fgb_cpu_handle_interrupts(fgb_cpu* cpu) {
    if (cpu->force_disable_interrupts || !cpu->ime) {
        return;
    }

    fgb_cpu_write_u8(cpu, --cpu->regs.sp, (cpu->regs.pc >> 8) & 0xFF);

    const uint8_t ienable = cpu->interrupt.enable;
    uint8_t iflags = cpu->interrupt.flags;
    const uint8_t irq = ienable & iflags;
    uint16_t dest = 0x0000; // 0 for the case where SP is 0 so PC is pushed to IE and may disable interrupts

    if      (irq & IRQ_VBLANK) { iflags &= ~IRQ_VBLANK; dest = fgb_interrupt_vector[IRQ_VBLANK]; }
    else if (irq & IRQ_LCD)    { iflags &= ~IRQ_LCD;    dest = fgb_interrupt_vector[IRQ_LCD];    }
    else if (irq & IRQ_TIMER)  { iflags &= ~IRQ_TIMER;  dest = fgb_interrupt_vector[IRQ_TIMER];  }
    else if (irq & IRQ_SERIAL) { iflags &= ~IRQ_SERIAL; dest = fgb_interrupt_vector[IRQ_SERIAL]; }
    else if (irq & IRQ_JOYPAD) { iflags &= ~IRQ_JOYPAD; dest = fgb_interrupt_vector[IRQ_JOYPAD]; }

    cpu->interrupt.flags = iflags;
    cpu->ime = false;
    cpu->mode = CPU_MODE_NORMAL;

    fgb_cpu_write_u8(cpu, --cpu->regs.sp, (cpu->regs.pc >> 0) & 0xFF);

    fgb_cpu_m_tick(cpu);
    fgb_cpu_m_tick(cpu);
    fgb_cpu_m_tick(cpu);

    cpu->regs.pc = dest;
}


// --------------------------------------------------------------
// Instruction execution functions
// --------------------------------------------------------------

static inline uint8_t fgb_inc_u8(fgb_cpu* cpu, uint8_t value) {
    set_flag(h, (value & 0xF) == 0xF);
    value++;
    set_flag(z, value == 0);
    set_flag(n, 0);

    return value;
}

static inline uint8_t fgb_dec_u8(fgb_cpu* cpu, uint8_t value) {
    set_flag(h, (value & 0xF) == 0);
    value--;
    set_flag(z, value == 0);
    set_flag(n, 1);
    return value;
}

static inline uint8_t fgb_add_u8(fgb_cpu* cpu, uint8_t a, uint8_t b) {
    const uint16_t result = a + b;
    set_flag(z, (result & 0xFF) == 0);
    set_flag(n, 0);
    set_flag(h, (a & 0xF) + (b & 0xF) > 0xF);
    set_flag(c, result > 0xFF);
    return result & 0xFF;
}

static inline uint16_t fgb_add_u16(fgb_cpu* cpu, uint16_t a, uint16_t b) {
    const uint32_t result = a + b;
    set_flag(c, result > 0xFFFF);
    set_flag(h, ((a & 0xFFF) + (b & 0xFFF)) > 0xFFF);
    set_flag(n, 0);
    return result & 0xFFFF;
}

static inline uint8_t fgb_adc_u8(fgb_cpu* cpu, uint8_t a, uint8_t b) {
    const uint16_t result = a + b + get_flag(c);
    set_flag(z, (result & 0xFF) == 0);
    set_flag(n, 0);
    set_flag(h, (a & 0xF) + (b & 0xF) + get_flag(c) > 0xF);
    set_flag(c, result > 0xFF);
    return result & 0xFF;
}

static inline uint8_t fgb_sub_u8(fgb_cpu* cpu, uint8_t a, uint8_t b) {
    set_flag(c, b > a);
    set_flag(h, (b & 0xF) > (a & 0xF));
    set_flag(n, 1);
    a -= b;
    set_flag(z, a == 0);
    return a;
}

static inline uint8_t fgb_sbc_u8(fgb_cpu* cpu, uint8_t a, uint8_t b) {
    set_flag(h, (b & 0xF) + get_flag(c) > (a & 0xF));
    const uint8_t new_c = b + get_flag(c) > a;
    set_flag(n, 1);
    a -= b + get_flag(c);
    set_flag(c, new_c);
    set_flag(z, a == 0);
    return a;
}

static inline uint8_t fgb_and_u8(fgb_cpu* cpu, uint8_t a, uint8_t b) {
    a &= b;
    set_flag(z, a == 0);
    set_flag(n, 0);
    set_flag(h, 1);
    set_flag(c, 0);
    return a;
}

static inline uint8_t fgb_xor_u8(fgb_cpu* cpu, uint8_t a, uint8_t b) {
    a ^= b;
    set_flag(z, a == 0);
    set_flag(n, 0);
    set_flag(h, 0);
    set_flag(c, 0);
    return a;
}

static inline uint8_t fgb_or_u8(fgb_cpu* cpu, uint8_t a, uint8_t b) {
    a |= b;
    set_flag(z, a == 0);
    set_flag(n, 0);
    set_flag(h, 0);
    set_flag(c, 0);
    return a;
}

// 3 M-cycles
static inline void fgb_call(fgb_cpu* cpu, uint16_t dest) {
    fgb_cpu_m_tick(cpu);
    fgb_cpu_write_u8(cpu, --cpu->regs.sp, (cpu->regs.pc >> 8) & 0xFF);
    fgb_cpu_write_u8(cpu, --cpu->regs.sp, (cpu->regs.pc >> 0) & 0xFF);
    cpu->regs.pc = dest;

    cpu->call_depth++;
}

inline void fgb_push(fgb_cpu *cpu, uint16_t value) {
    fgb_cpu_write_u8(cpu, --cpu->regs.sp, (value >> 8) & 0xFF);
    fgb_cpu_write_u8(cpu, --cpu->regs.sp, (value >> 0) & 0xFF);
}

// 3 M-cycles
static inline void fgb_ret_(fgb_cpu* cpu) {
    const uint8_t low = fgb_cpu_read_u8(cpu, cpu->regs.sp++);
    const uint8_t high = fgb_cpu_read_u8(cpu, cpu->regs.sp++);
    cpu->regs.pc = (high << 8 | low) & 0xFFFF;
    fgb_cpu_m_tick(cpu);

    if (cpu->call_depth > 0) {
        cpu->call_depth--;
    }
}

void fgb_nop(fgb_cpu* cpu, const fgb_instruction* ins) {
    (void)cpu;
    (void)ins;
}

void fgb_stop(fgb_cpu* cpu, const fgb_instruction* ins) {
    (void)fgb_cpu_fetch(cpu); // Consume the next byte
    
    // Just gonna interpret STOP as a HALT because I cba
    cpu->mode = CPU_MODE_STOP;
}

void fgb_halt(fgb_cpu* cpu, const fgb_instruction* ins) {
    if (cpu->ime) {
        cpu->mode = CPU_MODE_HALT;
    } else {
        if (fgb_cpu_has_pending_interrupts(cpu)) {
            cpu->mode = CPU_MODE_HALT_BUG;
        } else {
            cpu->mode = CPU_MODE_HALT_DI;
        }
    }
}

void fgb_ld_bc_imm(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.bc = fgb_cpu_fetch_u16(cpu);
}

void fgb_ld_de_imm(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.de = fgb_cpu_fetch_u16(cpu);
}

void fgb_ld_hl_imm(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.hl = fgb_cpu_fetch_u16(cpu);
}

void fgb_ld_sp_imm(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.sp = fgb_cpu_fetch_u16(cpu);
}

void fgb_ld_p_bc_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_cpu_write_u8(cpu, cpu->regs.bc, cpu->regs.a);
}

void fgb_ld_p_de_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_cpu_write_u8(cpu, cpu->regs.de, cpu->regs.a);
}

void fgb_ld_p_hli_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_cpu_write_u8(cpu, cpu->regs.hl++, cpu->regs.a);
}

void fgb_ld_p_hld_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_cpu_write_u8(cpu, cpu->regs.hl--, cpu->regs.a);
}

void fgb_ld_b_imm(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.b = fgb_cpu_fetch(cpu);
}

void fgb_ld_d_imm(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.d = fgb_cpu_fetch(cpu);
}

void fgb_ld_h_imm(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.h = fgb_cpu_fetch(cpu);
}

void fgb_ld_c_imm(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.c = fgb_cpu_fetch(cpu);
}

void fgb_ld_e_imm(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.e = fgb_cpu_fetch(cpu);
}

void fgb_ld_l_imm(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.l = fgb_cpu_fetch(cpu);
}

void fgb_ld_a_imm(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_cpu_fetch(cpu);
}

void fgb_ld_p_hl_imm(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_cpu_write_u8(cpu, cpu->regs.hl, fgb_cpu_fetch(cpu));
}

void fgb_ld_a_p_bc(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_cpu_read_u8(cpu, cpu->regs.bc);
}

void fgb_ld_a_p_de(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_cpu_read_u8(cpu, cpu->regs.de);
}

void fgb_ld_a_p_hli(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_cpu_read_u8(cpu, cpu->regs.hl++);
}

void fgb_ld_a_p_hld(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_cpu_read_u8(cpu, cpu->regs.hl--);
}

void fgb_inc_bc(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.bc++;
    fgb_cpu_m_tick(cpu);
}

void fgb_inc_de(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.de++;
    fgb_cpu_m_tick(cpu);
}

void fgb_inc_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.hl++;
    fgb_cpu_m_tick(cpu);
}

void fgb_inc_sp(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.sp++;
    fgb_cpu_m_tick(cpu);
}

void fgb_dec_bc(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.bc--;
    fgb_cpu_m_tick(cpu);
}

void fgb_dec_de(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.de--;
    fgb_cpu_m_tick(cpu);
}

void fgb_dec_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.hl--;
    fgb_cpu_m_tick(cpu);
}

void fgb_dec_sp(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.sp--;
    fgb_cpu_m_tick(cpu);
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
    uint8_t value = fgb_cpu_read_u8(cpu, cpu->regs.hl);
    value = fgb_inc_u8(cpu, value);
    fgb_cpu_write_u8(cpu, cpu->regs.hl, value);
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
    uint8_t value = fgb_cpu_read_u8(cpu, cpu->regs.hl);
    value = fgb_dec_u8(cpu, value);
    fgb_cpu_write_u8(cpu, cpu->regs.hl, value);
}

void fgb_rlca(fgb_cpu* cpu, const fgb_instruction* ins) {
    set_flag(c, cpu->regs.a >> 7);
    cpu->regs.a <<= 1;
    cpu->regs.a |= get_flag(c);

    set_flag(z, 0);
    set_flag(n, 0);
    set_flag(h, 0);
}

void fgb_rrca(fgb_cpu* cpu, const fgb_instruction* ins) {
    set_flag(c, cpu->regs.a & 1);
    cpu->regs.a >>= 1;
    cpu->regs.a |= get_flag(c) << 7;

    set_flag(z, 0);
    set_flag(n, 0);
    set_flag(h, 0);
}

void fgb_rla(fgb_cpu* cpu, const fgb_instruction* ins) {
    const uint8_t c = get_flag(c);
    set_flag(c, cpu->regs.a >> 7);
    cpu->regs.a <<= 1;
    cpu->regs.a |= c;

    set_flag(z, 0);
    set_flag(n, 0);
    set_flag(h, 0);
}

void fgb_rra(fgb_cpu* cpu, const fgb_instruction* ins) {
    const uint8_t c = get_flag(c);
    set_flag(c, cpu->regs.a & 1);
    cpu->regs.a >>= 1;
    cpu->regs.a |= c << 7;

    set_flag(z, 0);
    set_flag(n, 0);
    set_flag(h, 0);
}

void fgb_daa(fgb_cpu* cpu, const fgb_instruction* ins) {
    uint8_t adj = 0;
    uint16_t a = cpu->regs.a;
    if (get_flag(n)) {
        if (get_flag(h)) adj += 0x6;
        if (get_flag(c)) adj += 0x60;
        a -= adj;
    } else {
        if (get_flag(h) || (a & 0xF) > 0x9) adj += 0x6;
        if (get_flag(c) || a > 0x99) { adj += 0x60; set_flag(c, 1); }
        a += adj;
    }

    cpu->regs.a = a & 0xFF;
    set_flag(z, cpu->regs.a == 0);
    set_flag(h, 0);
}

void fgb_cpl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a ^= 0xFF;
    set_flag(n, 1);
    set_flag(h, 1);
}

void fgb_scf(fgb_cpu* cpu, const fgb_instruction* ins) {
    set_flag(n, 0);
    set_flag(h, 0);
    set_flag(c, 1);
}

void fgb_ccf(fgb_cpu* cpu, const fgb_instruction* ins) {
    set_flag(n, 0);
    set_flag(h, 0);
    toggle_flag(c);
}

void fgb_ld_p_imm_sp(fgb_cpu* cpu, const fgb_instruction* ins) {
    const uint16_t addr = fgb_cpu_fetch_u16(cpu);
    fgb_cpu_write_u16(cpu, addr, cpu->regs.sp);
}

static void fgb_jr_(fgb_cpu* cpu, int8_t offset) {
    cpu->regs.pc += offset;
    fgb_cpu_m_tick(cpu); // Extra cycle for the jump
}

void fgb_jr(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_jr_(cpu, (int8_t)fgb_cpu_fetch(cpu));
}

void fgb_jr_nz(fgb_cpu* cpu, const fgb_instruction* ins) {
    // Offset read happens regardless of whether the jump is taken
    const int8_t offset = (int8_t)fgb_cpu_fetch(cpu);
    if (!get_flag(z)) {
        fgb_jr_(cpu, offset);
    }
}

void fgb_jr_z(fgb_cpu* cpu, const fgb_instruction* ins) {
    const int8_t offset = (int8_t)fgb_cpu_fetch(cpu);
    if (get_flag(z)) {
        fgb_jr_(cpu, offset);
    }
}

void fgb_jr_nc(fgb_cpu* cpu, const fgb_instruction* ins) {
    const int8_t offset = (int8_t)fgb_cpu_fetch(cpu);
    if (!get_flag(c)) {
        fgb_jr_(cpu, offset);
    }
}

void fgb_jr_c(fgb_cpu* cpu, const fgb_instruction* ins) {
    const int8_t offset = (int8_t)fgb_cpu_fetch(cpu);
    if (get_flag(c)) {
        fgb_jr_(cpu, offset);
    }
}

void fgb_add_hl_bc(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.hl = fgb_add_u16(cpu, cpu->regs.hl, cpu->regs.bc);
    fgb_cpu_m_tick(cpu);
}

void fgb_add_hl_de(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.hl = fgb_add_u16(cpu, cpu->regs.hl, cpu->regs.de);
    fgb_cpu_m_tick(cpu);
}

void fgb_add_hl_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.hl = fgb_add_u16(cpu, cpu->regs.hl, cpu->regs.hl);
    fgb_cpu_m_tick(cpu);
}

void fgb_add_hl_sp(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.hl = fgb_add_u16(cpu, cpu->regs.hl, cpu->regs.sp);
    fgb_cpu_m_tick(cpu);
}

void fgb_add_sp_imm(fgb_cpu* cpu, const fgb_instruction* ins) {
    const uint8_t operand = fgb_cpu_fetch(cpu);
    const int sp = cpu->regs.sp + (int8_t)operand;
    set_flag(c, (cpu->regs.sp & 0xFF) + (operand & 0xFF) > 0xFF);
    set_flag(h, (cpu->regs.sp & 0xF) + (operand & 0xF) > 0xF);
    set_flag(n, 0);
    set_flag(z, 0);
    cpu->regs.sp = sp & 0xFFFF;

    fgb_cpu_m_tick(cpu);
    fgb_cpu_m_tick(cpu);
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
    cpu->regs.b = fgb_cpu_read_u8(cpu, cpu->regs.hl);
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
    cpu->regs.c = fgb_cpu_read_u8(cpu, cpu->regs.hl);
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
    cpu->regs.d = fgb_cpu_read_u8(cpu, cpu->regs.hl);
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
    cpu->regs.e = fgb_cpu_read_u8(cpu, cpu->regs.hl);
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
    cpu->regs.h = fgb_cpu_read_u8(cpu, cpu->regs.hl);
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
    cpu->regs.l = fgb_cpu_read_u8(cpu, cpu->regs.hl);
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
    cpu->regs.a = fgb_cpu_read_u8(cpu, cpu->regs.hl);
}

void fgb_ld_p_hl_b(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_cpu_write_u8(cpu, cpu->regs.hl, cpu->regs.b);
}

void fgb_ld_p_hl_c(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_cpu_write_u8(cpu, cpu->regs.hl, cpu->regs.c);
}

void fgb_ld_p_hl_d(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_cpu_write_u8(cpu, cpu->regs.hl, cpu->regs.d);
}

void fgb_ld_p_hl_e(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_cpu_write_u8(cpu, cpu->regs.hl, cpu->regs.e);
}

void fgb_ld_p_hl_h(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_cpu_write_u8(cpu, cpu->regs.hl, cpu->regs.h);
}

void fgb_ld_p_hl_l(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_cpu_write_u8(cpu, cpu->regs.hl, cpu->regs.l);
}

void fgb_ld_p_hl_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_cpu_write_u8(cpu, cpu->regs.hl, cpu->regs.a);
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
    cpu->regs.a = fgb_add_u8(cpu, cpu->regs.a, fgb_cpu_read_u8(cpu, cpu->regs.hl));
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
    cpu->regs.a = fgb_adc_u8(cpu, cpu->regs.a, fgb_cpu_read_u8(cpu, cpu->regs.hl));
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
    cpu->regs.a = fgb_sub_u8(cpu, cpu->regs.a, fgb_cpu_read_u8(cpu, cpu->regs.hl));
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
    cpu->regs.a = fgb_sbc_u8(cpu, cpu->regs.a, fgb_cpu_read_u8(cpu, cpu->regs.hl));
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
    cpu->regs.a = fgb_and_u8(cpu, cpu->regs.a, fgb_cpu_read_u8(cpu, cpu->regs.hl));
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
    cpu->regs.a = fgb_xor_u8(cpu, cpu->regs.a, fgb_cpu_read_u8(cpu, cpu->regs.hl));
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
    cpu->regs.a = fgb_or_u8(cpu, cpu->regs.a, fgb_cpu_read_u8(cpu, cpu->regs.hl));
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
    (void)fgb_sub_u8(cpu, cpu->regs.a, fgb_cpu_read_u8(cpu, cpu->regs.hl));
}

void fgb_add_a_imm(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_add_u8(cpu, cpu->regs.a, fgb_cpu_fetch(cpu));
}

void fgb_adc_a_imm(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_adc_u8(cpu, cpu->regs.a, fgb_cpu_fetch(cpu));
}

void fgb_sub_a_imm(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_sub_u8(cpu, cpu->regs.a, fgb_cpu_fetch(cpu));
}

void fgb_sbc_a_imm(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_sbc_u8(cpu, cpu->regs.a, fgb_cpu_fetch(cpu));
}

void fgb_and_a_imm(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_and_u8(cpu, cpu->regs.a, fgb_cpu_fetch(cpu));
}

void fgb_xor_a_imm(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_xor_u8(cpu, cpu->regs.a, fgb_cpu_fetch(cpu));
}

void fgb_or_a_imm(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_or_u8(cpu, cpu->regs.a, fgb_cpu_fetch(cpu));
}

void fgb_cp_a_imm(fgb_cpu* cpu, const fgb_instruction* ins) {
    (void)fgb_sub_u8(cpu, cpu->regs.a, fgb_cpu_fetch(cpu));
}

void fgb_push_bc(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_cpu_m_tick(cpu);
    fgb_cpu_write_u8(cpu, --cpu->regs.sp, cpu->regs.b);
    fgb_cpu_write_u8(cpu, --cpu->regs.sp, cpu->regs.c);
}

void fgb_push_de(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_cpu_m_tick(cpu);
    fgb_cpu_write_u8(cpu, --cpu->regs.sp, cpu->regs.d);
    fgb_cpu_write_u8(cpu, --cpu->regs.sp, cpu->regs.e);
}

void fgb_push_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_cpu_m_tick(cpu);
    fgb_cpu_write_u8(cpu, --cpu->regs.sp, cpu->regs.h);
    fgb_cpu_write_u8(cpu, --cpu->regs.sp, cpu->regs.l);
}

void fgb_push_af(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_cpu_m_tick(cpu);
    fgb_cpu_write_u8(cpu, --cpu->regs.sp, cpu->regs.a);
    fgb_cpu_write_u8(cpu, --cpu->regs.sp, cpu->regs.f);
}

void fgb_pop_bc(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.c = fgb_cpu_read_u8(cpu, cpu->regs.sp++);
    cpu->regs.b = fgb_cpu_read_u8(cpu, cpu->regs.sp++);
}

void fgb_pop_de(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.e = fgb_cpu_read_u8(cpu, cpu->regs.sp++);
    cpu->regs.d = fgb_cpu_read_u8(cpu, cpu->regs.sp++);
}

void fgb_pop_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.l = fgb_cpu_read_u8(cpu, cpu->regs.sp++);
    cpu->regs.h = fgb_cpu_read_u8(cpu, cpu->regs.sp++);
}

void fgb_pop_af(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.f = fgb_cpu_read_u8(cpu, cpu->regs.sp++) & 0xF0;
    cpu->regs.a = fgb_cpu_read_u8(cpu, cpu->regs.sp++);
}

void fgb_ld_p_imm_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_cpu_write_u8(cpu, 0xFF00 + fgb_cpu_fetch(cpu), cpu->regs.a);
}

void fgb_ld_a_p_imm(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_cpu_read_u8(cpu, 0xFF00 + fgb_cpu_fetch(cpu));
}

void fgb_ld_p_c_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_cpu_write_u8(cpu, 0xFF00 + cpu->regs.c, cpu->regs.a);
}

void fgb_ld_a_p_c(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_cpu_read_u8(cpu, 0xFF00 + cpu->regs.c);
}

void fgb_ld_p_imm16_a(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_cpu_write_u8(cpu, fgb_cpu_fetch_u16(cpu), cpu->regs.a);
}

void fgb_ld_a_p_imm16(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.a = fgb_cpu_read_u8(cpu, fgb_cpu_fetch_u16(cpu));
}

void fgb_ld_hl_sp_imm(fgb_cpu* cpu, const fgb_instruction* ins) {
    const uint8_t operand = fgb_cpu_fetch(cpu);
    const int hl = cpu->regs.sp + (int8_t)operand;
    set_flag(c, (cpu->regs.sp & 0xFF) + (operand & 0xFF) > 0xFF);
    set_flag(h, (cpu->regs.sp & 0xF) + (operand & 0xF) > 0xF);
    set_flag(n, 0);
    set_flag(z, 0);
    cpu->regs.hl = hl & 0xFFFF;

    fgb_cpu_m_tick(cpu);
}

void fgb_ld_sp_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.sp = cpu->regs.hl;
    fgb_cpu_m_tick(cpu);
}

static void fgb_jp_(fgb_cpu* cpu, uint16_t address) {
    cpu->regs.pc = address;
    fgb_cpu_m_tick(cpu);
}

void fgb_jp_imm16(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_jp_(cpu, fgb_cpu_fetch_u16(cpu));
}

void fgb_jp_z_imm16(fgb_cpu* cpu, const fgb_instruction* ins) {
    const uint16_t operand = fgb_cpu_fetch_u16(cpu);
    if (get_flag(z)) {
        fgb_jp_(cpu, operand);
    }
}

void fgb_jp_c_imm16(fgb_cpu* cpu, const fgb_instruction* ins) {
    const uint16_t operand = fgb_cpu_fetch_u16(cpu);
    if (get_flag(c)) {
        fgb_jp_(cpu, operand);
    }
}

void fgb_jp_nz_imm16(fgb_cpu* cpu, const fgb_instruction* ins) {
    const uint16_t operand = fgb_cpu_fetch_u16(cpu);
    if (!get_flag(z)) {
        fgb_jp_(cpu, operand);
    }
}

void fgb_jp_nc_imm16(fgb_cpu* cpu, const fgb_instruction* ins) {
    const uint16_t operand = fgb_cpu_fetch_u16(cpu);
    if (!get_flag(c)) {
        fgb_jp_(cpu, operand);
    }
}

void fgb_jp_hl(fgb_cpu* cpu, const fgb_instruction* ins) {
    cpu->regs.pc = cpu->regs.hl;
}

void fgb_call_imm16(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_call(cpu, fgb_cpu_fetch_u16(cpu));
}

void fgb_call_z_imm16(fgb_cpu* cpu, const fgb_instruction* ins) {
    const uint16_t operand = fgb_cpu_fetch_u16(cpu);
    if (get_flag(z)) {
        fgb_call(cpu, operand);
    }
}

void fgb_call_c_imm16(fgb_cpu* cpu, const fgb_instruction* ins) {
    const uint16_t operand = fgb_cpu_fetch_u16(cpu);
    if (get_flag(c)) {
        fgb_call(cpu, operand);
    }
}

void fgb_call_nz_imm16(fgb_cpu* cpu, const fgb_instruction* ins) {
    const uint16_t operand = fgb_cpu_fetch_u16(cpu);
    if (!get_flag(z)) {
        fgb_call(cpu, operand);
    }
}

void fgb_call_nc_imm16(fgb_cpu* cpu, const fgb_instruction* ins) {
    const uint16_t operand = fgb_cpu_fetch_u16(cpu);
    if (!get_flag(c)) {
        fgb_call(cpu, operand);
    }
}

void fgb_ret(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_ret_(cpu);
}

void fgb_ret_z(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_cpu_m_tick(cpu);
    if (get_flag(z)) {
        fgb_ret_(cpu);
    }
}

void fgb_ret_c(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_cpu_m_tick(cpu);
    if (get_flag(c)) {
        fgb_ret_(cpu);
    }
}

void fgb_ret_nz(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_cpu_m_tick(cpu);
    if (!get_flag(z)) {
        fgb_ret_(cpu);
    }
}

void fgb_ret_nc(fgb_cpu* cpu, const fgb_instruction* ins) {
    fgb_cpu_m_tick(cpu);
    if (!get_flag(c)) {
        fgb_ret_(cpu);
    }
}

void fgb_ei(fgb_cpu* cpu, const fgb_instruction* ins) {
    // The effect of EI is delayed by one instruction
    cpu->mode = CPU_MODE_EI;
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
    set_flag(c, value >> 7);
    value <<= 1;
    value |= get_flag(c);
    set_flag(z, value == 0);
    set_flag(n, 0);
    set_flag(h, 0);
    return value;
}

static inline uint8_t fgb_cb_rrc(fgb_cpu* cpu, uint8_t value) {
    set_flag(c, value & 1);
    value >>= 1;
    value |= get_flag(c) << 7;
    set_flag(z, value == 0);
    set_flag(n, 0);
    set_flag(h, 0);
    return value;
}

static inline uint8_t fgb_cb_rl(fgb_cpu* cpu, uint8_t value) {
    const uint8_t c = get_flag(c);
    set_flag(c, value >> 7);
    value <<= 1;
    value |= c;
    set_flag(z, value == 0);
    set_flag(n, 0);
    set_flag(h, 0);
    return value;
}

static inline uint8_t fgb_cb_rr(fgb_cpu* cpu, uint8_t value) {
    const uint8_t c = get_flag(c);
    set_flag(c, value & 1);
    value >>= 1;
    value |= c << 7;
    set_flag(z, value == 0);
    set_flag(n, 0);
    set_flag(h, 0);
    return value;
}

static inline uint8_t fgb_cb_sla(fgb_cpu* cpu, uint8_t value) {
    set_flag(c, value >> 7);
    value <<= 1;
    set_flag(z, value == 0);
    set_flag(n, 0);
    set_flag(h, 0);
    return value;
}

static inline uint8_t fgb_cb_sra(fgb_cpu* cpu, uint8_t value) {
    set_flag(c, value & 1);
    value = (int8_t)value >> 1;
    set_flag(z, value == 0);
    set_flag(n, 0);
    set_flag(h, 0);
    return value;
}

static inline uint8_t fgb_cb_swap(fgb_cpu* cpu, uint8_t value) {
    value = (value & 0x0F) << 4 | (value & 0xF0) >> 4;
    set_flag(z, value == 0);
    set_flag(n, 0);
    set_flag(h, 0);
    set_flag(c, 0);
    return value;
}

static inline uint8_t fgb_cb_srl(fgb_cpu* cpu, uint8_t value) {
    set_flag(c, value & 1);
    value >>= 1;
    set_flag(z, value == 0);
    set_flag(n, 0);
    set_flag(h, 0);
    return value;
}

static inline void fgb_cb_bit(fgb_cpu* cpu, uint8_t bit, uint8_t value) {
    set_flag(z, ((value >> bit) & 1) == 0);
    set_flag(n, 0);
    set_flag(h, 1);
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

void fgb_cb(fgb_cpu* cpu, const fgb_instruction* ins) {
    const uint8_t opcode = fgb_cpu_fetch(cpu);
    switch (opcode) {
        // RLC reg8
    case 0x00: cpu->regs.b = fgb_cb_rlc(cpu, cpu->regs.b); break;
    case 0x01: cpu->regs.c = fgb_cb_rlc(cpu, cpu->regs.c); break;
    case 0x02: cpu->regs.d = fgb_cb_rlc(cpu, cpu->regs.d); break;
    case 0x03: cpu->regs.e = fgb_cb_rlc(cpu, cpu->regs.e); break;
    case 0x04: cpu->regs.h = fgb_cb_rlc(cpu, cpu->regs.h); break;
    case 0x05: cpu->regs.l = fgb_cb_rlc(cpu, cpu->regs.l); break;
    case 0x06: fgb_cpu_write_u8(cpu, cpu->regs.hl, fgb_cb_rlc(cpu, fgb_cpu_read_u8(cpu, cpu->regs.hl))); break;
    case 0x07: cpu->regs.a = fgb_cb_rlc(cpu, cpu->regs.a); break;

        // RRC reg8
    case 0x08: cpu->regs.b = fgb_cb_rrc(cpu, cpu->regs.b); break;
    case 0x09: cpu->regs.c = fgb_cb_rrc(cpu, cpu->regs.c); break;
    case 0x0A: cpu->regs.d = fgb_cb_rrc(cpu, cpu->regs.d); break;
    case 0x0B: cpu->regs.e = fgb_cb_rrc(cpu, cpu->regs.e); break;
    case 0x0C: cpu->regs.h = fgb_cb_rrc(cpu, cpu->regs.h); break;
    case 0x0D: cpu->regs.l = fgb_cb_rrc(cpu, cpu->regs.l); break;
    case 0x0E: fgb_cpu_write_u8(cpu, cpu->regs.hl, fgb_cb_rrc(cpu, fgb_cpu_read_u8(cpu, cpu->regs.hl))); break;
    case 0x0F: cpu->regs.a = fgb_cb_rrc(cpu, cpu->regs.a); break;

        // RL reg8
    case 0x10: cpu->regs.b = fgb_cb_rl(cpu, cpu->regs.b); break;
    case 0x11: cpu->regs.c = fgb_cb_rl(cpu, cpu->regs.c); break;
    case 0x12: cpu->regs.d = fgb_cb_rl(cpu, cpu->regs.d); break;
    case 0x13: cpu->regs.e = fgb_cb_rl(cpu, cpu->regs.e); break;
    case 0x14: cpu->regs.h = fgb_cb_rl(cpu, cpu->regs.h); break;
    case 0x15: cpu->regs.l = fgb_cb_rl(cpu, cpu->regs.l); break;
    case 0x16: fgb_cpu_write_u8(cpu, cpu->regs.hl, fgb_cb_rl(cpu, fgb_cpu_read_u8(cpu, cpu->regs.hl))); break;
    case 0x17: cpu->regs.a = fgb_cb_rl(cpu, cpu->regs.a); break;

        // RR reg8
    case 0x18: cpu->regs.b = fgb_cb_rr(cpu, cpu->regs.b); break;
    case 0x19: cpu->regs.c = fgb_cb_rr(cpu, cpu->regs.c); break;
    case 0x1A: cpu->regs.d = fgb_cb_rr(cpu, cpu->regs.d); break;
    case 0x1B: cpu->regs.e = fgb_cb_rr(cpu, cpu->regs.e); break;
    case 0x1C: cpu->regs.h = fgb_cb_rr(cpu, cpu->regs.h); break;
    case 0x1D: cpu->regs.l = fgb_cb_rr(cpu, cpu->regs.l); break;
    case 0x1E: fgb_cpu_write_u8(cpu, cpu->regs.hl, fgb_cb_rr(cpu, fgb_cpu_read_u8(cpu, cpu->regs.hl))); break;
    case 0x1F: cpu->regs.a = fgb_cb_rr(cpu, cpu->regs.a); break;

        // SLA reg8
    case 0x20: cpu->regs.b = fgb_cb_sla(cpu, cpu->regs.b); break;
    case 0x21: cpu->regs.c = fgb_cb_sla(cpu, cpu->regs.c); break;
    case 0x22: cpu->regs.d = fgb_cb_sla(cpu, cpu->regs.d); break;
    case 0x23: cpu->regs.e = fgb_cb_sla(cpu, cpu->regs.e); break;
    case 0x24: cpu->regs.h = fgb_cb_sla(cpu, cpu->regs.h); break;
    case 0x25: cpu->regs.l = fgb_cb_sla(cpu, cpu->regs.l); break;
    case 0x26: fgb_cpu_write_u8(cpu, cpu->regs.hl, fgb_cb_sla(cpu, fgb_cpu_read_u8(cpu, cpu->regs.hl))); break;
    case 0x27: cpu->regs.a = fgb_cb_sla(cpu, cpu->regs.a); break;

        // SRA reg8
    case 0x28: cpu->regs.b = fgb_cb_sra(cpu, cpu->regs.b); break;
    case 0x29: cpu->regs.c = fgb_cb_sra(cpu, cpu->regs.c); break;
    case 0x2A: cpu->regs.d = fgb_cb_sra(cpu, cpu->regs.d); break;
    case 0x2B: cpu->regs.e = fgb_cb_sra(cpu, cpu->regs.e); break;
    case 0x2C: cpu->regs.h = fgb_cb_sra(cpu, cpu->regs.h); break;
    case 0x2D: cpu->regs.l = fgb_cb_sra(cpu, cpu->regs.l); break;
    case 0x2E: fgb_cpu_write_u8(cpu, cpu->regs.hl, fgb_cb_sra(cpu, fgb_cpu_read_u8(cpu, cpu->regs.hl))); break;
    case 0x2F: cpu->regs.a = fgb_cb_sra(cpu, cpu->regs.a); break;

        // SWAP reg8
    case 0x30: cpu->regs.b = fgb_cb_swap(cpu, cpu->regs.b); break;
    case 0x31: cpu->regs.c = fgb_cb_swap(cpu, cpu->regs.c); break;
    case 0x32: cpu->regs.d = fgb_cb_swap(cpu, cpu->regs.d); break;
    case 0x33: cpu->regs.e = fgb_cb_swap(cpu, cpu->regs.e); break;
    case 0x34: cpu->regs.h = fgb_cb_swap(cpu, cpu->regs.h); break;
    case 0x35: cpu->regs.l = fgb_cb_swap(cpu, cpu->regs.l); break;
    case 0x36: fgb_cpu_write_u8(cpu, cpu->regs.hl, fgb_cb_swap(cpu, fgb_cpu_read_u8(cpu, cpu->regs.hl))); break;
    case 0x37: cpu->regs.a = fgb_cb_swap(cpu, cpu->regs.a); break;

        // SRL reg8
    case 0x38: cpu->regs.b = fgb_cb_srl(cpu, cpu->regs.b); break;
    case 0x39: cpu->regs.c = fgb_cb_srl(cpu, cpu->regs.c); break;
    case 0x3A: cpu->regs.d = fgb_cb_srl(cpu, cpu->regs.d); break;
    case 0x3B: cpu->regs.e = fgb_cb_srl(cpu, cpu->regs.e); break;
    case 0x3C: cpu->regs.h = fgb_cb_srl(cpu, cpu->regs.h); break;
    case 0x3D: cpu->regs.l = fgb_cb_srl(cpu, cpu->regs.l); break;
    case 0x3E: fgb_cpu_write_u8(cpu, cpu->regs.hl, fgb_cb_srl(cpu, fgb_cpu_read_u8(cpu, cpu->regs.hl))); break;
    case 0x3F: cpu->regs.a = fgb_cb_srl(cpu, cpu->regs.a); break;

        // BIT n, reg8
    fgb_cb_cases(0x40): fgb_cb_bit(cpu, fgb_cb_bit_index(opcode), cpu->regs.b); break;
    fgb_cb_cases(0x41): fgb_cb_bit(cpu, fgb_cb_bit_index(opcode), cpu->regs.c); break;
    fgb_cb_cases(0x42): fgb_cb_bit(cpu, fgb_cb_bit_index(opcode), cpu->regs.d); break;
    fgb_cb_cases(0x43): fgb_cb_bit(cpu, fgb_cb_bit_index(opcode), cpu->regs.e); break;
    fgb_cb_cases(0x44): fgb_cb_bit(cpu, fgb_cb_bit_index(opcode), cpu->regs.h); break;
    fgb_cb_cases(0x45): fgb_cb_bit(cpu, fgb_cb_bit_index(opcode), cpu->regs.l); break;
    fgb_cb_cases(0x46): fgb_cb_bit(cpu, fgb_cb_bit_index(opcode), fgb_cpu_read_u8(cpu, cpu->regs.hl)); break;
    fgb_cb_cases(0x47): fgb_cb_bit(cpu, fgb_cb_bit_index(opcode), cpu->regs.a); break;

        // RES n, reg8
    fgb_cb_cases(0x80): cpu->regs.b = fgb_cb_res(cpu->regs.b, fgb_cb_bit_index(opcode)); break;
    fgb_cb_cases(0x81): cpu->regs.c = fgb_cb_res(cpu->regs.c, fgb_cb_bit_index(opcode)); break;
    fgb_cb_cases(0x82): cpu->regs.d = fgb_cb_res(cpu->regs.d, fgb_cb_bit_index(opcode)); break;
    fgb_cb_cases(0x83): cpu->regs.e = fgb_cb_res(cpu->regs.e, fgb_cb_bit_index(opcode)); break;
    fgb_cb_cases(0x84): cpu->regs.h = fgb_cb_res(cpu->regs.h, fgb_cb_bit_index(opcode)); break;
    fgb_cb_cases(0x85): cpu->regs.l = fgb_cb_res(cpu->regs.l, fgb_cb_bit_index(opcode)); break;
    fgb_cb_cases(0x86): fgb_cpu_write_u8(cpu, cpu->regs.hl, fgb_cb_res(fgb_cpu_read_u8(cpu, cpu->regs.hl), fgb_cb_bit_index(opcode))); break;
    fgb_cb_cases(0x87): cpu->regs.a = fgb_cb_res(cpu->regs.a, fgb_cb_bit_index(opcode)); break;

        // SET n, reg8
    fgb_cb_cases(0xC0): cpu->regs.b = fgb_cb_set(cpu->regs.b, fgb_cb_bit_index(opcode)); break;
    fgb_cb_cases(0xC1): cpu->regs.c = fgb_cb_set(cpu->regs.c, fgb_cb_bit_index(opcode)); break;
    fgb_cb_cases(0xC2): cpu->regs.d = fgb_cb_set(cpu->regs.d, fgb_cb_bit_index(opcode)); break;
    fgb_cb_cases(0xC3): cpu->regs.e = fgb_cb_set(cpu->regs.e, fgb_cb_bit_index(opcode)); break;
    fgb_cb_cases(0xC4): cpu->regs.h = fgb_cb_set(cpu->regs.h, fgb_cb_bit_index(opcode)); break;
    fgb_cb_cases(0xC5): cpu->regs.l = fgb_cb_set(cpu->regs.l, fgb_cb_bit_index(opcode)); break;
    fgb_cb_cases(0xC6): fgb_cpu_write_u8(cpu, cpu->regs.hl, fgb_cb_set(fgb_cpu_read_u8(cpu, cpu->regs.hl), fgb_cb_bit_index(opcode))); break;
    fgb_cb_cases(0xC7): cpu->regs.a = fgb_cb_set(cpu->regs.a, fgb_cb_bit_index(opcode)); break;

        // Shouldn't ever happen but whatever
    default: log_warn("Unknown instruction: CB %02X", opcode); break;
    }
}
