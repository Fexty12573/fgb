#include "mmu.h"

#include <string.h>

static void fgb_mmu_reset(fgb_mmu* mmu);
static void fgb_mmu_write(fgb_mmu* mmu, uint16_t addr, uint8_t value);
static uint8_t fgb_mmu_read(const fgb_mmu* mmu, uint16_t addr);
static uint16_t fgb_mmu_read_u16(const fgb_mmu* mmu, uint16_t addr);

void fgb_mmu_init(fgb_mmu* mmu, const fgb_mmu_ops* ops) {
    mmu->use_ext_data = false;

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
    // Will do memory mapping later
    mmu->data[addr] = value;
}

static uint8_t fgb_mmu_read(const fgb_mmu* mmu, uint16_t addr) {
    return mmu->data[addr];
}

static uint16_t fgb_mmu_read_u16(const fgb_mmu* mmu, uint16_t addr) {
    const uint16_t lower = fgb_mmu_read(mmu, addr);
    const uint16_t upper = fgb_mmu_read(mmu, addr + 1);
    return upper << 8 | lower;
}
