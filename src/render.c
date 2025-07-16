#include "render.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include <GL/glew.h>
#include <ulog.h>

#define TILE_BLOCK_1_ADDR (0x8000)
#define TILE_BLOCK_2_ADDR (TILE_BLOCK_1_ADDR + TILE_BLOCK_SIZE)
#define TILE_BLOCK_3_ADDR (TILE_BLOCK_2_ADDR + TILE_BLOCK_SIZE)
#define TILE_BLOCK_ADDR(BLOCK) (TILE_BLOCK_1_ADDR + ((BLOCK) * TILE_BLOCK_SIZE))
#define TILE_BLOCK_VRAM_OFFSET(BLOCK) ((BLOCK) * TILE_BLOCK_SIZE)

#define TILE_BLOCK_SIZE_RGBA (TILES_PER_BLOCK * TILE_SIZE * sizeof(uint32_t)) // 128 tiles per block, each tile is 64 pixels (RGBA)

#define gl_call(x) do { \
    x; \
    GLenum err = glGetError(); \
    if (err != GL_NO_ERROR) { \
        printf("OpenGL error: %d at %s:%d", err, __FILE__, __LINE__); \
        fflush(stdout); \
    } \
} while (0)

static const float QUAD_VERTICES[] = {
    -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, // Bottom-left corner
     1.0f, -1.0f, 0.0f, 1.0f, 0.0f, // Bottom-right corner
     1.0f,  1.0f, 0.0f, 1.0f, 1.0f, // Top-right corner
    -1.0f,  1.0f, 0.0f, 0.0f, 1.0f  // Top-left corner
};


static const int QUAD_INDICES[] = {
    0, 1, 2, // First triangle
    2, 3, 0  // Second triangle
};

static uint32_t* s_texture_data = NULL;

typedef union fgb_tile {
    uint8_t data[TILE_SIZE_BYTES];
} fgb_tile;

static inline uint8_t fgb_tile_get_pixel(const fgb_tile* tile, uint8_t x, uint8_t y) {
    const uint8_t lsb = tile->data[y * 2];
    const uint8_t msb = tile->data[y * 2 + 1];
    return ((msb >> (7 - x)) & 1) << 1 | ((lsb >> (7 - x)) & 1);
}

uint32_t fgb_create_screen_texture(void) {
    uint32_t texture_id;
    gl_call(glGenTextures(1, &texture_id));

    gl_call(glBindTexture(GL_TEXTURE_2D, texture_id));
    gl_call(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SCREEN_WIDTH, SCREEN_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL));

    gl_call(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    gl_call(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    gl_call(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    gl_call(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

    return texture_id;
}

void fgb_upload_screen_texture(uint32_t texture_id, fgb_ppu* ppu) {
    gl_call(glBindTexture(GL_TEXTURE_2D, texture_id));

    // ppu->screen is already in RGBA format, so we can upload it directly
    fgb_ppu_lock_buffer(ppu);
    const uint32_t* framebuffer = fgb_ppu_get_front_buffer(ppu);
    gl_call(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, framebuffer));
    fgb_ppu_unlock_buffer(ppu);
}

uint32_t fgb_create_tile_block_texture(int tiles_per_row) {
    assert(TILES_PER_BLOCK % tiles_per_row == 0);
    
    const int width = tiles_per_row * TILE_WIDTH;
    const int height = (TILES_PER_BLOCK / tiles_per_row) * TILE_HEIGHT;

    assert(width * height * sizeof(uint32_t) == TILE_BLOCK_SIZE_RGBA);

    uint32_t texture_id;
    gl_call(glGenTextures(1, &texture_id));
    gl_call(glBindTexture(GL_TEXTURE_2D, texture_id));
    gl_call(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL));
    gl_call(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    gl_call(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    gl_call(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    gl_call(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

    return texture_id;
}

void fgb_upload_tile_block_texture(uint32_t texture_id, int tiles_per_row, const fgb_ppu* ppu, int tile_block, const fgb_palette* pal) {
    const fgb_tile* tiles = (const fgb_tile*)(&ppu->vram[TILE_BLOCK_VRAM_OFFSET(tile_block)]);

    if (!s_texture_data) {
        s_texture_data = malloc(TILE_BLOCK_SIZE_RGBA);
        if (!s_texture_data) {
            log_error("Failed to allocate texture data");
            return;
        }
    }

    uint32_t* texture_data = s_texture_data;
    
    for (int i = 0; i < TILES_PER_BLOCK; i++) {
        const fgb_tile* tile = tiles + i;

        for (int y = 0; y < TILE_HEIGHT; y++) {
            for (int x = 0; x < TILE_WIDTH; x++) {
                const uint8_t pixel_index = fgb_tile_get_pixel(tile, x, y);

                const int tex_x = (i % tiles_per_row) * TILE_WIDTH + x;
                const int tex_y = (i / tiles_per_row) * TILE_HEIGHT + y;
                const int tex_index = (tex_y * (tiles_per_row * TILE_WIDTH)) + tex_x;

                texture_data[tex_index] = pal->colors[pixel_index];
            }
        }
    }

    const int width = tiles_per_row * TILE_WIDTH;
    const int height = (TILES_PER_BLOCK / tiles_per_row) * TILE_HEIGHT;

    gl_call(glBindTexture(GL_TEXTURE_2D, texture_id));
    gl_call(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, texture_data));
}

void fgb_create_quad(uint32_t* vertex_array, uint32_t* vertex_buffer, uint32_t* index_buffer) {
    assert(vertex_array != NULL);
    assert(vertex_buffer != NULL);

    glGenVertexArrays(1, vertex_array);
    glBindVertexArray(*vertex_array);

    glGenBuffers(1, vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, *vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD_VERTICES), QUAD_VERTICES, GL_STATIC_DRAW);

    if (index_buffer) {
        glGenBuffers(1, index_buffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, *index_buffer);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(QUAD_INDICES), QUAD_INDICES, GL_STATIC_DRAW);
    }

    // Position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Texture coordinate attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}
