#include "mmu.h"

#include <string.h>


// -------------- Memory Map --------------
// 0000 - 3FFF  16 KiB ROM bank 00              From cartridge, usually a fixed bank
// 4000 - 7FFF  16 KiB ROM Bank 01–NN           From cartridge, switchable bank via mapper (if any)
// 8000 - 9FFF  8 KiB Video RAM (VRAM)          In CGB mode, switchable bank 0/1
// A000 - BFFF  8 KiB External RAM              From cartridge, switchable bank if any
// C000 - CFFF  4 KiB Work RAM(WRAM)
// D000 - DFFF  4 KiB Work RAM(WRAM)            In CGB mode, switchable bank 1–7
// E000 - FDFF  Echo RAM(mirror of C000–DDFF)   Nintendo says use of this area is prohibited.
// FE00 - FE9F  Object attribute memory(OAM)
// FEA0 - FEFF  Not Usable                      Nintendo says use of this area is prohibited.
// FF00 - FF7F  I / O Registers
// FF80 - FFFE  High RAM(HRAM)
// FFFF - FFFF  Interrupt Enable register (IE)
// ----------------------------------------

#define MEMORY_RANGE_CART_START 0x0000
#define MEMORY_RANGE_CART_END   0x8000


static void fgb_mmu_reset(fgb_mmu* mmu);
static void fgb_mmu_write(fgb_mmu* mmu, uint16_t addr, uint8_t value);
static uint8_t fgb_mmu_read(const fgb_mmu* mmu, uint16_t addr);
static uint16_t fgb_mmu_read_u16(const fgb_mmu* mmu, uint16_t addr);

void fgb_mmu_init(fgb_mmu* mmu, fgb_cart* cart, const fgb_mmu_ops* ops) {
    mmu->use_ext_data = false;
    mmu->cart = cart;

    if (ops) {
        mmu->reset = ops->reset;
        mmu->write_u8 = ops->write_u8;
        mmu->read_u8 = ops->read_u8;
        mmu->read_u16 = ops->read_u16;

        if (ops->data) {
            mmu->ext_data = ops->data;
            mmu->ext_data_size = ops->data_size;
            mmu->use_ext_data = true;
        }
    } else {
        mmu->reset = fgb_mmu_reset;
        mmu->write_u8 = fgb_mmu_write;
        mmu->read_u8 = fgb_mmu_read;
        mmu->read_u16 = fgb_mmu_read_u16;
    }
}

void fgb_mmu_reset(fgb_mmu* mmu) {
    if (mmu->use_ext_data) {
        memset(mmu->ext_data, 0, mmu->ext_data_size);
    } else {
        memset(mmu->data, 0, sizeof(mmu->data));
    }
}

static void fgb_mmu_write(fgb_mmu* mmu, uint16_t addr, uint8_t value) {
    if (addr < MEMORY_RANGE_CART_END) {
        fgb_cart_write(mmu->cart, addr - MEMORY_RANGE_CART_START, value);
        return;
    }

    mmu->data[addr] = value;
}

static uint8_t fgb_mmu_read(const fgb_mmu* mmu, uint16_t addr) {
    if (addr < MEMORY_RANGE_CART_END) {
        return fgb_cart_read(mmu->cart, addr - MEMORY_RANGE_CART_START);
    }
    
    return mmu->data[addr];
}

static uint16_t fgb_mmu_read_u16(const fgb_mmu* mmu, uint16_t addr) {
    const uint16_t lower = fgb_mmu_read(mmu, addr);
    const uint16_t upper = fgb_mmu_read(mmu, addr + 1);
    return upper << 8 | lower;
}
