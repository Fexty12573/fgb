#include "ppu.h"
#include "cpu.h"

#include <stdlib.h>
#include <string.h>

#include <ulog.h>

#define OAM_SCAN_CYCLES             (80)  // T-cycles
#define SCANLINE_CYCLES             (172) // T-cycles
#define HBLANK_CYCLES               (204) // T-cycles
#define VBLANK_CYCLES               (456) // T-cycles

#define TILE_MAP_BASE               (0x9800 - 0x8000)
#define TILE_MAP_WIDTH              32
#define TILE_MAP_HEIGHT             32
#define TILE_MAP_SIZE               (TILE_MAP_WIDTH * TILE_MAP_HEIGHT * 1)
#define TILE_MAP_OFFSET(MAP)        (TILE_MAP_BASE + (MAP) * TILE_MAP_SIZE)
#define TILE_OFFSET(MAP, X, Y)      (TILE_MAP_OFFSET(MAP) + (Y) * TILE_MAP_WIDTH + (X))

#define TILE_DATA_BLOCK_BASE             (0x8000 - 0x8000)
#define TILE_DATA_BLOCK_OFFSET(BLOCK)    (TILE_DATA_BLOCK_BASE + (BLOCK) * TILE_BLOCK_SIZE)
#define TILE_DATA_OFFSET(BLOCK, TILE)    (TILE_DATA_BLOCK_OFFSET(BLOCK) + (TILE) * TILE_SIZE_BYTES)

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

static const fgb_tile* fgb_ppu_get_tile_data(const fgb_ppu* ppu, int tile_map, int tile_block, int x, int y) {
    const int tile_id = ppu->vram[TILE_OFFSET(tile_map, x, y)];
    return (fgb_tile*)&ppu->vram[TILE_DATA_OFFSET(tile_block, tile_id)];
}

static uint32_t fgb_ppu_get_color(const fgb_ppu* ppu, uint8_t pixel_index) {
    return ppu->palette.colors[(ppu->bgp.value >> (pixel_index * 2)) & 0x3];
}

static void fgb_ppu_render_pixels(fgb_ppu* ppu, int count);

fgb_ppu* fgb_ppu_create(void) {
    fgb_ppu* ppu = malloc(sizeof(fgb_ppu));
    if (!ppu) {
        log_error("Failed to allocate PPU");
        return NULL;
    }

    memset(ppu, 0, sizeof(fgb_ppu));

    ppu->palette.colors[0] = 0xFFFFFFFF; // Color 0: White
    ppu->palette.colors[1] = 0xFFB0B0B0; // Color 1: Light Gray
    ppu->palette.colors[2] = 0xFF606060; // Color 2: Dark Gray
    ppu->palette.colors[3] = 0xFF000000; // Color 3: Black

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
        if (ppu->mode_cycles >= OAM_SCAN_CYCLES) {
            ppu->mode_cycles -= OAM_SCAN_CYCLES;
            ppu->stat.mode = PPU_MODE_DRAW;
            ppu->pixels_drawn = 0; // Reset pixel count for the new scanline
        }
        break;

    case PPU_MODE_DRAW:
        // We draw `cycles` pixels for each call to fgb_ppu_tick
        fgb_ppu_render_pixels(ppu, cycles);

        if (ppu->mode_cycles >= SCANLINE_CYCLES) {
            ppu->mode_cycles -= SCANLINE_CYCLES;
            ppu->ly++;

            if (ppu->lyc == ppu->ly) {
                ppu->stat.lyc_eq_ly = 1;
                ppu->stat.lyc_int = 1;
            } else {
                ppu->stat.lyc_eq_ly = 0;
            }

            if (ppu->ly == 144) {
                ppu->stat.mode = PPU_MODE_VBLANK;
                ppu->stat.mode_1_int = 1; // Set mode 1 interrupt flag
                fgb_cpu_request_interrupt(ppu->cpu, IRQ_VBLANK);
            }
            else {
                ppu->stat.mode = PPU_MODE_HBLANK;
                ppu->stat.mode_0_int = 1; // Set mode 0 interrupt flag
            }
        }
        break;

    case PPU_MODE_HBLANK:
        if (ppu->mode_cycles >= HBLANK_CYCLES) {
            ppu->mode_cycles -= HBLANK_CYCLES;
            ppu->stat.mode = PPU_MODE_OAM_SCAN;
            ppu->stat.mode_2_int = 1; // Set mode 2 interrupt flag
        }
        break;

    case PPU_MODE_VBLANK:
        if (ppu->mode_cycles >= VBLANK_CYCLES) {
            ppu->mode_cycles -= VBLANK_CYCLES;
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

    case 0xFF47:
        ppu->bgp.value = value;
        break;

    case 0xFF48:
        ppu->obp0.value = value;
        break;

    case 0xFF49:
        ppu->obp1.value = value;
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

    case 0xFF47:
        return ppu->bgp.value;

    case 0xFF48:
        return ppu->obp0.value;

    case 0xFF49:
        return ppu->obp1.value;

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
    //if (ppu->stat.mode == PPU_MODE_DRAW) {
    //    log_trace("PPU: Attempt to write to VRAM during MODE_DRAW");
    //    return;
    //}

    ppu->vram[addr] = value;
}

uint8_t fgb_ppu_read_vram(const fgb_ppu* ppu, uint16_t addr) {
    //if (ppu->stat.mode == PPU_MODE_DRAW) {
    //    log_warn("PPU: Attempt to read from VRAM during MODE_DRAW");
    //    return 0xFF;
    //}

    return ppu->vram[addr];
}

void fgb_ppu_write_oam(fgb_ppu* ppu, uint16_t addr, uint8_t value) {
    ppu->oam[addr] = value;
}

uint8_t fgb_ppu_read_oam(const fgb_ppu* ppu, uint16_t addr) {
    return ppu->oam[addr];
}

static void fgb_ppu_render_pixels(fgb_ppu* ppu, int count) {
    // Make sure we don't draw more pixels than the screen width
    count = min(count, SCREEN_WIDTH - ppu->pixels_drawn);

    for (int i = 0; i < count; i++) {
        const int x = ppu->pixels_drawn + i;
        const int y = ppu->ly;
        if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT) {
            break; // Prevent drawing outside the screen
        }

        // TODO: Add scrolling
        const int tile_x = x / TILE_WIDTH;
        const int tile_y = y / TILE_HEIGHT;

        // Get the background tile data
        const fgb_tile* tile = fgb_ppu_get_tile_data(ppu, ppu->lcd_control.bg_tile_map, 0, tile_x, tile_y);
        if (!tile) {
            log_error("PPU: Failed to get tile data for (%d, %d)", tile_x, tile_y);
            continue;
        }

        // Get the pixel from the tile
        const int pixel_x = x % TILE_WIDTH;
        const int pixel_y = y % TILE_HEIGHT;
        uint8_t pixel = fgb_ppu_get_pixel(ppu, tile, pixel_x, pixel_y);
        ppu->screen[y * SCREEN_WIDTH + x] = fgb_ppu_get_color(ppu, pixel);
    }

    ppu->pixels_drawn += count;
}
