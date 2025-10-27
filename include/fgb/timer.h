#ifndef FGB_TIMER_H
#define FGB_TIMER_H

#include <stdint.h>


enum fgb_timer_clock {
    TIMER_CLOCK_4096_HZ   = 0,  // 256 M-Cycles
    TIMER_CLOCK_262144_HZ = 1,  // 4 M-Cycles
    TIMER_CLOCK_65536_HZ  = 2,  // 16 M-Cycles
    TIMER_CLOCK_16384_HZ  = 3,  // 64 M-Cycles
};

typedef struct fgb_timer {
    uint16_t divider;
    uint8_t counter;
    uint8_t modulo;
    union {
        uint8_t control;
        struct {
            uint8_t clk_sel : 2; // See fgb_timer_clock
            uint8_t enable : 1;
            uint8_t : 5;
        };
    };

    struct fgb_cpu* cpu;
} fgb_timer;


void fgb_timer_init(fgb_timer* timer, struct fgb_cpu* cpu);
void fgb_timer_tick(fgb_timer* timer);
void fgb_timer_reset(fgb_timer* timer);

void fgb_timer_write(fgb_timer* timer, uint16_t addr, uint8_t value);
uint8_t fgb_timer_read(const fgb_timer* timer, uint16_t addr);

#endif // FGB_TIMER_H
