#ifndef FGB_RENDER_H
#define FGB_RENDER_H

#include <stdint.h>
#include <stdbool.h>

#include <fgb/ppu.h>

#define TILE_WIDTH 8
#define TILE_HEIGHT 8
#define TILE_SIZE (TILE_WIDTH * TILE_HEIGHT)
#define TILE_SIZE_BYTES (TILE_SIZE / 4) // 2bpp
#define TILES_PER_BLOCK 128 // Number of tiles in a block
#define TILE_BLOCK_COUNT 3 // Number of tile blocks

typedef struct fgb_palette {
    uint32_t colors[4];
} fgb_palette;

uint32_t fgb_create_tile_block_texture(int tiles_per_row);
void fgb_upload_tile_block_texture(uint32_t texture_id, int tiles_per_row, const fgb_ppu* ppu, int tile_block, const fgb_palette* pal);

void fgb_create_quad(uint32_t* vertex_array, uint32_t* vertex_buffer, uint32_t* index_buffer);

#endif // FGB_RENDER_H
