#include "io.h"

#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "ulog.h"

static fgb_joypad fgb_io_get_joypad(const fgb_io* io);

void fgb_io_init(fgb_io* io, fgb_cpu* cpu) {
    memset(io, 0, sizeof(fgb_io));
    io->cpu = cpu;
    io->joypad.value = 0xFF; // All buttons released by default
}

void fgb_io_write(fgb_io* io, uint16_t addr, uint8_t value) {
    switch (addr) {
    case 0xFF00:
        io->joypad.sel = (value >> 4) & 0b11;
        return;

    case 0xFF01:
        io->serial.sb = value;
        return;

    case 0xFF02:
        io->serial.sc = value;
        if (io->serial.transfer) {
            putc(io->serial.sb, stdout);
        }
        return;

    default:
        break;
    }

    log_warn("Unknown address for IO write: 0x%04X", addr);
}

uint8_t fgb_io_read(const fgb_io* io, uint16_t addr) {
    switch (addr) {
    case 0xFF00:
        return fgb_io_get_joypad(io).value;

    case 0xFF01:
        return io->serial.sb;

    case 0xFF02:
        return io->serial.sc;

    default:
        break;
    }

    log_warn("Unknown address for IO write: 0x%04X", addr);
    return 0xAA;
}

void fgb_io_press_button(fgb_io* io, enum fgb_button button) {
    io->buttons_pressed[button] = true;
    fgb_cpu_request_interrupt(io->cpu, IRQ_JOYPAD);
}

void fgb_io_release_button(fgb_io* io, enum fgb_button button) {
    io->buttons_pressed[button] = false;
}

fgb_joypad fgb_io_get_joypad(const fgb_io* io) {
    fgb_joypad pad = {
        .sel = io->joypad.sel,
    };

    if (pad.sel == 0b11) {
        pad.lower = 0xF; // All buttons released
        return pad;
    }

    if (pad.sel_buttons == 0) {
        pad.a = !io->buttons_pressed[BUTTON_A];
        pad.b = !io->buttons_pressed[BUTTON_B];
        pad.select = !io->buttons_pressed[BUTTON_SELECT];
        pad.start = !io->buttons_pressed[BUTTON_START];
    } else if (pad.sel_dpad == 0) {
        pad.right = !io->buttons_pressed[BUTTON_RIGHT];
        pad.left = !io->buttons_pressed[BUTTON_LEFT];
        pad.up = !io->buttons_pressed[BUTTON_UP];
        pad.down = !io->buttons_pressed[BUTTON_DOWN];
    }

    return pad;
}
