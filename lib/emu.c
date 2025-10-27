#include "emu.h"

#include <stdlib.h>

#include <ulog.h>


fgb_emu* fgb_emu_create(const uint8_t* cart_data, size_t cart_size) {
    fgb_emu* emu = malloc(sizeof(fgb_emu));
    if (!emu) {
        log_error("Failed to allocate emulator");
        return NULL;
    }

    emu->cart = fgb_cart_load(cart_data, cart_size);
    if (!emu->cart) {
        fgb_emu_destroy(emu);
        return NULL;
    }

    emu->ppu = fgb_ppu_create();
    if (!emu->ppu) {
        fgb_cart_destroy(emu->cart);
        fgb_emu_destroy(emu);
        return NULL;
    }

    emu->cpu = fgb_cpu_create(emu->cart, emu->ppu);
    if (!emu->cpu) {
        fgb_emu_destroy(emu);
        return NULL;
    }

    emu->mmu = &emu->cpu->mmu;

    return emu;
}

void fgb_emu_destroy(fgb_emu* emu) {
    if (emu->cart) fgb_cart_destroy(emu->cart);
    if (emu->cpu) fgb_cpu_destroy(emu->cpu);
    emu->cart = NULL;
    emu->cpu = NULL;
    emu->mmu = NULL;

    free(emu);
}

void fgb_emu_reset(fgb_emu* emu) {
    if (!emu || !emu->cpu) {
        log_error("Emulator or CPU not initialized");
        return;
    }

	fgb_cpu_reset(emu->cpu);
    fgb_ppu_reset(emu->ppu);
}

void fgb_emu_set_log_level(fgb_emu* emu, enum ulog_level level) {
    if (!emu || !emu->cpu) {
        log_error("Emulator or CPU not initialized");
        return;
    }

    ulog_set_level(level);
}

void fgb_emu_press_button(fgb_emu* emu, enum fgb_button button) {
    fgb_io_press_button(&emu->cpu->io, button);
}

void fgb_emu_release_button(fgb_emu* emu, enum fgb_button button) {
    fgb_io_release_button(&emu->cpu->io, button);
}
