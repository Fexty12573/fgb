#include "memory.h"

static void fgb_mem_write(fgb_memory* memory, uint16_t addr, uint8_t value);
static uint8_t fgb_mem_read(const fgb_memory* memory, uint16_t addr);
static uint16_t fgb_mem_read_u16(const fgb_memory* memory, uint16_t addr);

void fgb_mem_init(fgb_memory* memory) {
    memory->write_u8 = fgb_mem_write;
    memory->read_u8 = fgb_mem_read;
    memory->read_u16 = fgb_mem_read_u16;
}

static void fgb_mem_write(fgb_memory* memory, uint16_t addr, uint8_t value) {
    // Will do memory mapping later
    memory->data[addr] = value;
}

static uint8_t fgb_mem_read(const fgb_memory* memory, uint16_t addr) {
    return memory->data[addr];
}

static uint16_t fgb_mem_read_u16(const fgb_memory* memory, uint16_t addr) {
    const uint16_t lower = fgb_mem_read(memory, addr);
    const uint16_t upper = fgb_mem_read(memory, addr + 1);
    return upper << 8 | lower;
}
