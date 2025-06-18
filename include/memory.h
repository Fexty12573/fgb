#ifndef FGB_MEMORY_H
#define FGB_MEMORY_H

#include <stdbool.h>
#include <stdint.h>

#define FGB_MEMORY_SIZE 0x10000 // 64KB memory size


typedef struct fgb_memory {
    union {
        uint8_t data[FGB_MEMORY_SIZE];
        struct {
            uint8_t* ext_data;
            size_t ext_data_size;
        };
    };

    void (*reset)(struct fgb_memory* memory);
    void (*write_u8)(struct fgb_memory* memory, uint16_t addr, uint8_t value);
    uint8_t(*read_u8)(const struct fgb_memory* memory, uint16_t addr);
    uint16_t(*read_u16)(const struct fgb_memory* memory, uint16_t addr);
    bool use_ext_data;
} fgb_memory;

typedef struct fgb_mem_ops {
    void (*reset)(fgb_memory* memory);
    void (*write_u8)(fgb_memory* memory, uint16_t addr, uint8_t value);
    uint8_t (*read_u8)(const fgb_memory* memory, uint16_t addr);
    uint16_t (*read_u16)(const fgb_memory* memory, uint16_t addr);
    uint8_t* data; // Can be NULL if not used
    size_t data_size;
} fgb_mem_ops;

void fgb_mem_init(fgb_memory* memory, const fgb_mem_ops* ops);

#endif // FGB_MEMORY_H
