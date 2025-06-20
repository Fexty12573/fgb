#ifndef FGB_IO_H
#define FGB_IO_H

#include <stdint.h>


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

    struct fgb_cpu* cpu;
} fgb_io;

void fgb_io_init(fgb_io* io, struct fgb_cpu* cpu);

void fgb_io_write(fgb_io* io, uint16_t addr, uint8_t value);
uint8_t fgb_io_read(const fgb_io* io, uint16_t addr);

#endif // FGB_IO_H
