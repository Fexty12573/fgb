#include "memory.h"

#include <string.h>

static void fgb_mem_reset(fgb_memory* memory);
static void fgb_mem_write(fgb_memory* memory, uint16_t addr, uint8_t value);
static uint8_t fgb_mem_read(const fgb_memory* memory, uint16_t addr);
static uint16_t fgb_mem_read_u16(const fgb_memory* memory, uint16_t addr);

void fgb_mem_init(fgb_memory* memory, const fgb_mem_ops* ops) {
    memory->use_ext_data = false;

    if (ops) {
        memory->reset = ops->reset;
        memory->write_u8 = ops->write_u8;
        memory->read_u8 = ops->read_u8;
        memory->read_u16 = ops->read_u16;

        if (ops->data) {
            memory->ext_data = ops->data;
            memory->ext_data_size = ops->data_size;
            memory->use_ext_data = true;
        }
    } else {
        memory->reset = fgb_mem_reset;
        memory->write_u8 = fgb_mem_write;
        memory->read_u8 = fgb_mem_read;
        memory->read_u16 = fgb_mem_read_u16;
    }
}

void fgb_mem_reset(fgb_memory* memory) {
    if (memory->use_ext_data) {
        memset(memory->ext_data, 0, memory->ext_data_size);
    } else {
        memset(memory->data, 0, sizeof(memory->data));
    }
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
