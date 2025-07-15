#ifndef FGB_MEMORY_H
#define FGB_MEMORY_H

#include <stdbool.h>
#include <stdint.h>

#include "cart.h"
#include "timer.h"
#include "io.h"
#include "ppu.h"

#define FGB_MEMORY_SIZE 0x10000 // 64KB mmu size
#define FGB_WRAM_SIZE   (0x1000 + 0x1000) // 2x 8 KiB Banks
#define FGB_HRAM_SIZE   0x7F


typedef struct fgb_mmu {
    union {
        struct {
            uint8_t* ext_data;
            size_t ext_data_size;
        };
        struct {
            uint8_t wram[FGB_WRAM_SIZE];
            uint8_t hram[FGB_HRAM_SIZE];
        };
    };

    fgb_cart* cart;
    fgb_timer* timer;
    fgb_io* io;
    fgb_ppu* ppu;
    struct fgb_cpu* cpu;

    void (*reset)(struct fgb_mmu* mmu);
    void (*write_u8)(struct fgb_mmu* mmu, uint16_t addr, uint8_t value);
    uint8_t(*read_u8)(const struct fgb_mmu* mmu, uint16_t addr);
    uint16_t(*read_u16)(const struct fgb_mmu* mmu, uint16_t addr);
    bool use_ext_data;
} fgb_mmu;

typedef struct fgb_mem_ops {
    void (*reset)(fgb_mmu* mmu);
    void (*write_u8)(fgb_mmu* mmu, uint16_t addr, uint8_t value);
    uint8_t (*read_u8)(const fgb_mmu* mmu, uint16_t addr);
    uint16_t (*read_u16)(const fgb_mmu* mmu, uint16_t addr);
    uint8_t* data; // Can be NULL if not used
    size_t data_size;
} fgb_mmu_ops;

void fgb_mmu_init(fgb_mmu* mmu, fgb_cart* cart, struct fgb_cpu* cpu, const fgb_mmu_ops* ops); // ops may be NULL

#endif // FGB_MEMORY_H
