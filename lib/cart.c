#include "cart.h"

#include <stdlib.h>
#include <string.h>

#include <ulog.h>


static uint8_t fgb_compute_header_checksum(const uint8_t* data);
static uint32_t fgb_get_ram_size_bytes(const fgb_cart_header* header);
static uint8_t fgb_cart_read_rom_only(const fgb_cart* cart, uint16_t addr);
static uint8_t fgb_cart_read_mbc3(const fgb_cart* cart, uint16_t addr);

static void fgb_cart_write_rom_only(fgb_cart* cart, uint16_t addr, uint8_t value);
static void fgb_cart_write_mbc3(fgb_cart* cart, uint16_t addr, uint8_t value);

static const uint8_t fgb_nintendo_logo[] = {
    0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
    0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E, 0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
    0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E,
};


fgb_cart* fgb_cart_load(const uint8_t* data, size_t size) {
    fgb_cart* cart = malloc(sizeof(fgb_cart));
    if (!cart) {
        log_error("Failed to allocate Cart");
        return NULL;
    }

    memset(cart, 0, sizeof(fgb_cart));

    cart->header = *(fgb_cart_header*)(data + 0x100);

    if (memcmp(fgb_nintendo_logo, cart->header.logo, sizeof(fgb_nintendo_logo)) != 0) {
        log_error("Nintendo Logo mismatch, aborting cart load");
        fgb_cart_destroy(cart);
        return NULL;
    }

    if (cart->header.header_checksum != fgb_compute_header_checksum(data)) {
        log_error("Header Checksum mismatch, aborting cart load");
        fgb_cart_destroy(cart);
        return NULL;
    }

    cart->rom = malloc(size);
    if (!cart->rom) {
        log_error("Failed to allocate ROM");
        fgb_cart_destroy(cart);
        return NULL;
    }

    const size_t rom_banks = 2ull << cart->header.rom_size;

    const size_t ram_size_bytes = fgb_get_ram_size_bytes(&cart->header);
    const size_t ram_banks = ram_size_bytes / FGB_CART_RAM_BANK_SIZE;

    if (size < rom_banks * FGB_CART_ROM_BANK_SIZE) {
        log_error("ROM size (%zu bytes) is smaller than expected (%zu bytes), aborting cart load", size, rom_banks * FGB_CART_ROM_BANK_SIZE);
        fgb_cart_destroy(cart);
        return NULL;
    }

    switch (cart->header.cartridge_type) {
    case CART_TYPE_ROM_ONLY:
        cart->read = fgb_cart_read_rom_only;
        cart->write = fgb_cart_write_rom_only;
        break;
    case CART_TYPE_MBC3_RAM_BATTERY:
        cart->read = fgb_cart_read_mbc3;
        cart->write = fgb_cart_write_mbc3;
        cart->rom_bank = 1; // MBC3 starts with bank 1 selected

        for (size_t i = 0; i < rom_banks; i++) {
            cart->rom_banks[i] = &cart->rom[i * FGB_CART_ROM_BANK_SIZE];
        }

        if (ram_size_bytes > 0) {
            cart->ram = malloc(ram_size_bytes);
            if (!cart->ram) {
                log_error("Failed to allocate RAM");
                fgb_cart_destroy(cart);
                return NULL;
            }

            memset(cart->ram, 0, ram_size_bytes);
            
            for (size_t i = 0; i < ram_banks; i++) {
                cart->ram_banks[i] = &cart->ram[i * FGB_CART_RAM_BANK_SIZE];
            }
        }
        break;
    case CART_TYPE_ROM_RAM:
    case CART_TYPE_ROM_RAM_BATTERY:
    case CART_TYPE_MBC1:
    case CART_TYPE_MBC1_RAM:
    case CART_TYPE_MBC1_RAM_BATTERY:
    case CART_TYPE_MBC2:
    case CART_TYPE_MBC2_BATTERY:
    case CART_TYPE_MBC3_TIMER_BATTERY:
    case CART_TYPE_MBC3:
    case CART_TYPE_MBC3_RAM:
    case CART_TYPE_MBC3_TIMER_RAM_BATTERY:
    case CART_TYPE_MBC5:
    case CART_TYPE_MBC5_RAM:
    case CART_TYPE_MBC5_RAM_BATTERY:
    case CART_TYPE_MBC5_RUMBLE:
    case CART_TYPE_MBC5_RUMBLE_RAM:
    case CART_TYPE_MBC5_RUMBLE_RAM_BATTERY:
    case CART_TYPE_HUC3:
    case CART_TYPE_HUC1_RAM_BATTERY:
    default:
        log_warn("Only ROM_ONLY Carts are supported. Game will not work properly");
        break;
    }

    memcpy(cart->rom, data, size);
    cart->rom_size = size;

    return cart;
}

