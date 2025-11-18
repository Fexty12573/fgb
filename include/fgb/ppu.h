#ifndef PPU_H
#define PPU_H

#include <stdint.h>
#include <stdbool.h>
#include <threads.h>

#define PPU_VRAM_SIZE 0x2000
#define PPU_OAM_SIZE  0xA0
#define PPU_DMA_BYTES PPU_OAM_SIZE // Number of bytes transferred in a single DMA operation
#define SCREEN_WIDTH  160
#define SCREEN_HEIGHT 144
#define ASPECT_RATIO  ((float)SCREEN_WIDTH / (float)SCREEN_HEIGHT)

#define TILE_WIDTH          8
#define TILE_HEIGHT         8
#define TILE_SIZE           (TILE_WIDTH * TILE_HEIGHT)
#define TILE_SIZE_BYTES     (TILE_SIZE / 4) // 2bpp
#define TILES_PER_BLOCK     128 // Number of tiles in a block
#define TILE_BLOCK_COUNT    3 // Number of tile blocks
#define TILES_PER_SCANLINE  (SCREEN_WIDTH / TILE_WIDTH)
#define TILE_BLOCK_SIZE     (TILES_PER_BLOCK * TILE_SIZE_BYTES) // 128 tiles per block

#define PPU_FRAMEBUFFER_COUNT   2 // Double buffering
#define PPU_SCANLINE_SPRITES    10 // Maximum number of sprites per scanline
#define PPU_SPRITE_SIZE_BYTES   4
#define PPU_OAM_SPRITES         (PPU_OAM_SIZE / PPU_SPRITE_SIZE_BYTES) // Number of sprites in OAM
#define PPU_SPRITE_W            TILE_WIDTH
#define PPU_SPRITE_H            TILE_HEIGHT
#define PPU_SPRITE_H16          (2 * TILE_HEIGHT)

#define PPU_PIXEL_FIFO_SIZE     8

enum fgb_ppu_mode {
    PPU_MODE_HBLANK = 0,
    PPU_MODE_VBLANK,
    PPU_MODE_OAM_SCAN,
    PPU_MODE_DRAW
};

enum fgb_fetch_step {
	// 2 T-cycles each
	FETCH_STEP_TILE_0,
	FETCH_STEP_TILE_1,
    FETCH_STEP_DATA_LOW_0,
    FETCH_STEP_DATA_LOW_1,
	FETCH_STEP_DATA_HIGH_0,
	FETCH_STEP_DATA_HIGH_1,
	FETCH_STEP_PUSH_0,
	FETCH_STEP_PUSH_1,
};

enum fgb_color_mode {
    PPU_COLOR_MODE_NORMAL,
    PPU_COLOR_MODE_TINTED,
};

typedef struct fgb_palette {
    uint32_t colors[4];
} fgb_palette;

typedef struct fgb_tile {
    uint8_t data[16]; // 8x8 tile data, 2 bytes per row
} fgb_tile;

typedef struct fgb_sprite {
    uint8_t y;
    uint8_t x;
    uint8_t tile;
    union {
        uint8_t flags;
        struct {
            uint8_t : 3; // Palette in CGB mode
            uint8_t : 1; // Bank in CGB mode
            uint8_t palette : 1;
            uint8_t x_flip : 1;
            uint8_t y_flip : 1;
            uint8_t priority : 1; // 0 = in front of background, 1 = behind background (*)
        };
    };
} fgb_sprite;

typedef struct fgb_pixel {
    uint8_t color : 2;
	uint8_t palette : 1;
    uint8_t sprite_prio : 1; // Only relevant for CGB sprites
    uint8_t bg_prio : 1; // See fgb_sprite::priority
	uint8_t is_wnd : 1; // Debug: is this pixel from the window?
} fgb_pixel;

typedef struct fgb_queue {
	fgb_pixel pixels[PPU_PIXEL_FIFO_SIZE];
	int push_index;
	int pop_index;
    int count;
} fgb_queue;

