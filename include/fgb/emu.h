#ifndef FGB_EMU_H
#define FGB_EMU_H

#include "cart.h"
#include "cpu.h"
#include "mmu.h"


typedef struct fgb_emu {
    fgb_cpu* cpu;
    fgb_mmu* mmu;
    fgb_cart* cart;
} fgb_emu;


fgb_emu* fgb_emu_create(const uint8_t* cart_data, size_t cart_size);
void fgb_emu_destroy(fgb_emu* emu);


#endif // FGB_EMU_H
