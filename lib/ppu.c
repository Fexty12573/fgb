#include "ppu.h"
#include "cpu.h"

#include <stdlib.h>
#include <string.h>

#include <ulog.h>

#define OAM_SCAN_CYCLES             (80)  // T-cycles
#define SCANLINE_CYCLES             (456) // T-cycles
#define VBLANK_CYCLES               SCANLINE_CYCLES
#define HBLANK_MAX_CYCLES           (SCANLINE_CYCLES - OAM_SCAN_CYCLES)

#define TILE_MAP_BASE               (0x9800 - 0x8000)
#define TILE_MAP_WIDTH              32
#define TILE_MAP_HEIGHT             32
#define TILE_MAP_SIZE               (TILE_MAP_WIDTH * TILE_MAP_HEIGHT * 1)
#define TILE_MAP_OFFSET(MAP)        (TILE_MAP_BASE + (MAP) * TILE_MAP_SIZE)
#define TILE_OFFSET(MAP, X, Y)      (TILE_MAP_OFFSET(MAP) + (Y) * TILE_MAP_WIDTH + (X))

#define TILE_DATA_BLOCK_BASE             (0x8000 - 0x8000)
#define TILE_DATA_BLOCK_OFFSET(BLOCK)    (TILE_DATA_BLOCK_BASE + (BLOCK) * TILE_BLOCK_SIZE)
#define TILE_DATA_OFFSET(BLOCK, TILE)    (TILE_DATA_BLOCK_OFFSET(BLOCK) + (TILE) * TILE_SIZE_BYTES)

#define TILE_PIXEL(LSB, MSB, X)    (((((MSB) >> (7 - (X))) & 1) << 1) | (((LSB) >> (7 - (X))) & 1))

static void fgb_ppu_render_pixels(fgb_ppu* ppu, int count);
static void fgb_ppu_do_oam_scan(fgb_ppu* ppu);
static void fgb_ppu_pixel_fetcher_tick(fgb_ppu* ppu);
static void fgb_ppu_lcd_push(fgb_ppu* ppu);
static void fgb_ppu_try_stat_irq(fgb_ppu* ppu);

static void fgb_queue_push(fgb_queue* queue, fgb_pixel pixel);
static fgb_pixel fgb_queue_pop(fgb_queue* queue);
static fgb_pixel* fgb_queue_at(fgb_queue* queue, int index);
static bool fgb_queue_full(const fgb_queue* queue);
static bool fgb_queue_empty(const fgb_queue* queue);
static void fgb_queue_clear(fgb_queue* queue);

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
    memset(ppu->line_sprites, 0xFF, sizeof(ppu->line_sprites));

    ppu->back_buffer = 0;
    ppu->mode_cycles = 0;
    ppu->frame_cycles = 0;
    ppu->scanline_cycles = 0;
    ppu->pixels_drawn = 0;
    ppu->sprite_count = 0;

    fgb_queue_clear(&ppu->bg_wnd_fifo);
	fgb_queue_clear(&ppu->sprite_fifo);

    ppu->reached_window_x = false;
    ppu->reached_window_y = false;
    ppu->window_line_counter = 0;

	ppu->bg_wnd_fetch_step = FETCH_STEP_TILE_0;
	ppu->fetch_tile_id = 0;
	ppu->fetch_x = 0;
	ppu->bg_wnd_tile_lo = 0;
	ppu->bg_wnd_tile_hi = 0;
	ppu->is_first_fetch = true;
	ppu->sprite_fetch_active = false;
	ppu->processed_pixels = 0;
	ppu->framebuffer_x = 0;

    ppu->oam_scan_done = false;
    ppu->reset = false;
    ppu->frames_rendered = 0;
    ppu->lcd_control.value = 0x91;
    ppu->ly = 0;
    ppu->lyc = 0;
    ppu->stat.value = 0x81;
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

void fgb_ppu_swap_buffers(fgb_ppu *ppu) {
    fgb_ppu_lock_buffer(ppu);
    ppu->back_buffer = (ppu->back_buffer + 1) % PPU_FRAMEBUFFER_COUNT;
    fgb_ppu_unlock_buffer(ppu);
}

