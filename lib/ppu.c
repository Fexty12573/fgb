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

static void fgb_ppu_render_pixels(fgb_ppu* ppu, int count);
static void fgb_ppu_do_oam_scan(fgb_ppu* ppu);

fgb_ppu* fgb_ppu_create(void) {
    fgb_ppu* ppu = malloc(sizeof(fgb_ppu));
    if (!ppu) {
        log_error("Failed to allocate PPU");
        return NULL;
    }

    memset(ppu, 0, sizeof(fgb_ppu));

    mtx_init(&ppu->buffer_mutex, mtx_plain);

    ppu->bg_palette.colors[0] = 0xFFFFFFFF; // Color 0: White
    ppu->bg_palette.colors[1] = 0xFFB0B0B0; // Color 1: Light Gray
    ppu->bg_palette.colors[2] = 0xFF606060; // Color 2: Dark Gray
    ppu->bg_palette.colors[3] = 0xFF000000; // Color 3: Black

    ppu->obj_palette.colors[0] = 0xFFFFFFFF; // Color 0: White
    ppu->obj_palette.colors[1] = 0xFFB0B0B0; // Color 1: Light Gray
    ppu->obj_palette.colors[2] = 0xFF606060; // Color 2: Dark Gray
    ppu->obj_palette.colors[3] = 0xFF000000; // Color 3: Black

    return ppu;
}

void fgb_ppu_destroy(fgb_ppu* ppu) {
    ppu->cpu = NULL;
    free(ppu);
}

void fgb_ppu_set_cpu(fgb_ppu* ppu, fgb_cpu* cpu) {
    ppu->cpu = cpu;
}

void fgb_ppu_reset(fgb_ppu* ppu) {
    memset(ppu->vram, 0, sizeof(ppu->vram));
    memset(ppu->oam, 0, sizeof(ppu->oam));
    memset(ppu->framebuffers, 0, sizeof(ppu->framebuffers));
    memset(ppu->line_sprites, 0, sizeof(ppu->line_sprites));

    ppu->back_buffer = 0;
    ppu->mode_cycles = 0;
    ppu->frame_cycles = 0;
    ppu->pixels_drawn = 0;
    ppu->sprite_count = 0;

    ppu->oam_scan_done = false;
    ppu->frames_rendered = 0;
    ppu->lcd_control.value = 0x00;
    ppu->ly = 0;
    ppu->lyc = 0;
    ppu->stat.value = 0x00;
    ppu->scroll.x = 0;
    ppu->scroll.y = 0;
    ppu->window_pos.x = 0;
    ppu->window_pos.y = 0;
    ppu->bgp.value = 0x00;
    ppu->obp[0].value = 0x00;
    ppu->obp[1].value = 0x00;
    ppu->debug.hide_bg = false;
    ppu->debug.hide_sprites = false;
    ppu->debug.hide_window = false;
    ppu->dma_active = false;
    ppu->dma = 0;
    ppu->dma_addr = 0;
    ppu->dma_bytes = 0;
	ppu->dma_cycles = 0;
}

const uint32_t* fgb_ppu_get_front_buffer(const fgb_ppu* ppu) {
    return ppu->framebuffers[(ppu->back_buffer + PPU_FRAMEBUFFER_COUNT - 1) % PPU_FRAMEBUFFER_COUNT];
}

void fgb_ppu_lock_buffer(fgb_ppu* ppu) {
    if (mtx_lock(&ppu->buffer_mutex) != thrd_success) {
        log_error("PPU: Failed to lock buffer mutex");
    }
}

void fgb_ppu_unlock_buffer(fgb_ppu* ppu) {
    if (mtx_unlock(&ppu->buffer_mutex) != thrd_success) {
        log_error("PPU: Failed to unlock buffer mutex");
    }
}

uint8_t fgb_tile_get_pixel(const fgb_tile* tile, uint8_t x, uint8_t y) {
    const uint8_t lsb = tile->data[y * 2];
    const uint8_t msb = tile->data[y * 2 + 1];
    return ((msb >> (7 - x)) & 1) << 1 | ((lsb >> (7 - x)) & 1);
}

int fgb_ppu_get_tile_id(const fgb_ppu* ppu, int tile_map, int x, int y) {
    return ppu->vram[TILE_OFFSET(tile_map, x, y)];
}

