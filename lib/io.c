#include "io.h"

#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "ulog.h"


void fgb_io_init(fgb_io* io, fgb_cpu* cpu) {
    memset(io, 0, sizeof(fgb_io));
    io->cpu = cpu;
}

void fgb_io_write(fgb_io* io, uint16_t addr, uint8_t value) {
    switch (addr) {
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