uint8_t fgb_tile_get_pixel(const fgb_tile* tile, uint8_t x, uint8_t y) {
    const uint8_t lsb = tile->data[y * 2];
    const uint8_t msb = tile->data[y * 2 + 1];
    return ((msb >> (7 - x)) & 1) << 1 | ((lsb >> (7 - x)) & 1);
}

int fgb_ppu_get_tile_id_old(const fgb_ppu* ppu, int tile_map, int x, int y) {
    return ppu->vram[TILE_OFFSET(tile_map, x, y)];
}

int fgb_ppu_get_tile_id(const fgb_ppu* ppu) {
    int offset;
    if (ppu->reached_window_x) {
        offset = ppu->fetch_x + (TILE_MAP_WIDTH * (ppu->window_line_counter / 8));
        offset += TILE_MAP_OFFSET(ppu->lcd_control.wnd_tile_map);
    } else {
        offset = ppu->fetch_x + ((ppu->scroll.x / 8) & 0x1F);
        offset += TILE_MAP_WIDTH * (((ppu->ly + ppu->scroll.y) & 0xFF) / 8);
        offset += TILE_MAP_OFFSET(ppu->lcd_control.bg_tile_map);
    }

    return ppu->vram[offset];
}

int fgb_ppu_get_current_tile_y(const fgb_ppu* ppu) {
    if (ppu->reached_window_x) {
        return 2 * (ppu->window_line_counter % 8);
    } else {
        return 2 * ((ppu->ly + ppu->scroll.y) % 8);
    }
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
    if (!ppu->lcd_control.bg_wnd_enable) {
        return ppu->bg_palette.colors[0]; // Background disabled, always color 0
	}

    return ppu->bg_palette.colors[(ppu->bgp.value >> (pixel_index * 2)) & 0x3];
}

uint32_t fgb_ppu_get_obj_color(const fgb_ppu* ppu, uint8_t pixel_index, int palette) {
    const int pal_index = (ppu->obp[palette].value >> (pixel_index * 2)) & 0x3;
    return ppu->obj_palette.colors[pal_index];
}

