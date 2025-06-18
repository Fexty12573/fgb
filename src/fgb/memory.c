#include "memory.h"


void fgb_mem_write(fgb_memory* memory, uint16_t addr, uint8_t value) {
    // Will do memory mapping later
    memory->data[addr] = value;
}

uint8_t fgb_mem_read(const fgb_memory* memory, uint16_t addr) {
    return memory->data[addr];
}

uint16_t fgb_mem_read_u16(const fgb_memory* memory, uint16_t addr) {
    const uint16_t lower = fgb_mem_read(memory, addr);
    const uint16_t upper = fgb_mem_read(memory, addr + 1);
    return upper << 8 | lower;
}
