#ifndef FGB_CART_H
#define FGB_CART_H

#include <stdint.h>
#include <stdbool.h>

#define FGB_CART_MAX_ROM_BANKS 256
#define FGB_CART_MAX_RAM_BANKS 16
#define FGB_CART_ROM_BANK_SIZE 0x4000
#define FGB_CART_RAM_BANK_SIZE 0x2000

enum fgb_cart_type {
    CART_TYPE_ROM_ONLY                       = 0x00,
    CART_TYPE_MBC1                           = 0x01,
    CART_TYPE_MBC1_RAM                       = 0x02,
    CART_TYPE_MBC1_RAM_BATTERY               = 0x03,
    CART_TYPE_MBC2                           = 0x05,
    CART_TYPE_MBC2_BATTERY                   = 0x06,
    CART_TYPE_ROM_RAM                        = 0x08,
    CART_TYPE_ROM_RAM_BATTERY                = 0x09,
    CART_TYPE_MMM01                          = 0x0B,
    CART_TYPE_MMM01_RAM                      = 0x0C,
    CART_TYPE_MMM01_RAM_BATTERY              = 0x0D,
    CART_TYPE_MBC3_TIMER_BATTERY             = 0x0F,
    CART_TYPE_MBC3_TIMER_RAM_BATTERY         = 0x10,
    CART_TYPE_MBC3                           = 0x11,
    CART_TYPE_MBC3_RAM                       = 0x12,
    CART_TYPE_MBC3_RAM_BATTERY               = 0x13,
    CART_TYPE_MBC5                           = 0x19,
    CART_TYPE_MBC5_RAM                       = 0x1A,
    CART_TYPE_MBC5_RAM_BATTERY               = 0x1B,
    CART_TYPE_MBC5_RUMBLE                    = 0x1C,
    CART_TYPE_MBC5_RUMBLE_RAM                = 0x1D,
    CART_TYPE_MBC5_RUMBLE_RAM_BATTERY        = 0x1E,
    CART_TYPE_MBC6                           = 0x20,
    CART_TYPE_MBC7_SENSOR_RUMBLE_RAM_BATTERY = 0x22,
    CART_TYPE_POCKET_CAMERA                  = 0xFC,
    CART_TYPE_BANDAI_TAMA5                   = 0xFD,
    CART_TYPE_HUC3                           = 0xFE,
    CART_TYPE_HUC1_RAM_BATTERY               = 0xFF,
};

enum fgb_cart_rom_size {
    ROM_SIZE_32KIB      = 0x00, // 32 KiB
    ROM_SIZE_64KIB      = 0x01, // 64 KiB
    ROM_SIZE_128KIB     = 0x02, // 128 KiB
    ROM_SIZE_256KIB     = 0x03, // 256 KiB
    ROM_SIZE_512KIB     = 0x04, // 512 KiB
    ROM_SIZE_1MIB       = 0x05, // 1 MiB
    ROM_SIZE_2MIB       = 0x06, // 2 MiB
    ROM_SIZE_4MIB       = 0x07, // 4 MiB
    ROM_SIZE_8MIB       = 0x08, // 8 MiB
    ROM_SIZE_1_1MIB     = 0x52, // 1.1 MiB
    ROM_SIZE_1_2MIB     = 0x53, // 1.2 MiB
    ROM_SIZE_1_5MIB     = 0x54, // 1.5 MiB
};

enum fgb_cart_ram_size {
    RAM_SIZE_0      = 0x00, // No RAM
    RAM_SIZE_8KIB   = 0x02, // 8 KiB x1
    RAM_SIZE_32KIB  = 0x03, // 8 KiB x4
    RAM_SIZE_128KIB = 0x04, // 8 KiB x16
    RAM_SIZE_64KIB  = 0x05, // 8 KiB x8
};

enum fgb_cart_dest_code {
    DEST_CODE_JAPAN     = 0x00,
    DEST_CODE_OVERSEAS  = 0x01,
};

typedef struct fgb_cart_header {
    /* 0x100 */ uint8_t entry_point[4];     // Usually NOP, JP 0x150
    /* 0x104 */ uint8_t logo[48];           // Nintendo logo
    /* 0x134 */ char title[16];             // Game title
    /* 0x144 */ char new_lic_code[2];       // New license code
    /* 0x146 */ uint8_t sgb_flag;           // SGB support flag
    /* 0x147 */ uint8_t cartridge_type;     // Cartridge type
    /* 0x148 */ uint8_t rom_size;           // ROM size
    /* 0x149 */ uint8_t ram_size;           // RAM size
    /* 0x14A */ uint8_t dest_code;          // Destination code
    /* 0x14B */ uint8_t old_lic_code;       // Old license code
    /* 0x14C */ uint8_t mask_rom_version;   // Mask ROM version
    /* 0x14D */ uint8_t header_checksum;    // Header checksum
    /* 0x14E */ uint8_t global_checksum[2]; // Global checksum (2 bytes)
} fgb_cart_header;

typedef struct fgb_cart {
    fgb_cart_header header;
    uint8_t* rom;
    uint8_t* ram;
    size_t rom_size;
    uint8_t rom_bank;
    uint8_t ram_bank;
    uint8_t* rom_banks[FGB_CART_MAX_ROM_BANKS];
    uint8_t* ram_banks[FGB_CART_MAX_RAM_BANKS];
    bool ram_enabled;
    uint8_t(*read)(const struct fgb_cart* cart, uint16_t addr);
    void(*write)(struct fgb_cart* cart, uint16_t addr, uint8_t value);
} fgb_cart;

fgb_cart* fgb_cart_load(const uint8_t* data, size_t size);
void fgb_cart_destroy(fgb_cart* cart);

uint8_t fgb_cart_read(const fgb_cart* cart, uint16_t addr);
void fgb_cart_write(fgb_cart* cart, uint16_t addr, uint8_t value);

#endif // FGB_CART_H
