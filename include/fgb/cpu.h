#ifndef FGB_CPU_H
#define FGB_CPU_H

#include "apu.h"
#include "mmu.h"
#include "timer.h"
#include "io.h"
#include "instruction.h"
#include "ppu.h"
#include "types.h"

#include <stdbool.h>
#include <stdint.h>


#define FGB_CPU_CLOCK_SPEED     4194304 // 4.194304 MHz
#define FGB_SCREEN_REFRESH_RATE 59.7275 // ~59.7 Hz
#define FGB_CYCLES_PER_FRAME    ((int)(FGB_CPU_CLOCK_SPEED / FGB_SCREEN_REFRESH_RATE)) // 70224 T-cycles
#define FGB_CPU_MAX_BREAKPOINTS 16


enum fgb_cpu_interrupt {
    IRQ_VBLANK  = 1 << 0,
    IRQ_LCD     = 1 << 1,
    IRQ_TIMER   = 1 << 2,
    IRQ_SERIAL  = 1 << 3,
    IRQ_JOYPAD  = 1 << 4,
    IRQ_MASK    = IRQ_VBLANK | IRQ_LCD | IRQ_TIMER | IRQ_SERIAL | IRQ_JOYPAD,
};

enum fgb_cpu_mode {
    CPU_MODE_NORMAL,
    CPU_MODE_STOP,
    CPU_MODE_HALT,
    CPU_MODE_HALT_BUG,
    CPU_MODE_HALT_DI,
    CPU_MODE_EI,
};

enum fgb_cpu_flag {
    CPU_FLAG_C = 1 << 4,
    CPU_FLAG_H = 1 << 5,
    CPU_FLAG_N = 1 << 6,
    CPU_FLAG_Z = 1 << 7,

    CPU_FLAG_c = CPU_FLAG_C,
    CPU_FLAG_h = CPU_FLAG_H,
    CPU_FLAG_n = CPU_FLAG_N,
    CPU_FLAG_z = CPU_FLAG_Z,
};

typedef void (*fgb_cpu_bp_callback)(struct fgb_cpu* cpu, size_t bp, uint16_t addr);
typedef void (*fgb_cpu_step_callback)(struct fgb_cpu* cpu);
typedef void (*fgb_cpu_trace_callback)(struct fgb_cpu* cpu, uint16_t addr, uint32_t depth, const char* disasm);

typedef struct fgb_cpu_regs {
    union {
        uint16_t af;
        struct {
            uint8_t f;
            uint8_t a;
        };
    };

    union {
        uint16_t bc;
        struct {
            uint8_t c;
            uint8_t b;
        };
    };

    union {
        uint16_t de;
        struct {
            uint8_t e;
            uint8_t d;
        };
    };

    union {
        uint16_t hl;
        struct {
            uint8_t l;
            uint8_t h;
        };
    };

    uint16_t sp;
    uint16_t pc;
} fgb_cpu_regs;

typedef struct fgb_cpu_trace_step {
    uint16_t addr;
    union {
        uint8_t op8;
        uint16_t op16;
    };
    const fgb_instruction* instruction;
} fgb_cpu_trace_step;

typedef struct fgb_cpu {
    fgb_cpu_regs regs;
    fgb_mmu mmu;
    fgb_timer timer;
    fgb_io io;
    fgb_ppu* ppu;
    fgb_apu* apu;

    fgb_model model; // DMG or CGB

    bool test_mode;
    
    bool ime;
    enum fgb_cpu_mode mode;

    int trace_count;
    int frames;
    uint32_t call_depth;

    uint32_t cycles_this_frame;
    uint64_t total_cycles;

    struct {
        uint8_t enable;
        uint8_t flags;
    } interrupt;

    uint16_t breakpoints[FGB_CPU_MAX_BREAKPOINTS];
    bool debugging;
    bool do_step;
    fgb_cpu_bp_callback bp_callback;
    fgb_cpu_step_callback step_callback;
    fgb_cpu_trace_callback trace_callback;
    bool force_disable_interrupts;

    fgb_cpu_trace_step last_ins;
} fgb_cpu;


fgb_cpu* fgb_cpu_create(fgb_cart* cart, fgb_ppu* ppu, fgb_apu* apu);
fgb_cpu* fgb_cpu_create_with(fgb_cart* cart, fgb_ppu* ppu, fgb_apu* apu, const fgb_mmu_ops* mmu_ops);
// Extended create that allows choosing model and custom MMU ops
fgb_cpu* fgb_cpu_create_ex(fgb_cart* cart, fgb_ppu* ppu, fgb_apu* apu, fgb_model model, const fgb_mmu_ops* mmu_ops);
void fgb_cpu_destroy(fgb_cpu* cpu);

void fgb_cpu_tick(fgb_cpu* cpu); // Tick 1 T-cycle
void fgb_cpu_m_tick(fgb_cpu* cpu); // Tick 1 M-cycle (4 T-cycles)
void fgb_cpu_reset(fgb_cpu* cpu);
void fgb_cpu_run_frame(fgb_cpu* cpu); // Executes FGB_CYCLES_PER_FRAME cycles
uint32_t fgb_cpu_step(fgb_cpu* cpu); // Executes a single instruction and returns its cycles
void fgb_cpu_request_interrupt(fgb_cpu* cpu, enum fgb_cpu_interrupt interrupt);
bool fgb_cpu_has_pending_interrupts(const fgb_cpu* cpu);

void fgb_cpu_write(fgb_cpu* cpu, uint16_t addr, uint8_t value);
uint8_t fgb_cpu_read(const fgb_cpu* cpu, uint16_t addr);

static inline void fgb_cpu_set_flag(fgb_cpu* cpu, enum fgb_cpu_flag flag, bool value) {
    if (value) {
        cpu->regs.f |= flag;
    }
    else {
        cpu->regs.f &= ~flag;
    }
}

static inline void fgb_cpu_clear_flag(fgb_cpu* cpu, enum fgb_cpu_flag flag) {
    cpu->regs.f &= ~flag;
}

static inline bool fgb_cpu_get_flag(const fgb_cpu* cpu, enum fgb_cpu_flag flag) {
    return (cpu->regs.f & flag) != 0;
}

static inline void fgb_cpu_toggle_flag(fgb_cpu* cpu, enum fgb_cpu_flag flag) {
    cpu->regs.f ^= flag;
}

// Debugging
void fgb_cpu_dump_state(const fgb_cpu* cpu);
void fgb_cpu_disassemble(const fgb_cpu* cpu, uint16_t addr, int count);
void fgb_cpu_disassemble_to(const fgb_cpu* cpu, uint16_t addr, int count, char** dest);
uint16_t fgb_cpu_disassemble_one(const fgb_cpu* cpu, uint16_t addr, char* dest, size_t dest_size);
void fgb_cpu_set_bp(fgb_cpu* cpu, uint16_t addr);
void fgb_cpu_clear_bp(fgb_cpu* cpu, uint16_t addr);
int fgb_cpu_get_bp_at(const fgb_cpu* cpu, uint16_t addr);
void fgb_cpu_set_bp_callback(fgb_cpu* cpu, fgb_cpu_bp_callback callback);
void fgb_cpu_set_step_callback(fgb_cpu* cpu, fgb_cpu_step_callback callback);
void fgb_cpu_set_trace_callback(fgb_cpu* cpu, fgb_cpu_trace_callback callback);

#endif // FGB_CPU_H
