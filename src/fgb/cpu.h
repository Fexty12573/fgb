#ifndef FGB_CPU_H
#define FGB_CPU_H

#include <stdbool.h>
#include <stdint.h>

#define FGB_CPU_MEMORY_SIZE 0x10000 // 64KB memory size
#define FGB_CPU_CLOCK_SPEED 4194304 // 4.194304 MHz
#define FGB_SCREEN_WIDTH    160
#define FGB_SCREEN_HEIGHT   144


typedef struct fgb_cpu_regs {
    union {
        uint16_t af;
        struct {
            uint8_t a;
            uint8_t f;
        };
        struct {
            uint16_t : 8;
            uint16_t : 4;
            uint16_t c : 1;
            uint16_t h : 1;
            uint16_t n : 1;
            uint16_t z : 1;
        } flags;
    };

    union {
        uint16_t bc;
        struct {
            uint8_t b;
            uint8_t c;
        };
    };

    union {
        uint16_t de;
        struct {
            uint8_t d;
            uint8_t e;
        };
    };

    union {
        uint16_t hl;
        struct {
            uint8_t h;
            uint8_t l;
        };
    };

    uint16_t sp;
    uint16_t pc;
} fgb_cpu_regs;

typedef struct fgb_cpu {
    fgb_cpu_regs regs;

    uint8_t memory[FGB_CPU_MEMORY_SIZE];
    uint8_t screen[FGB_SCREEN_WIDTH * FGB_SCREEN_HEIGHT * 3]; // 3 bytes per pixel (RGB)

    bool halted;
    bool stopped;
} fgb_cpu;


fgb_cpu* fgb_cpu_create(void);
void fgb_cpu_destroy(fgb_cpu* cpu);

void fgb_cpu_reset(fgb_cpu* cpu);

#endif // FGB_CPU_H