const fgb_tile* fgb_ppu_get_tile_data(const fgb_ppu* ppu, int tile_id, bool is_sprite) {
    if (is_sprite || ppu->lcd_control.bg_wnd_tiles == 1) {
        // Use tile blocks 0 and 1
        return (fgb_tile*)&ppu->vram[TILE_DATA_OFFSET(0, tile_id)];
    }

    int tile_block = tile_id > 127 ? 2 : 1; // Tile ID > 127 uses block 2, otherwise block 1
    return (fgb_tile*)&ppu->vram[TILE_DATA_OFFSET(tile_block, tile_id % 128)];
}

uint32_t fgb_ppu_get_bg_color(const fgb_ppu* ppu, uint8_t pixel_index) {
    return ppu->bg_palette.colors[(ppu->bgp.value >> (pixel_index * 2)) & 0x3];
}

uint32_t fgb_ppu_get_obj_color(const fgb_ppu* ppu, uint8_t pixel_index, int palette) {
    const int pal_index = (ppu->obp[palette].value >> (pixel_index * 2)) & 0x3;
    return ppu->obj_palette.colors[pal_index];
}

bool fgb_ppu_tick(fgb_ppu* ppu, uint32_t cycles) {
    if (!ppu->cpu) {
        log_error("PPU: CPU not set");
        return false;
    }

    ppu->mode_cycles += cycles;
    ppu->frame_cycles += cycles;
    ppu->dma_cycles += cycles;

    if (ppu->dma_active && ppu->dma_cycles >= 4) {
        const int bytes_to_transfer = min(ppu->dma_cycles / 4, PPU_DMA_BYTES - ppu->dma_bytes);
        ppu->dma_cycles -= bytes_to_transfer * 4;

        const fgb_mmu* mmu = &ppu->cpu->mmu;

        for (int i = 0; i < bytes_to_transfer; i++) {
            const uint16_t src = ppu->dma_addr + ppu->dma_bytes + i;
            const uint16_t dst = (ppu->dma_bytes + i) % PPU_OAM_SIZE;
            ppu->oam[dst] = mmu->read_u8(mmu, src);
        }

        ppu->dma_bytes += bytes_to_transfer;

        if (ppu->dma_bytes >= PPU_DMA_BYTES) {
            ppu->dma_active = false; // DMA transfer complete
        }
    }

    switch (ppu->stat.mode) {
    case PPU_MODE_OAM_SCAN:
        fgb_ppu_do_oam_scan(ppu);
        if (ppu->mode_cycles >= OAM_SCAN_CYCLES) {
            ppu->mode_cycles -= OAM_SCAN_CYCLES;
            ppu->oam_scan_done = false; // Set this for the next scanline
            ppu->stat.mode = PPU_MODE_DRAW;
            ppu->pixels_drawn = 0; // Reset pixel count for the new scanline

            if (ppu->mode_cycles > 0) {
                // Immediately render pixels if there are leftover cycles
                fgb_ppu_render_pixels(ppu, ppu->mode_cycles);
            }
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
                if (ppu->stat.lyc_int) {
                    fgb_cpu_request_interrupt(ppu->cpu, IRQ_LCD);
                }
            } else {
                ppu->stat.lyc_eq_ly = 0;
            }

            ppu->stat.mode = PPU_MODE_HBLANK;
            if (ppu->stat.hblank_int) {
                fgb_cpu_request_interrupt(ppu->cpu, IRQ_LCD);
            }
        }
        break;

    case PPU_MODE_HBLANK:
        if (ppu->mode_cycles >= HBLANK_CYCLES) {
            ppu->mode_cycles -= HBLANK_CYCLES;

            if (ppu->ly == 144) {
                ppu->stat.mode = PPU_MODE_VBLANK;
                fgb_cpu_request_interrupt(ppu->cpu, IRQ_VBLANK);

                if (ppu->stat.vblank_int) {
                    fgb_cpu_request_interrupt(ppu->cpu, IRQ_LCD);
                }
            } else {
                ppu->stat.mode = PPU_MODE_OAM_SCAN;
                if (ppu->stat.oam_int) {
                    fgb_cpu_request_interrupt(ppu->cpu, IRQ_LCD);
                }
            }
        }
        break;

    case PPU_MODE_VBLANK:
        if (ppu->mode_cycles >= VBLANK_CYCLES) {
            ppu->mode_cycles -= VBLANK_CYCLES;
            ppu->ly++;
            if (ppu->ly >= 154) {
                ppu->ly = 0;
                ppu->stat.mode = PPU_MODE_OAM_SCAN;
                ppu->frame_cycles = 0; // Reset frame cycles
                ppu->frames_rendered++;

                fgb_ppu_lock_buffer(ppu);

                // Switch to the next framebuffer at the end of VBLANK
                ppu->back_buffer = (ppu->back_buffer + 1) % PPU_FRAMEBUFFER_COUNT;

                fgb_ppu_unlock_buffer(ppu);

                // Clear the line sprites for the next frame
                memset(ppu->line_sprites, 0xFF, sizeof(ppu->line_sprites));

                if (ppu->stat.oam_int) {
                    fgb_cpu_request_interrupt(ppu->cpu, IRQ_LCD);
                }

                return true;
            }
        }
        break;
    default:
        log_error("PPU: Unknown mode %d", ppu->stat.mode);
        ppu->stat.mode = PPU_MODE_OAM_SCAN; // Reset to a known state
        break;
    }

    return false;
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

    case 0xFF46:
		ppu->dma = value;

        if (ppu->dma_active) {
            log_warn("PPU: DMA transfer already active, ignoring new request");
            return;
        }

        ppu->dma_active = true;
        ppu->dma_addr = (uint16_t)value << 8;
        ppu->dma_cycles = 0;
        ppu->dma_bytes = 0;

        log_trace("PPU: Starting DMA transfer during mode %d", ppu->stat.mode);
        break;

    case 0xFF47:
        ppu->bgp.value = value;
        break;

    case 0xFF48:
        ppu->obp[0].value = value;
        break;

    case 0xFF49:
        ppu->obp[1].value = value;
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

    case 0xFF46:
		return ppu->dma;

    case 0xFF47:
        return ppu->bgp.value;

    case 0xFF48:
        return ppu->obp[0].value;

    case 0xFF49:
        return ppu->obp[1].value;

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
    //if (ppu->stat.mode == PPU_MODE_OAM_SCAN || ppu->stat.mode == PPU_MODE_DRAW) {
    //    log_warn("PPU: Attempt to write to OAM during OAM scan");
    //    return;
    //}

    ppu->oam[addr] = value;
}

