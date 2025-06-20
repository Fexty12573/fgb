#include "cart.h"

#include <stdlib.h>
#include <string.h>

#include <ulog.h>


static uint8_t fgb_compute_header_checksum(const uint8_t* data);

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

    if (cart->header.cartridge_type != CART_TYPE_ROM_ONLY) {
        log_warn("Only ROM_ONLY Carts are supported. Game will not work properly");
    }

    memcpy(cart->rom, data, size);
    cart->rom_size = size;

    return cart;
}

void fgb_cart_destroy(fgb_cart* cart) {
    free(cart->rom);
    free(cart);
}

uint8_t fgb_cart_read(const fgb_cart* cart, uint16_t addr) {
    return cart->rom[addr];
}

void fgb_cart_write(const fgb_cart* cart, uint16_t addr, uint8_t value) {
    log_warn("Cart write at 0x%04X. Not supported.", addr);
    (void)cart;
    (void)value;
}

uint8_t fgb_compute_header_checksum(const uint8_t* data) {
    uint8_t checksum = 0;
    for (uint16_t addr = 0x134; addr <= 0x14C; addr++) {
        checksum -= data[addr] + 1;
    }

    return checksum;
}
