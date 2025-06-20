#ifndef FGB_CPU_H
#define FGB_CPU_H

#include "mmu.h"

#include <stdbool.h>
#include <stdint.h>


#define FGB_CPU_CLOCK_SPEED     4194304 // 4.194304 MHz
#define FGB_SCREEN_WIDTH        160
#define FGB_SCREEN_HEIGHT       144
#define FGB_SCREEN_REFRESH_RATE 60 // 60 Hz
#define FGB_CYCLES_PER_FRAME    (FGB_CPU_CLOCK_SPEED / FGB_SCREEN_REFRESH_RATE)


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
    
    uint8_t screen[FGB_SCREEN_WIDTH * FGB_SCREEN_HEIGHT * 3]; // 3 bytes per pixel (RGB)

    bool ime;
    bool halted;
    bool stopped;
} fgb_cpu;


fgb_cpu* fgb_cpu_create(fgb_cart* cart);
fgb_cpu* fgb_cpu_create_with(fgb_cart* cart, const fgb_mmu_ops* mmu_ops);
void fgb_cpu_destroy(fgb_cpu* cpu);

void fgb_cpu_reset(fgb_cpu* cpu);
void fgb_cpu_step(fgb_cpu* cpu); // Executes FGB_CYCLES_PER_FRAME cycles
int fgb_cpu_execute(fgb_cpu* cpu); // Executes a single instruction and returns its cycles

#endif // FGB_CPU_H
