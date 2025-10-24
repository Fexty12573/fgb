#include "mmu.h"
#include "cpu.h"

#include <string.h>

#include "ulog.h"

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


static void fgb_mmu_reset(fgb_mmu* mmu);
static void fgb_mmu_write(fgb_mmu* mmu, uint16_t addr, uint8_t value);
static uint8_t fgb_mmu_read(const fgb_mmu* mmu, uint16_t addr);
static uint16_t fgb_mmu_read_u16(const fgb_mmu* mmu, uint16_t addr);

void fgb_mmu_init(fgb_mmu* mmu, fgb_cart* cart, fgb_cpu* cpu, const fgb_mmu_ops* ops) {
    mmu->use_ext_data = false;
    mmu->cart = cart;
    mmu->timer = &cpu->timer;
    mmu->io = &cpu->io;
    mmu->ppu = cpu->ppu;
    mmu->cpu = cpu;

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
    
}

static void fgb_mmu_write(fgb_mmu* mmu, uint16_t addr, uint8_t value) {
    // Cart
    if (addr < 0x8000) {
        fgb_cart_write(mmu->cart, addr, value);
        return;
    }

    // VRAM (>= 0x8000)
    if (addr < 0xA000) {
        fgb_ppu_write_vram(mmu->ppu, addr - 0x8000, value);
        return;
    }

    // External RAM Bank (>= 0xA000)
    if (addr < 0xC000) {
        fgb_cart_write(mmu->cart, addr, value);
        return;
    }

    // WRAM (>= 0xC000)
    if (addr < 0xE000) {
        mmu->wram[addr - 0xC000] = value;
        return;
    }

    // ECHO RAM (>= 0xE000)
    if (addr < 0xFE00) {
        // Maps directly to C000 - DDFF
        mmu->wram[addr - 0xE000] = value;
        return;
    }

    // OAM (>= 0xFE00)
    if (addr < 0xFE9F) {
        fgb_ppu_write_oam(mmu->ppu, addr - 0xFE00, value);
        return;
    }

    if (addr >= 0xFF04 && addr <= 0xFF07) {
        fgb_timer_write(mmu->timer, addr, value);
        return;
    }

    if (addr >= 0xFF40 && addr < 0xFF50) {
        fgb_ppu_write(mmu->ppu, addr, value);
        return;
    }

    if (addr == 0xFFFF || addr == 0xFF0F) {
        fgb_cpu_write(mmu->cpu, addr, value);
        return;
    }

    if (addr >= 0xFF00 && addr < 0xFF80) {
        fgb_io_write(mmu->io, addr, value);
        return;
    }

    // HRAM
    if (addr >= 0xFF80 && addr < 0xFFFF) {
        mmu->hram[addr - 0xFF80] = value;
        return;
    }

    log_error("Unmapped memory write to 0x%04X", addr);
}

static uint8_t fgb_mmu_read(const fgb_mmu* mmu, uint16_t addr) {
    // Cart
    if (addr < 0x8000) {
        return fgb_cart_read(mmu->cart, addr);
    }

    // VRAM (>= 0x8000)
    if (addr < 0xA000) {
        return fgb_ppu_read_vram(mmu->ppu, addr - 0x8000);
    }

    // External RAM Bank (>= 0xA000)
    if (addr < 0xC000) {
        return fgb_cart_read(mmu->cart, addr);
    }

    // WRAM (>= 0xC000)
    if (addr < 0xE000) {
        return mmu->wram[addr - 0xC000];
    }

    // ECHO RAM (>= 0xE000)
    if (addr < 0xFE00) {
        // Maps directly to C000 - DDFF
        return mmu->wram[addr - 0xE000];
    }

    // OAM (>= 0xFE00)
    if (addr < 0xFEA0) {
        return fgb_ppu_read_oam(mmu->ppu, addr - 0xFE00);
    }

    if (addr >= 0xFF04 && addr <= 0xFF07) {
        return fgb_timer_read(mmu->timer, addr);
    }

    if (addr >= 0xFF40 && addr < 0xFF50) {
        return fgb_ppu_read(mmu->ppu, addr);
    }

    if (addr == 0xFFFF || addr == 0xFF0F) {
        return fgb_cpu_read(mmu->cpu, addr);
    }

    if (addr >= 0xFF00 && addr < 0xFF80) {
        return fgb_io_read(mmu->io, addr);
    }

    // HRAM
    if (addr >= 0xFF80 && addr < 0xFFFF) {
        return mmu->hram[addr - 0xFF80];
    }

    log_error("Unmapped memory read from 0x%04X", addr);
    return 0xAA; // Return a default value for unmapped reads
}

static uint16_t fgb_mmu_read_u16(const fgb_mmu* mmu, uint16_t addr) {
    const uint16_t lower = fgb_mmu_read(mmu, addr);
    const uint16_t upper = fgb_mmu_read(mmu, addr + 1);
    return upper << 8 | lower;
}
