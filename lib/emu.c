#include "emu.h"

#include <stdlib.h>

#include <ulog.h>


fgb_emu* fgb_emu_create_ex(const uint8_t* cart_data, size_t cart_size,
                           fgb_model model,
                           uint32_t apu_sample_rate,
                           fgb_apu_sample_callback sample_cb,
                           void* userdata,
                           const fgb_mmu_ops* mmu_ops) {
    fgb_emu* emu = malloc(sizeof(fgb_emu));
    if (!emu) {
        log_error("Failed to allocate emulator");
        return NULL;
    }

    emu->model = model;

    emu->cart = fgb_cart_load(cart_data, cart_size);
    if (!emu->cart) {
        fgb_emu_destroy(emu);
        return NULL;
    }

    emu->ppu = (model == FGB_MODEL_DMG) ? fgb_ppu_create() : fgb_ppu_create_with_model(model);
    if (!emu->ppu) {
        fgb_emu_destroy(emu);
        return NULL;
    }

    emu->apu = fgb_apu_create(apu_sample_rate, sample_cb, userdata);
    if (!emu->apu) {
        fgb_emu_destroy(emu);
        return NULL;
    }

    // Prefer extended CPU create (propagate model and optional MMU ops)
    if (mmu_ops) {
        emu->cpu = fgb_cpu_create_ex(emu->cart, emu->ppu, emu->apu, model, mmu_ops);
    } else {
        emu->cpu = fgb_cpu_create_ex(emu->cart, emu->ppu, emu->apu, model, NULL);
    }

    if (!emu->cpu) {
        fgb_emu_destroy(emu);
        return NULL;
    }

    // Model hookup to PPU if needed
    fgb_ppu_set_model(emu->ppu, model);

    emu->mmu = &emu->cpu->mmu;

    return emu;
}

fgb_emu* fgb_emu_create(const uint8_t* cart_data, size_t cart_size, uint32_t apu_sample_rate, fgb_apu_sample_callback sample_cb, void* userdata) {
    // Back-compat: default to DMG
    return fgb_emu_create_ex(cart_data, cart_size, FGB_MODEL_DMG, apu_sample_rate, sample_cb, userdata, NULL);
}

void fgb_emu_destroy(fgb_emu* emu) {
    if (!emu) return;
    if (emu->cart) fgb_cart_destroy(emu->cart);
    if (emu->ppu) fgb_ppu_destroy(emu->ppu);
    if (emu->apu) fgb_apu_destroy(emu->apu);
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
    fgb_apu_reset(emu->apu);
}

void fgb_emu_set_log_level(fgb_emu* emu, int level) {
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