bool fgb_ppu_tick(fgb_ppu* ppu) {
    if (!ppu->cpu) {
        log_error("PPU: CPU not set");
        return false;
    }

    if (!ppu->lcd_control.lcd_ppu_enable) {
        // LCD and PPU are disabled
        if (!ppu->reset) {
            ppu->stat.mode = PPU_MODE_HBLANK;
            ppu->ly = 0;
            ppu->mode_cycles = 0;
            ppu->frame_cycles = 0;
            ppu->reset = true;

            fgb_ppu_lock_buffer(ppu);
            memset(ppu->framebuffers, 0xFF, sizeof(ppu->framebuffers));
            fgb_ppu_unlock_buffer(ppu);

            fgb_ppu_swap_buffers(ppu);
        }

        return false;
    } else if (ppu->lcd_control.lcd_ppu_enable && ppu->reset) {
        // LCD/PPU just got enabled, reset state
        ppu->stat.mode = PPU_MODE_OAM_SCAN;
        ppu->mode_cycles = 4; // After turning on LCD, OAM Scan is 4 cycles shorter
        ppu->frame_cycles = 0;
        ppu->framebuffer_x = 0;
        ppu->processed_pixels = 0;
        ppu->fetch_x = 0;
        ppu->is_first_fetch = true;
        fgb_queue_clear(&ppu->bg_wnd_fifo);
        fgb_queue_clear(&ppu->sprite_fifo);
        ppu->reset = false;
    }

    ppu->mode_cycles++;
    ppu->frame_cycles++;
    ppu->dma_cycles++;
    ppu->scanline_cycles++;

    if (ppu->dma_active && ppu->dma_cycles > 4) {
        ppu->oam_blocked = true;

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

    if (!ppu->dma_active && ppu->oam_blocked && ppu->dma_cycles > 4) {
        ppu->oam_blocked = false;
    }

    switch (ppu->stat.mode) {
    case PPU_MODE_OAM_SCAN:
        fgb_ppu_do_oam_scan(ppu);
        if (ppu->mode_cycles >= OAM_SCAN_CYCLES) {
            ppu->mode_cycles -= OAM_SCAN_CYCLES;
            ppu->oam_scan_done = false; // Set this for the next scanline
            ppu->stat.mode = PPU_MODE_DRAW;
            ppu->pixels_drawn = 0; // Reset pixel count for the new scanline
            ppu->sprite_index = 0; // Reset sprite index for fetching

            if (ppu->ly == ppu->window_pos.y) {
                ppu->reached_window_y = true;
            }
        }
        break;

    case PPU_MODE_DRAW:
		fgb_ppu_pixel_fetcher_tick(ppu); // Fetch pixels into the FIFOs
		fgb_ppu_lcd_push(ppu); // Try to push pixels to the framebuffer

        if (ppu->framebuffer_x >= SCREEN_WIDTH) {
			// Reset Fetcher and FIFO state for the next line
            ppu->framebuffer_x = 0;
            ppu->fetch_x = 0;
			ppu->is_first_fetch = true;
            ppu->processed_pixels = 0;
			ppu->bg_wnd_fetch_step = FETCH_STEP_TILE_0;
            ppu->sprite_fetch_active = false;
			fgb_queue_clear(&ppu->bg_wnd_fifo);

            // Increment window line counter every time a scanline
            // has any window pixels drawn
            if (ppu->reached_window_x) {
                ppu->window_line_counter++;
            }

			// Transition to HBlank
            ppu->hblank_cycles = HBLANK_MAX_CYCLES - ppu->mode_cycles;
			ppu->mode_cycles = 0;

            ppu->stat.mode = PPU_MODE_HBLANK;
        }
        break;

    case PPU_MODE_HBLANK:
        if (ppu->mode_cycles >= ppu->hblank_cycles) {
            if (ppu->scanline_cycles != SCANLINE_CYCLES) {
                log_warn("Scanline took %d cycles instead of 456", ppu->scanline_cycles);
            }

			ppu->mode_cycles = 0;
            ppu->scanline_cycles = 0;

            ppu->ly++;

            ppu->reached_window_x = false;

            // Clear sprites for next scanline
            memset(ppu->line_sprites, 0xFF, sizeof(ppu->line_sprites));

            if (ppu->ly == 144) {
                ppu->stat.mode = PPU_MODE_VBLANK;
                fgb_cpu_request_interrupt(ppu->cpu, IRQ_VBLANK);
                fgb_ppu_swap_buffers(ppu);

                ppu->reached_window_x = false;
                ppu->reached_window_y = false;
                ppu->window_line_counter = 0;
            } else {
                ppu->stat.mode = PPU_MODE_OAM_SCAN;
            }
        }
        break;

    case PPU_MODE_VBLANK:
        if (ppu->mode_cycles >= VBLANK_CYCLES) {
            ppu->mode_cycles -= VBLANK_CYCLES;
            ppu->scanline_cycles = 0;
            ppu->ly++;

            if (ppu->ly >= 154) {
                ppu->ly = 0;
                ppu->stat.mode = PPU_MODE_OAM_SCAN;
                ppu->frame_cycles = 0; // Reset frame cycles
                ppu->frames_rendered++;

                return true;
            }
        }
        break;
    default:
        log_error("PPU: Unknown mode %d", ppu->stat.mode);
        ppu->stat.mode = PPU_MODE_OAM_SCAN;
        break;
    }

    fgb_ppu_try_stat_irq(ppu);

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
        ppu->oam_blocked = ppu->dma_active; // OAM is blocked if a DMA is already active
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
		return ppu->stat.value
            | 0x80 // Unused bit 7 is always 1
            | ((ppu->lyc == ppu->ly) << 2); // Bit 2: LYC == LY

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

    return 0xFF;
}

void fgb_ppu_write_vram(fgb_ppu* ppu, uint16_t addr, uint8_t value) {
    if (ppu->stat.mode == PPU_MODE_DRAW) {
       return;
    }

    ppu->vram[addr] = value;
}

uint8_t fgb_ppu_read_vram(const fgb_ppu* ppu, uint16_t addr) {
    if (ppu->stat.mode == PPU_MODE_DRAW) {
       return 0xFF;
    }

    return ppu->vram[addr];
}

void fgb_ppu_write_oam(fgb_ppu* ppu, uint16_t addr, uint8_t value) {
    if (ppu->stat.mode == PPU_MODE_OAM_SCAN || ppu->stat.mode == PPU_MODE_DRAW) {
       return;
    }

    if (ppu->oam_blocked) {
        return;
    }

    ppu->oam[addr] = value;
}

uint8_t fgb_ppu_read_oam(const fgb_ppu* ppu, uint16_t addr) {
    if (ppu->stat.mode == PPU_MODE_OAM_SCAN || ppu->stat.mode == PPU_MODE_DRAW) {
       return 0xFF;
    }

    if (ppu->oam_blocked) {
        return 0xFF;
    }

    return ppu->oam[addr];
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

void fgb_ppu_pixel_fetcher_tick(fgb_ppu* ppu) {
    if (ppu->sprite_index < ppu->sprite_count && ppu->lcd_control.obj_enable && !ppu->sprite_fetch_active) {
        const fgb_sprite* next_sprite = (const fgb_sprite*)&ppu->oam[ppu->sprite_buffer[ppu->sprite_index]];
        if (next_sprite->x <= (ppu->framebuffer_x + 8)) {
            ppu->current_sprite = next_sprite;
            ppu->sprite_fetch_active = true;
            ppu->sprite_index++;
            ppu->sprite_fetch_step = FETCH_STEP_TILE_0;

            // When sprite fetch starts, BG/Wnd fetch is paused and reset
            ppu->bg_wnd_fetch_step = FETCH_STEP_TILE_0;
        }
    }

    if (ppu->sprite_fetch_active) {
        switch (ppu->sprite_fetch_step++) {
        case FETCH_STEP_TILE_0:
        case FETCH_STEP_TILE_1:
            break;
        case FETCH_STEP_DATA_LOW_0: {
            // Handle 8x8 and 8x16 sprites. For 8x16 the tile id's LSB is ignored
            // and the sprite may span two tiles vertically
            const int tile_id_base = ppu->current_sprite->tile & (ppu->lcd_control.obj_size ? 0xFE : 0xFF);
            const int sprite_height = ppu->lcd_control.obj_size ? PPU_SPRITE_H16 : PPU_SPRITE_H;
            int line = (ppu->ly + 16) - ppu->current_sprite->y; // line within sprite

            if (ppu->current_sprite->y_flip) {
                line = sprite_height - 1 - line;
            }
            line = max(line, 0);

            const int tile_index = tile_id_base + (line >= 8 ? 1 : 0);
            const int tile_row = 2 * (line % 8);
            const fgb_tile* tile = fgb_ppu_get_tile_data(ppu, tile_index, true);
            ppu->sprite_tile_lo = tile->data[tile_row];
        } break;
        case FETCH_STEP_DATA_LOW_1:
            break;
        case FETCH_STEP_DATA_HIGH_0: {
            const int tile_id_base = ppu->current_sprite->tile & (ppu->lcd_control.obj_size ? 0xFE : 0xFF);
            const int sprite_height = ppu->lcd_control.obj_size ? 16 : 8;
            int line = (ppu->ly + 16) - ppu->current_sprite->y; // line within sprite 0..height-1

            if (ppu->current_sprite->y_flip) {
                line = sprite_height - 1 - line;
            }
            line = max(line, 0);

            const int tile_index = tile_id_base + (line >= 8 ? 1 : 0);
            const int tile_row = 2 * (line % 8);
            const fgb_tile* tile = fgb_ppu_get_tile_data(ppu, tile_index, true);
            ppu->sprite_tile_hi = tile->data[tile_row + 1];
        } break;
        case FETCH_STEP_DATA_HIGH_1:
            break;
        case FETCH_STEP_PUSH_0: {
            // Push the 8 sprite pixels into the sprite FIFO aligned to the
            // current framebuffer_x. Each sprite pixel's screen X is
            // (sprite.x - 8) + sx. We compute its position relative to the
            // FIFO front (framebuffer_x) and either overwrite a transparent
            // slot or push blank pixels until the target index is reached.
            for (int sx = 0; sx < PPU_SPRITE_W; sx++) {
                const int bit = ppu->current_sprite->x_flip ? (PPU_SPRITE_W - sx - 1) : sx;
                const uint8_t color = TILE_PIXEL(ppu->sprite_tile_lo, ppu->sprite_tile_hi, bit);

                // Absolute screen X for this sprite pixel
                const int screen_x = ppu->current_sprite->x - 8 + sx;
                // Index relative to framebuffer_x
                const int rel = screen_x - ppu->framebuffer_x;

                if (rel < 0) {
                    continue; // pixel already past
                } else if (rel >= PPU_PIXEL_FIFO_SIZE) {
                    break; // too far to fit in FIFO 
                }

                const fgb_pixel pixel = {
                    .color = color,
                    .palette = ppu->current_sprite->palette,
                    .sprite_prio = 0,
                    .bg_prio = ppu->current_sprite->priority,
                };

                // If the FIFO already contains an entry at `rel`, only
                // overwrite it if it's transparent (color == 0). Otherwise
                // push blanks until we reach `rel` and then push the pixel.
                if (rel < ppu->sprite_fifo.count) {
                    fgb_pixel* existing = fgb_queue_at(&ppu->sprite_fifo, rel);
                    if (existing && existing->color == 0) {
                        *existing = pixel;
                    }
                } else {
                    while (ppu->sprite_fifo.count < rel) {
                        fgb_queue_push(&ppu->sprite_fifo, (fgb_pixel){ 0, 0, 0, 0 });
                    }
                    fgb_queue_push(&ppu->sprite_fifo, pixel);
                }
            }

            ppu->sprite_fetch_active = false;
        } break;
        case FETCH_STEP_PUSH_1:
            break;
        }
    }

    if (!ppu->sprite_fetch_active) {
        switch (ppu->bg_wnd_fetch_step++) {
        case FETCH_STEP_TILE_0: {
            ppu->fetch_tile_id = fgb_ppu_get_tile_id(ppu);
        } break;
        case FETCH_STEP_TILE_1:
            break;
        case FETCH_STEP_DATA_LOW_0: {
            const fgb_tile* tile = fgb_ppu_get_tile_data(ppu, ppu->fetch_tile_id, false);
            const int tile_y = fgb_ppu_get_current_tile_y(ppu);
            ppu->bg_wnd_tile_lo = tile->data[tile_y];
        } break;
        case FETCH_STEP_DATA_LOW_1:
            break;
        case FETCH_STEP_DATA_HIGH_0: {
            const fgb_tile* tile = fgb_ppu_get_tile_data(ppu, ppu->fetch_tile_id, false);
            const int tile_y = fgb_ppu_get_current_tile_y(ppu);
            ppu->bg_wnd_tile_hi = tile->data[tile_y + 1];

            if (ppu->is_first_fetch) {
                // The first time on each scanline the first 3 steps are repeated
                ppu->bg_wnd_fetch_step = FETCH_STEP_TILE_0;
                ppu->is_first_fetch = false;
            }
        } break;
        case FETCH_STEP_DATA_HIGH_1:
            break;
        case FETCH_STEP_PUSH_0: {
            if (!fgb_queue_empty(&ppu->bg_wnd_fifo)) {
                ppu->bg_wnd_fetch_step = FETCH_STEP_PUSH_1; // Wait until there is space in the FIFO
                break;
            }

            for (int x = 0; x < 8; x++) {
                const uint8_t pixel_index = TILE_PIXEL(ppu->bg_wnd_tile_lo, ppu->bg_wnd_tile_hi, x);
                const fgb_pixel pixel = { pixel_index, 0, 0, 0 };
                fgb_queue_push(&ppu->bg_wnd_fifo, pixel);
            }

            ppu->fetch_x++;
        } break;
        case FETCH_STEP_PUSH_1:
            ppu->bg_wnd_fetch_step = FETCH_STEP_TILE_0;
            break;
        }
    }
}

void fgb_ppu_lcd_push(fgb_ppu* ppu) {
    // No pixels are pushed to the LCD if the BG/Wnd FIFO is empty or if a sprite fetch is active
    if (fgb_queue_empty(&ppu->bg_wnd_fifo) || ppu->sprite_fetch_active) {
        return;
	}

    if (++ppu->processed_pixels < (ppu->scroll.x % 8)) {
        // Skip pixels until we reach the scroll offset
        fgb_queue_pop(&ppu->bg_wnd_fifo);
        return;
    }

	uint32_t* framebuffer = ppu->framebuffers[ppu->back_buffer];
    const fgb_pixel bg_pixel = fgb_queue_pop(&ppu->bg_wnd_fifo);
    const fgb_pixel sprite_pixel = fgb_queue_empty(&ppu->sprite_fifo)
        ? (fgb_pixel){ 0, 0, 0, 0 }
        : fgb_queue_pop(&ppu->sprite_fifo);
    
    uint32_t color = 0;
    if (sprite_pixel.color == 0) {
        // No sprite pixel, draw background pixel
        color = fgb_ppu_get_bg_color(ppu, bg_pixel.color);
    } else if (sprite_pixel.bg_prio == 1 && bg_pixel.color != 0) {
        // Sprite is behind background and background pixel is not color 0
        color = fgb_ppu_get_bg_color(ppu, bg_pixel.color);
    } else {
        // Draw sprite pixel
        color = fgb_ppu_get_obj_color(ppu, sprite_pixel.color, sprite_pixel.palette);
    }

	framebuffer[ppu->ly * SCREEN_WIDTH + ppu->framebuffer_x] = color;
	ppu->framebuffer_x++;

    if (ppu->reached_window_x) {
        return;
    }

    const int window_pos = (int)ppu->window_pos.x - 7;
    if (ppu->lcd_control.wnd_enable && ppu->reached_window_y && ppu->framebuffer_x >= window_pos) {
        ppu->reached_window_x = true;
        ppu->window_line_counter = 0;
        ppu->bg_wnd_fetch_step = FETCH_STEP_TILE_0;
        ppu->fetch_x = 0;
        fgb_queue_clear(&ppu->bg_wnd_fifo);
    }
}

void fgb_ppu_try_stat_irq(fgb_ppu* ppu) {
    const bool stat = (ppu->ly == ppu->lyc && ppu->stat.lyc_int) ||
        (ppu->stat.mode == PPU_MODE_HBLANK && ppu->stat.hblank_int) ||
        (ppu->stat.mode == PPU_MODE_OAM_SCAN && ppu->stat.oam_int) ||
        (ppu->stat.mode == PPU_MODE_VBLANK && (ppu->stat.vblank_int || ppu->stat.oam_int));

    if (!ppu->last_stat && stat) {
        fgb_cpu_request_interrupt(ppu->cpu, IRQ_LCD);
    }

    ppu->last_stat = stat;
}

void fgb_queue_push(fgb_queue* queue, fgb_pixel pixel) {
    if (fgb_queue_full(queue)) {
        log_warn("PPU Pixel Queue Overflow");
        return;
	}

    queue->pixels[queue->push_index] = pixel;
	queue->push_index = (queue->push_index + 1) % PPU_PIXEL_FIFO_SIZE;
    queue->count++;
}

fgb_pixel fgb_queue_pop(fgb_queue* queue) {
    if (fgb_queue_empty(queue)) {
        log_warn("PPU Pixel Queue Underflow");
        return (fgb_pixel) { 0, 0, 0, 0 };
    }

    const fgb_pixel pixel = queue->pixels[queue->pop_index];
    queue->pop_index = (queue->pop_index + 1) % PPU_PIXEL_FIFO_SIZE;
	queue->count--;

	return pixel;
}

fgb_pixel* fgb_queue_at(fgb_queue* queue, int index) {
    if (index < 0 || index >= PPU_PIXEL_FIFO_SIZE) {
        log_warn("PPU Pixel Queue Index Out of Bounds");
        return NULL;
    }

    const int real_index = (queue->pop_index + index) % PPU_PIXEL_FIFO_SIZE;
    return &queue->pixels[real_index];
}

bool fgb_queue_full(const fgb_queue* queue) {
	return queue->count >= PPU_PIXEL_FIFO_SIZE;
}

bool fgb_queue_empty(const fgb_queue* queue) {
	return queue->count == 0;
}

void fgb_queue_clear(fgb_queue* queue) {
    queue->push_index = 0;
	queue->pop_index = 0;
	queue->count = 0;
}
