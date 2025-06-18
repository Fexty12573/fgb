#include "memory.h"


void fgb_mem_write(fgb_memory* memory, uint16_t addr, uint8_t value) {
    // Will do memory mapping later
    memory->data[addr] = value;
}

uint8_t fgb_mem_read(const fgb_memory* memory, uint16_t addr) {
    return memory->data[addr];
}