void fgb_cart_destroy(fgb_cart* cart) {
    free(cart->rom);
    free(cart->ram);
    free(cart);
}

uint8_t fgb_cart_read(const fgb_cart* cart, uint16_t addr) {
    return cart->read(cart, addr);
}

void fgb_cart_write(fgb_cart* cart, uint16_t addr, uint8_t value) {
    cart->write(cart, addr, value);
}

uint8_t fgb_compute_header_checksum(const uint8_t* data) {
    uint8_t checksum = 0;
    for (uint16_t addr = 0x134; addr <= 0x14C; addr++) {
        checksum -= data[addr] + 1;
    }

    return checksum;
}

uint32_t fgb_get_ram_size_bytes(const fgb_cart_header* header) {
    switch (header->ram_size) {
    case RAM_SIZE_0:
        return 0;
    case RAM_SIZE_8KIB:
        return FGB_CART_RAM_BANK_SIZE * 1;
    case RAM_SIZE_32KIB:
        return FGB_CART_RAM_BANK_SIZE * 4;
    case RAM_SIZE_128KIB:
        return FGB_CART_RAM_BANK_SIZE * 16;
    case RAM_SIZE_64KIB:
        return FGB_CART_RAM_BANK_SIZE * 8;
    default:
        log_warn("Unknown RAM size code: 0x%02X", header->ram_size);
        return 0;
    }
}

uint8_t fgb_cart_read_rom_only(const fgb_cart* cart, uint16_t addr) {
    return cart->rom[addr];
}

uint8_t fgb_cart_read_mbc3(const fgb_cart* cart, uint16_t addr) {
    if (addr < 0x4000) {
        // Bank 0
        return cart->rom[addr];
    }

    if (addr < 0x8000) {
        // Switchable ROM bank
        return cart->rom_banks[cart->rom_bank][addr - 0x4000];
    }

    if (addr >= 0xA000 && addr < 0xC000) {
        // Switchable RAM bank
        if (!cart->ram_enabled || !cart->ram) {
            log_warn("Attempt to read from RAM when RAM is disabled or not present");
            return 0xFF;
        }

        return cart->ram_banks[cart->ram_bank][addr - 0xA000];
    }

    log_warn("Attempt to read from unmapped MBC3 memory at address 0x%04X", addr);
    return 0xFF;
}

void fgb_cart_write_rom_only(fgb_cart* cart, uint16_t addr, uint8_t value) {
    (void)cart;
    (void)addr;
    (void)value;
    log_warn("Attempt to write to ROM_ONLY cart at 0x%04X", addr);
}

void fgb_cart_write_mbc3(fgb_cart* cart, uint16_t addr, uint8_t value) {
    if (addr < 0x2000) {
        // Enable/Disable RAM
        cart->ram_enabled = (value & 0x0F) == 0x0A;
        return;
    }

    if (addr < 0x4000) {
        // Switch ROM Bank
        cart->rom_bank = value ? value & 0x7F : 1;
        return;
    }

    if (addr < 0x6000) {
        // Switch RAM Bank
        if (value < 4) {
            cart->ram_bank = value;
        } else {
            log_warn("RTC not implemented (%d)", value);
        }

        return;
    }

    if (addr >= 0xA000 && addr < 0xC000 && cart->ram_enabled) {
        cart->ram_banks[cart->ram_bank][addr - 0xA000] = value;
        return;
    }

    log_warn("Attempt to write to unmapped MBC3 memory at address 0x%04X", addr);
}

