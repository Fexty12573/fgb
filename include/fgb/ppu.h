#ifndef PPU_H
#define PPU_H

#include <stdint.h>
#include <stdbool.h>

#define PPU_VRAM_SIZE   0x2000
#define PPU_OAM_SIZE    0xA0
#define PPU_DMA_BYTES   PPU_OAM_SIZE // Number of bytes transferred in a single DMA operation
#define SCREEN_WIDTH    160
#define SCREEN_HEIGHT   144

#define TILE_WIDTH          8
#define TILE_HEIGHT         8
#define TILE_SIZE           (TILE_WIDTH * TILE_HEIGHT)
#define TILE_SIZE_BYTES     (TILE_SIZE / 4) // 2bpp
#define TILES_PER_BLOCK     128 // Number of tiles in a block
#define TILE_BLOCK_COUNT    3 // Number of tile blocks
#define TILES_PER_SCANLINE  (SCREEN_WIDTH / TILE_WIDTH)
#define TILE_BLOCK_SIZE     (TILES_PER_BLOCK * TILE_SIZE_BYTES) // 128 tiles per block

#define PPU_SCANLINE_SPRITES    10 // Maximum number of sprites per scanline
#define PPU_SPRITE_SIZE_BYTES   4
#define PPU_OAM_SPRITES         (PPU_OAM_SIZE / PPU_SPRITE_SIZE_BYTES) // Number of sprites in OAM
#define PPU_SPRITE_W            TILE_WIDTH
#define PPU_SPRITE_H            TILE_HEIGHT
#define PPU_SPRITE_H16          (2 * TILE_HEIGHT)

enum fgb_ppu_mode {
    PPU_MODE_HBLANK = 0,
    PPU_MODE_VBLANK,
    PPU_MODE_OAM_SCAN,
    PPU_MODE_DRAW
};

typedef struct fgb_palette {
    uint32_t colors[4];
} fgb_palette;

typedef struct fgb_ppu {
    uint8_t vram[PPU_VRAM_SIZE];
    uint8_t oam[PPU_OAM_SIZE];
    uint32_t screen[SCREEN_WIDTH * SCREEN_HEIGHT];

    uint32_t mode_cycles; // Cycles for the current mode
    uint32_t frame_cycles; // Cycles for the current frame

    int pixels_drawn; // Number of pixels drawn in the current scanline

    uint8_t sprite_buffer[PPU_SCANLINE_SPRITES]; // Offsets into OAM for sprites on the current scanline
    int sprite_count;
    bool oam_scan_done;

    fgb_palette bg_palette;
    fgb_palette obj_palette;

    int frames_rendered;

    union {
        uint8_t value;
        struct {
            uint8_t bg_wnd_prio_enable : 1;
            uint8_t obj_enable : 1;
            uint8_t obj_size : 1;
            uint8_t bg_tile_map : 1;
            uint8_t bg_wnd_tiles : 1;
            uint8_t wnd_enable : 1;
            uint8_t wnd_tile_map : 1;
            uint8_t lcd_ppu_enable : 1;
        };
    } lcd_control;

    uint8_t ly;
    uint8_t lyc;
    union {
        uint8_t value;
        struct {
            uint8_t mode : 2;
            uint8_t lyc_eq_ly : 1;
            uint8_t mode_0_int : 1;
            uint8_t mode_1_int : 1;
            uint8_t mode_2_int : 1;
            uint8_t lyc_int : 1;
        };
        struct {
            // mode and lyc_eq_ly are read-only
            uint8_t read_only : 3;
            uint8_t : 5;
        };
    } stat;

    struct {
        uint8_t x;
        uint8_t y;
    } scroll;

    struct {
        uint8_t x;
        uint8_t y;
    } window_pos;

    union {
        uint8_t value;
        struct {
            uint8_t col0 : 2;
            uint8_t col1 : 2;
            uint8_t col2 : 2;
            uint8_t col3 : 2;
        };
    } bgp;

    union {
        uint8_t value;
        struct {
            uint8_t col0 : 2; // always transparent
            uint8_t col1 : 2;
            uint8_t col2 : 2;
            uint8_t col3 : 2;
        };
    } obp[2];

    bool dma_active; // DMA transfer is active
    uint16_t dma_addr; // Address for DMA transfer
    int dma_bytes; // Number of bytes transferred in the current DMA operation
    int dma_cycles;

    struct fgb_cpu* cpu;
} fgb_ppu;


fgb_ppu* fgb_ppu_create(void);
void fgb_ppu_destroy(fgb_ppu* ppu);
void fgb_ppu_set_cpu(fgb_ppu* ppu, struct fgb_cpu* cpu);

bool fgb_ppu_tick(fgb_ppu* ppu, uint32_t cycles);

void fgb_ppu_write(fgb_ppu* ppu, uint16_t addr, uint8_t value);
uint8_t fgb_ppu_read(const fgb_ppu* ppu, uint16_t addr);

void fgb_ppu_write_vram(fgb_ppu* ppu, uint16_t addr, uint8_t value);
uint8_t fgb_ppu_read_vram(const fgb_ppu* ppu, uint16_t addr);

void fgb_ppu_write_oam(fgb_ppu* ppu, uint16_t addr, uint8_t value);
uint8_t fgb_ppu_read_oam(const fgb_ppu* ppu, uint16_t addr);

#endif // PPU_H
