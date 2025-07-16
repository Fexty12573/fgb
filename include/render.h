#ifndef FGB_RENDER_H
#define FGB_RENDER_H

#include <stdint.h>
#include <stdbool.h>

#include <fgb/ppu.h>

uint32_t fgb_create_screen_texture(void);
void fgb_upload_screen_texture(uint32_t texture_id, fgb_ppu* ppu);
uint32_t fgb_create_tile_block_texture(int tiles_per_row);
void fgb_upload_tile_block_texture(uint32_t texture_id, int tiles_per_row, const fgb_ppu* ppu, int tile_block, const fgb_palette* pal);

void fgb_create_quad(uint32_t* vertex_array, uint32_t* vertex_buffer, uint32_t* index_buffer);

#endif // FGB_RENDER_H