typedef struct fgb_ppu {
    uint8_t vram[PPU_VRAM_SIZE];
    uint8_t oam[PPU_OAM_SIZE];
    uint32_t framebuffers[PPU_FRAMEBUFFER_COUNT][SCREEN_WIDTH * SCREEN_HEIGHT];
	int framebuffer_x; // Current X position in the framebuffer (actual number of pixels drawn)
    int processed_pixels; // Number of pixels pushed OR discarded from the FIFO

    // For keeping track of which sprites were rendered on which scanline
    uint8_t line_sprites[SCREEN_HEIGHT][PPU_SCANLINE_SPRITES];

    // Pixel FIFO
    fgb_queue bg_wnd_fifo;
	fgb_queue sprite_fifo;
	enum fgb_fetch_step bg_wnd_fetch_step;
    enum fgb_fetch_step sprite_fetch_step;
	int fetch_x;

    bool reached_window_x;
    bool reached_window_y;
    int window_line_counter;

    // Fetcher internal storage
    int fetch_tile_id;
	uint8_t bg_wnd_tile_lo;
	uint8_t bg_wnd_tile_hi;
    uint8_t sprite_tile_lo;
    uint8_t sprite_tile_hi;
    bool is_first_fetch;
    bool sprite_fetch_active;
    bool is_window_tile;
    int sprite_index; // Index of the next sprite to evaluate during pixel fetching
    const fgb_sprite* current_sprite; // Current sprite being fetched

    int back_buffer;
    mtx_t buffer_mutex;

    uint32_t mode_cycles; // Cycles for the current mode
    uint32_t frame_cycles; // Cycles for the current frame
	uint32_t hblank_cycles; // Cycles spent in HBlank
    uint32_t scanline_cycles; // Cycles spent in the current scanline

    int pixels_drawn; // Number of pixels drawn in the current scanline

    uint8_t sprite_buffer[PPU_SCANLINE_SPRITES]; // Offsets into OAM for sprites on the current scanline
    int sprite_count;
    bool oam_scan_done;

    bool last_stat;
    bool reset;

    fgb_palette bg_palette;
    fgb_palette obj_palette;

    int frames_rendered;

    union {
        uint8_t value;
        struct {
            /* 0 */ uint8_t bg_wnd_enable : 1;
            /* 1 */ uint8_t obj_enable : 1;
            /* 2 */ uint8_t obj_size : 1;
            /* 3 */ uint8_t bg_tile_map : 1;
            /* 4 */ uint8_t bg_wnd_tiles : 1;
            /* 5 */ uint8_t wnd_enable : 1;
            /* 6 */ uint8_t wnd_tile_map : 1;
            /* 7 */ uint8_t lcd_ppu_enable : 1;
        };
    } lcd_control;

    uint8_t ly;
    uint8_t lyc;
    union {
        uint8_t value;
        struct {
            /* 0 */ uint8_t mode : 2;
            /* 2 */ uint8_t : 1; // lyc_eq_ly flag (computed on read)
            /* 3 */ uint8_t hblank_int : 1;
            /* 4 */ uint8_t vblank_int : 1;
            /* 5 */ uint8_t oam_int : 1;
            /* 6 */ uint8_t lyc_int : 1;
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

    struct {
        bool hide_bg;
        bool hide_sprites;
        bool hide_window;
        uint32_t window_color;
    } debug;

    bool dma_active; // DMA transfer is active
    bool oam_blocked; // OAM is blocked by DMA
    uint8_t dma; // DMA register value
    uint16_t dma_addr; // Address for DMA transfer
    int dma_bytes; // Number of bytes transferred in the current DMA operation
    int dma_cycles;

    struct fgb_cpu* cpu;
} fgb_ppu;


fgb_ppu* fgb_ppu_create(void);
void fgb_ppu_destroy(fgb_ppu* ppu);
void fgb_ppu_set_cpu(fgb_ppu* ppu, struct fgb_cpu* cpu);
void fgb_ppu_reset(fgb_ppu* ppu);

const uint32_t* fgb_ppu_get_front_buffer(const fgb_ppu* ppu);
const uint32_t* fgb_ppu_get_back_buffer(const fgb_ppu* ppu);
void fgb_ppu_lock_buffer(fgb_ppu* ppu);
void fgb_ppu_unlock_buffer(fgb_ppu* ppu);
void fgb_ppu_swap_buffers(fgb_ppu* ppu);
void fgb_ppu_set_color_mode(fgb_ppu* ppu, enum fgb_color_mode mode);

int fgb_ppu_get_tile_id_old(const fgb_ppu* ppu, int tile_map, int x, int y);
const fgb_tile* fgb_ppu_get_tile_data(const fgb_ppu* ppu, int tile_id, bool is_sprite);
uint8_t fgb_tile_get_pixel(const fgb_tile* tile, uint8_t x, uint8_t y);
uint32_t fgb_ppu_get_bg_color(const fgb_ppu* ppu, fgb_pixel pixel);
uint32_t fgb_ppu_get_obj_color(const fgb_ppu* ppu, uint8_t pixel_index, int palette);

bool fgb_ppu_tick(fgb_ppu* ppu);

void fgb_ppu_write(fgb_ppu* ppu, uint16_t addr, uint8_t value);
uint8_t fgb_ppu_read(const fgb_ppu* ppu, uint16_t addr);

void fgb_ppu_write_vram(fgb_ppu* ppu, uint16_t addr, uint8_t value);
uint8_t fgb_ppu_read_vram(const fgb_ppu* ppu, uint16_t addr);

void fgb_ppu_write_oam(fgb_ppu* ppu, uint16_t addr, uint8_t value);
uint8_t fgb_ppu_read_oam(const fgb_ppu* ppu, uint16_t addr);

#endif // PPU_H
