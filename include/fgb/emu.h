#ifndef FGB_EMU_H
#define FGB_EMU_H

#include "apu.h"
#include "cart.h"
#include "cpu.h"
#include "mmu.h"
#include "ppu.h"


typedef struct fgb_emu {
    fgb_cpu* cpu;
    fgb_mmu* mmu;
    fgb_ppu* ppu;
    fgb_apu* apu;
    fgb_cart* cart;
} fgb_emu;


fgb_emu* fgb_emu_create(const uint8_t* cart_data, size_t cart_size, uint32_t apu_sample_rate, fgb_apu_sample_callback sample_cb, void* userdata);
void fgb_emu_destroy(fgb_emu* emu);
void fgb_emu_reset(fgb_emu* emu);

void fgb_emu_set_log_level(fgb_emu* emu, enum ulog_level level);

void fgb_emu_press_button(fgb_emu* emu, enum fgb_button button);
void fgb_emu_release_button(fgb_emu* emu, enum fgb_button button);

static inline void fgb_emu_set_button(fgb_emu* emu, enum fgb_button button, bool pressed) {
    if (pressed) {
        fgb_emu_press_button(emu, button);
    } else {
        fgb_emu_release_button(emu, button);
    }
}


#endif // FGB_EMU_H
