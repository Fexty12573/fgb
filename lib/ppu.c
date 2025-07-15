#include "ppu.h"
#include "cpu.h"

#include <stdlib.h>
#include <string.h>

#include <ulog.h>

#define FGB_PPU_OAM_SCAN_CYCLES (80)  // T-cycles
#define FGB_PPU_SCANLINE_CYCLES (172) // T-cycles
#define FGB_PPU_HBLANK_CYCLES   (204) // T-cycles
#define FGB_PPU_VBLANK_CYCLES   (456) // T-cycles


typedef struct fgb_sprite {
    uint8_t y;
    uint8_t x;
    uint8_t tile;
    union {
        uint8_t flags;
        struct {
            uint8_t : 3; // Palette in CGB mode
            uint8_t bank : 1;
            uint8_t palette : 1;
            uint8_t x_flip : 1;
            uint8_t y_flip : 1;
            uint8_t priority : 1; // 0 = in front of background, 1 = behind background
        };
    };
} fgb_sprite;

typedef struct fgb_tile {
    uint8_t data[16]; // 8x8 tile data, 2 bytes per row
} fgb_tile;

static uint8_t fgb_ppu_get_pixel(const fgb_ppu* ppu, const fgb_tile* tile, uint8_t x, uint8_t y) {
    const uint8_t lsb = tile->data[y * 2];
    const uint8_t msb = tile->data[y * 2 + 1];
    return ((msb >> (7 - x)) & 1) << 1 | ((lsb >> (7 - x)) & 1);
}

fgb_ppu* fgb_ppu_create(void) {
    fgb_ppu* ppu = malloc(sizeof(fgb_ppu));
    if (!ppu) {
        log_error("Failed to allocate PPU");
        return NULL;
    }

    memset(ppu, 0, sizeof(fgb_ppu));

    return ppu;
}

void fgb_ppu_destroy(fgb_ppu* ppu) {
    ppu->cpu = NULL;
    free(ppu);
}

void fgb_ppu_set_cpu(fgb_ppu* ppu, fgb_cpu* cpu) {
    ppu->cpu = cpu;
}

void fgb_ppu_tick(fgb_ppu* ppu, uint32_t cycles) {
    if (!ppu->cpu) {
        log_error("PPU: CPU not set");
        return;
    }

    ppu->mode_cycles += cycles;
    ppu->frame_cycles += cycles;

    switch (ppu->stat.mode) {
    case PPU_MODE_OAM_SCAN:
        if (ppu->mode_cycles >= FGB_PPU_OAM_SCAN_CYCLES) {
            ppu->mode_cycles -= FGB_PPU_OAM_SCAN_CYCLES;
            ppu->stat.mode = PPU_MODE_DRAW;
        }
        break;

    case PPU_MODE_DRAW:
        if (ppu->mode_cycles >= FGB_PPU_SCANLINE_CYCLES) {
            ppu->ly++;
            if (ppu->ly == 144) {
                ppu->stat.mode = PPU_MODE_VBLANK;
                ppu->stat.mode_1_int = 1; // Set mode 1 interrupt flag
            }
            else {
                ppu->stat.mode = PPU_MODE_HBLANK;
                ppu->stat.mode_0_int = 1; // Set mode 0 interrupt flag
            }
        }
        break;

    case PPU_MODE_HBLANK:
        if (ppu->mode_cycles >= FGB_PPU_HBLANK_CYCLES) {
            ppu->mode_cycles -= FGB_PPU_HBLANK_CYCLES;
            ppu->stat.mode = PPU_MODE_OAM_SCAN;
            ppu->stat.mode_2_int = 1; // Set mode 2 interrupt flag
        }
        break;

    case PPU_MODE_VBLANK:
        if (ppu->mode_cycles >= FGB_PPU_VBLANK_CYCLES) {
            ppu->mode_cycles -= FGB_PPU_VBLANK_CYCLES;
            ppu->ly++;
            if (ppu->ly >= 154) {
                ppu->ly = 0;
                ppu->stat.mode = PPU_MODE_OAM_SCAN;
                ppu->stat.mode_0_int = 1; // Set mode 0 interrupt flag
            }
        }
        break;
    }


}

void fgb_ppu_write(fgb_ppu* ppu, uint16_t addr, uint8_t value) {
    switch (addr) {
    case 0xFF40:
        ppu->lcd_control.value = value;
        break;

    case 0xFF41:
        ppu->stat.value = (value & 0xF8) | ppu->stat.read_only;
        break;

    case 0xFF42:
        ppu->scroll.y = value;
        break;

    case 0xFF43:
        ppu->scroll.x = value;
        break;

    case 0xFF44: 
        break; // LY is read-only

    case 0xFF45:
        ppu->lyc = value;
        break;

    case 0xFF4A:
        ppu->window_pos.y = value;
        break;

    case 0xFF4B:
        ppu->window_pos.x = value;
        break;

    default:
        log_warn("Unknown address for PPU write: 0x%04X", addr);
        break;
    }
}

uint8_t fgb_ppu_read(const fgb_ppu* ppu, uint16_t addr) {
    switch (addr) {
    case 0xFF40:
        return ppu->lcd_control.value;

    case 0xFF41:
        return ppu->stat.value;

    case 0xFF42:
        return ppu->scroll.y;

    case 0xFF43:
        return ppu->scroll.x;

    case 0xFF44:
        return ppu->ly;

    case 0xFF45:
        return ppu->lyc;

    case 0xFF4A:
        return ppu->window_pos.y;

    case 0xFF4B:
        return ppu->window_pos.x;

    default:
        log_warn("Unknown address for PPU read: 0x%04X", addr);
        break;
    }

    return 0xAA;
}

// TODO: Add checks for if the memory is even accessible
void fgb_ppu_write_vram(fgb_ppu* ppu, uint16_t addr, uint8_t value) {
    // VRAM is only accessible during VBLANK and HBLANK
    if (ppu->stat.mode != PPU_MODE_VBLANK && ppu->stat.mode != PPU_MODE_HBLANK) {
        log_warn("PPU: Attempt to write to VRAM outside of VBLANK or OAM scan mode");
        return;
    }

    ppu->vram[addr] = value;
}

uint8_t fgb_ppu_read_vram(const fgb_ppu* ppu, uint16_t addr) {
    if (ppu->stat.mode != PPU_MODE_VBLANK && ppu->stat.mode != PPU_MODE_HBLANK) {
        log_warn("PPU: Attempt to read from VRAM outside of VBLANK or OAM scan mode");
        return 0xFF; // Return a default value
    }

    return ppu->vram[addr];
}

void fgb_ppu_write_oam(fgb_ppu* ppu, uint16_t addr, uint8_t value) {
    ppu->oam[addr] = value;
}

uint8_t fgb_ppu_read_oam(const fgb_ppu* ppu, uint16_t addr) {
    return ppu->oam[addr];
}
