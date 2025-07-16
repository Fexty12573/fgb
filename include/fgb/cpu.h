#ifndef FGB_CPU_H
#define FGB_CPU_H

#include "mmu.h"
#include "timer.h"
#include "io.h"
#include "ppu.h"

#include <stdbool.h>
#include <stdint.h>


#define FGB_CPU_CLOCK_SPEED     4194304 // 4.194304 MHz
#define FGB_SCREEN_REFRESH_RATE 60 // 60 Hz
#define FGB_CYCLES_PER_FRAME    (FGB_CPU_CLOCK_SPEED / FGB_SCREEN_REFRESH_RATE)
#define FGB_CPU_MAX_BREAKPOINTS 16


enum fgb_cpu_interrupt {
    IRQ_VBLANK  = 1 << 0,
    IRQ_LCD     = 1 << 1,
    IRQ_TIMER   = 1 << 2,
    IRQ_SERIAL  = 1 << 3,
    IRQ_JOYPAD  = 1 << 4,
};

typedef struct fgb_cpu_regs {
    union {
        uint16_t af;
        struct {
            uint8_t f;
            uint8_t a;
        };
        struct {
            uint16_t : 4;
            uint16_t c : 1;
            uint16_t h : 1;
            uint16_t n : 1;
            uint16_t z : 1;
            uint16_t : 8;
        } flags;
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

typedef struct fgb_cpu {
    fgb_cpu_regs regs;
    fgb_mmu mmu;
    fgb_timer timer;
    fgb_io io;
    fgb_ppu* ppu;
    
    bool ime;
    bool halted;

    bool trace;

    struct {
        uint8_t enable;
        uint8_t flags;
    } interrupt;

    uint16_t breakpoints[FGB_CPU_MAX_BREAKPOINTS];
    bool debugging;
    bool do_step;
} fgb_cpu;


fgb_cpu* fgb_cpu_create(fgb_cart* cart, fgb_ppu* ppu);
fgb_cpu* fgb_cpu_create_with(fgb_cart* cart, fgb_ppu* ppu, const fgb_mmu_ops* mmu_ops);
void fgb_cpu_destroy(fgb_cpu* cpu);

void fgb_cpu_reset(fgb_cpu* cpu);
void fgb_cpu_step(fgb_cpu* cpu); // Executes FGB_CYCLES_PER_FRAME cycles
int fgb_cpu_execute(fgb_cpu* cpu); // Executes a single instruction and returns its cycles
void fgb_cpu_request_interrupt(fgb_cpu* cpu, enum fgb_cpu_interrupt interrupt);

void fgb_cpu_write(fgb_cpu* cpu, uint16_t addr, uint8_t value);
uint8_t fgb_cpu_read(const fgb_cpu* cpu, uint16_t addr);

// Debugging
void fgb_cpu_dump_state(const fgb_cpu* cpu);
void fgb_cpu_disassemble(const fgb_cpu* cpu, uint16_t addr, int count);
void fgb_cpu_set_bp(fgb_cpu* cpu, uint16_t addr);
void fgb_cpu_clear_bp(fgb_cpu* cpu, uint16_t addr);

#endif // FGB_CPU_H
