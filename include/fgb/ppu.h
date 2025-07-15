#ifndef FGB_PPU_H
#define FGB_PPU_H

#include <stdint.h>

#define FGB_PPU_VRAM_SIZE   0x2000
#define FGB_PPU_OAM_SIZE    0xA0


enum fgb_ppu_mode {
    PPU_MODE_HBLANK = 0,
    PPU_MODE_VBLANK,
    PPU_MODE_OAM_SCAN,
    PPU_MODE_DRAW
};

typedef struct fgb_ppu {
    uint8_t vram[FGB_PPU_VRAM_SIZE];
    uint8_t oam[FGB_PPU_OAM_SIZE];

    uint32_t mode_cycles; // Cycles for the current mode
    uint32_t frame_cycles; // Cycles for the current frame

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
            uint8_t : 2; // always transparent
            uint8_t col1 : 2;
            uint8_t col2 : 2;
            uint8_t col3 : 2;
        };
    } obp0, obp1;

    struct fgb_cpu* cpu;
} fgb_ppu;


fgb_ppu* fgb_ppu_create(void);
void fgb_ppu_destroy(fgb_ppu* ppu);
void fgb_ppu_set_cpu(fgb_ppu* ppu, struct fgb_cpu* cpu);

void fgb_ppu_tick(fgb_ppu* ppu, uint32_t cycles);

void fgb_ppu_write(fgb_ppu* ppu, uint16_t addr, uint8_t value);
uint8_t fgb_ppu_read(const fgb_ppu* ppu, uint16_t addr);

void fgb_ppu_write_vram(fgb_ppu* ppu, uint16_t addr, uint8_t value);
uint8_t fgb_ppu_read_vram(const fgb_ppu* ppu, uint16_t addr);

void fgb_ppu_write_oam(fgb_ppu* ppu, uint16_t addr, uint8_t value);
uint8_t fgb_ppu_read_oam(const fgb_ppu* ppu, uint16_t addr);

#endif // FGB_PPU_H
