#include "timer.h"
#include "cpu.h"

#include <string.h>

#include <ulog.h>


enum {
    TIMER_DIV_ADDRESS   = 0xFF04,
    TIMER_TIMA_ADDRESS  = 0xFF05,
    TIMER_TMA_ADDRESS   = 0xFF06,
    TIMER_TAC_ADDRESS   = 0xFF07,
};

static const uint16_t div_bit_table[] = {
    [TIMER_CLOCK_4096_HZ]   = (1 << 9), // Bit 9
    [TIMER_CLOCK_262144_HZ] = (1 << 3), // Bit 3
    [TIMER_CLOCK_65536_HZ]  = (1 << 5), // Bit 5
    [TIMER_CLOCK_16384_HZ]  = (1 << 7), // Bit 7
};

void fgb_timer_init(fgb_timer* timer, fgb_cpu* cpu) {
    memset(timer, 0, sizeof(fgb_timer));
    timer->divider = 0xAC00; // Initial value for the divider register
    timer->cpu = cpu;
}

void fgb_timer_tick(fgb_timer* timer) {
    const uint16_t prev_div = timer->divider;
    timer->divider++;

    const uint16_t div_bit = div_bit_table[timer->clk_sel];
    if ((prev_div & div_bit) != 0 && (timer->divider & div_bit) == 0 && timer->enable) {
        timer->counter++;

        if (timer->counter == 0x00) { // Overflow
            timer->counter = timer->modulo;

            fgb_cpu_request_interrupt(timer->cpu, IRQ_TIMER);
        }
    }
}

void fgb_timer_write(fgb_timer* timer, uint16_t addr, uint8_t value) {
    switch (addr) {
    case TIMER_DIV_ADDRESS:
        timer->divider = 0; 
        break;

    case TIMER_TIMA_ADDRESS:
        timer->counter = value;
        break;

    case TIMER_TMA_ADDRESS:
        timer->modulo = value;
        break;

    case TIMER_TAC_ADDRESS:
        timer->control = value;
        break;

    default:
        log_warn("Unknown address for timer write: 0x%04X", addr);
        break;
    }
}

uint8_t fgb_timer_read(const fgb_timer* timer, uint16_t addr) {
    switch (addr) {
    case TIMER_DIV_ADDRESS:
        return timer->divider >> 8;

    case TIMER_TIMA_ADDRESS:
        return timer->counter;

    case TIMER_TMA_ADDRESS:
        return timer->modulo;

    case TIMER_TAC_ADDRESS:
        return timer->control;

    default:
        log_warn("Unknown address for timer read: 0x%04X", addr);
        return 0xAA; // Return a default value for unknown addresses
    }
}
