#ifndef FGB_IO_H
#define FGB_IO_H

#include <stdint.h>
#include <stdbool.h>

enum fgb_button {
    BUTTON_A = 0,
    BUTTON_B,
    BUTTON_SELECT,
    BUTTON_START,
    BUTTON_RIGHT,
    BUTTON_LEFT,
    BUTTON_DOWN,
    BUTTON_UP,

    BUTTON_COUNT
};

typedef union fgb_joypad {
    uint8_t value;
    union {
        struct {
            uint8_t a : 1;
            uint8_t b : 1;
            uint8_t select : 1;
            uint8_t start : 1;
            uint8_t sel_dpad : 1;
            uint8_t sel_buttons : 1;
            uint8_t : 2; // Unused bits
        };

        struct {
            uint8_t right : 1;
            uint8_t left : 1;
            uint8_t up : 1;
            uint8_t down : 1;
            uint8_t : 4;
        };

        // For writing to P1
        struct {
            uint8_t : 4;
            uint8_t sel : 2;
            uint8_t : 2;
        };

        struct {
            uint8_t lower : 4;
            uint8_t upper : 4;
        };
    };
} fgb_joypad;

typedef struct fgb_io {
    struct {
        uint8_t sb;
        union {
            uint8_t sc;
            struct {
                uint8_t clk_sel : 1;
                uint8_t clk_spd : 1;
                uint8_t : 5;
                uint8_t transfer : 1;
            };
        };
    } serial;

    fgb_joypad joypad;
    bool buttons_pressed[BUTTON_COUNT];

    struct fgb_cpu* cpu;
} fgb_io;

void fgb_io_init(fgb_io* io, struct fgb_cpu* cpu);

void fgb_io_write(fgb_io* io, uint16_t addr, uint8_t value);
uint8_t fgb_io_read(const fgb_io* io, uint16_t addr);

void fgb_io_press_button(fgb_io* io, enum fgb_button button);
void fgb_io_release_button(fgb_io* io, enum fgb_button button);

#endif // FGB_IO_H
