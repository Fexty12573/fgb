#ifndef FGB_MEMORY_H
#define FGB_MEMORY_H

#include <stdint.h>

#define FGB_MEMORY_SIZE 0x10000 // 64KB memory size


typedef struct fgb_memory {
    uint8_t data[FGB_MEMORY_SIZE];
} fgb_memory;

void fgb_mem_write(fgb_memory* memory, uint16_t addr, uint8_t value);
uint8_t fgb_mem_read(const fgb_memory* memory, uint16_t addr);

#endif // FGB_MEMORY_H