uint8_t fgb_ppu_read_oam(const fgb_ppu* ppu, uint16_t addr) {
    //if (ppu->stat.mode == PPU_MODE_OAM_SCAN || ppu->stat.mode == PPU_MODE_DRAW) {
    //    log_warn("PPU: Attempt to read from OAM during OAM scan");
    //    return 0xFF;
    //}

    return ppu->oam[addr];
}

static void fgb_ppu_render_pixels(fgb_ppu* ppu, int count) {
    // Make sure we don't draw more pixels than the screen width
    count = min(count, SCREEN_WIDTH - ppu->pixels_drawn);

    uint32_t* framebuffer = ppu->framebuffers[ppu->back_buffer];

    const int scx = ppu->scroll.x;
    const int scy = ppu->scroll.y;

	const int wx = ppu->window_pos.x >= 7 ? ppu->window_pos.x - 7 : 0;
	const int wy = ppu->window_pos.y;

    for (int i = 0; i < count; i++) {
        const int sx = ppu->pixels_drawn + i;
        const int sy = ppu->ly;
        if (sx >= SCREEN_WIDTH || sy >= SCREEN_HEIGHT) {
            break; // Prevent drawing outside the screen
        }

        const int bx = (sx + scx) & 0xFF;
		const int by = (sy + scy) & 0xFF;

		const int tile_x = (bx / TILE_WIDTH) % TILE_MAP_WIDTH;
		const int tile_y = (by / TILE_HEIGHT) % TILE_MAP_HEIGHT;

        // Check if there is a sprite at this position
        fgb_sprite* sprite = NULL;
        uint8_t sprite_pixel = 0;
        uint8_t sprite_index = 0xFF;
        if (ppu->lcd_control.obj_enable) {
            for (int j = 0; j < ppu->sprite_count; j++) {
                fgb_sprite* s = (fgb_sprite*)&ppu->oam[ppu->sprite_buffer[j]];
                const int sprite_window_x = s->x - 8; // Adjust for sprite X position
                const int sprite_window_y = s->y - 16;
                if (sx >= sprite_window_x && sx < sprite_window_x + PPU_SPRITE_W) { // Check if the pixel is within the sprite's X bounds
                    sprite = s;
                    sprite_index = ppu->sprite_buffer[j];

                    // If obj_size == 1 (8x16 sprites), the lsb of the tile ID is ignored
                    const int tile_id = ppu->lcd_control.obj_size ? s->tile & 0xFE : s->tile;
                    const fgb_tile* tile = fgb_ppu_get_tile_data(ppu, tile_id, true);
                    if (!tile) {
                        log_error("PPU: Failed to get sprite tile data for tile ID %d", tile_id);
                        continue;
                    }

                    const uint8_t pixel_x = (sx - sprite_window_x) % TILE_WIDTH;
                    const uint8_t pixel_y = (sy - sprite_window_y) % TILE_HEIGHT;
                    sprite_pixel = fgb_tile_get_pixel(tile, pixel_x, pixel_y);
                    break;
                }
            }
        }

        // Get the background tile data
        const int tile_id = fgb_ppu_get_tile_id(ppu, ppu->lcd_control.bg_tile_map, tile_x, tile_y);
        const fgb_tile* tile = fgb_ppu_get_tile_data(ppu, tile_id, false);
        if (!tile) {
            log_error("PPU: Failed to get tile data for (%d, %d)", tile_x, tile_y);
            continue;
        }

        // Get the pixel from the tile
		const uint8_t pixel_x = bx % TILE_WIDTH;
		const uint8_t pixel_y = by % TILE_HEIGHT;
        uint8_t bg_pixel = fgb_tile_get_pixel(tile, pixel_x, pixel_y);
        const int screen_index = sy * SCREEN_WIDTH + sx;

        if (ppu->lcd_control.wnd_enable && sx >= wx && sy >= wy) {
			const int rel_window_x = sx - wx;
			const int rel_window_y = sy - wy;
            const uint8_t window_x = rel_window_x % TILE_WIDTH;
            const uint8_t window_y = rel_window_y % TILE_HEIGHT;
            const int window_tile_x = (rel_window_x / TILE_WIDTH) % TILE_MAP_WIDTH;
            const int window_tile_y = (rel_window_y / TILE_HEIGHT) % TILE_MAP_HEIGHT;

            const int window_tile_id = fgb_ppu_get_tile_id(ppu, ppu->lcd_control.wnd_tile_map, window_tile_x, window_tile_y);
            const fgb_tile* window_tile = fgb_ppu_get_tile_data(ppu, window_tile_id, false);
            if (window_tile) {
                bg_pixel = fgb_tile_get_pixel(window_tile, window_x, window_y);
            } else {
                log_error("PPU: Failed to get window tile data for tile ID %d", window_tile_id);
			}
        }

        if (!sprite || ppu->debug.hide_sprites) {
            if (ppu->debug.hide_bg) {
                framebuffer[screen_index] = 0x00000000; // Black
            } else if (!ppu->lcd_control.bg_wnd_enable) {
				framebuffer[screen_index] = 0xFFFFFFFF; // White
            } else {
                // No sprite at this position, just draw the background pixel
                framebuffer[screen_index] = fgb_ppu_get_bg_color(ppu, bg_pixel);
            }

            continue;
        }

        // Sprite color 0 is transparent
        // Sprite priority 1 means it is behind bg colors 1, 2 and 3
        if (sprite_pixel == 0 || (sprite->priority == 1 && bg_pixel != 0)) {
            framebuffer[screen_index] = fgb_ppu_get_bg_color(ppu, bg_pixel);
            continue;
        }

        for (int i = 0; i < PPU_SCANLINE_SPRITES; i++) {
            if (ppu->line_sprites[ppu->ly][i] == sprite_index) {
                break;
            }
            if (ppu->line_sprites[ppu->ly][i] == 0xFF) {
                // Found an empty slot, store the sprite index
                ppu->line_sprites[ppu->ly][i] = sprite_index;
                break;
            }
        }
        
        framebuffer[screen_index] = fgb_ppu_get_obj_color(ppu, sprite_pixel, sprite->palette);
    }

    ppu->pixels_drawn += count;
}

void fgb_ppu_do_oam_scan(fgb_ppu* ppu) {
    if (ppu->oam_scan_done) {
        return;
    }

    if (ppu->dma_active) {
        return; // Wait for DMA to finish
    }

    ppu->sprite_count = 0;

    const int sprite_height = ppu->lcd_control.obj_size ? PPU_SPRITE_H16 : PPU_SPRITE_H;

    for (int i = 0; i < PPU_OAM_SPRITES; i++) {
        const fgb_sprite* sprite = (const fgb_sprite*)&ppu->oam[i * PPU_SPRITE_SIZE_BYTES];
        if (sprite->x == 0) continue; // Sprite is not visible
        if (ppu->ly < sprite->y - 16 || ppu->ly >= sprite->y - 16 + sprite_height) continue; // Sprite is not on this scanline

        ppu->sprite_buffer[ppu->sprite_count++] = i * PPU_SPRITE_SIZE_BYTES;
        
        if (ppu->sprite_count >= PPU_SCANLINE_SPRITES) {
            break;
        }
    }

    ppu->oam_scan_done = true;
}
