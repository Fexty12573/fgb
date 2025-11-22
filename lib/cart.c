#include "cart.h"
#include "cpu.h"

#include <stdlib.h>
#include <string.h>

#include <ulog.h>

#define MAKE_RTC_DAYS(HIGH, LOW) ((((((uint16_t)HIGH) & 0x01) << 8) | (uint16_t)(LOW)))

static uint8_t fgb_compute_header_checksum(const uint8_t* data);
static uint32_t fgb_get_ram_size_bytes(const fgb_cart_header* header);
static bool fgb_cart_map_banks(fgb_cart* cart);

static uint8_t fgb_cart_read_rom_only(const fgb_cart* cart, uint16_t addr);
static void fgb_cart_write_rom_only(fgb_cart* cart, uint16_t addr, uint8_t value);

static uint8_t fgb_cart_read_mbc3(const fgb_cart* cart, uint16_t addr);
static void fgb_cart_write_mbc3(fgb_cart* cart, uint16_t addr, uint8_t value);
static void fgb_cart_tick_mbc3(fgb_cart* cart);

static uint8_t fgb_cart_read_mbc1(const fgb_cart* cart, uint16_t addr);
static void fgb_cart_write_mbc1(fgb_cart* cart, uint16_t addr, uint8_t value);

static uint8_t fgb_cart_read_mbc2(const fgb_cart* cart, uint16_t addr);
static void fgb_cart_write_mbc2(fgb_cart* cart, uint16_t addr, uint8_t value);

static uint8_t fgb_cart_read_mbc5(const fgb_cart* cart, uint16_t addr);
static void fgb_cart_write_mbc5(fgb_cart* cart, uint16_t addr, uint8_t value);

static bool fgb_cart_has_rumble(const fgb_cart* cart);
static size_t fgb_cart_get_rom_banks(enum fgb_cart_rom_size size);

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

    const size_t rom_banks = fgb_cart_get_rom_banks((enum fgb_cart_rom_size)cart->header.rom_size);
    cart->ram_size_bytes = fgb_get_ram_size_bytes(&cart->header);
    cart->rom_bank_mask = (uint8_t)(rom_banks - 1ull);

    if (!fgb_cart_map_banks(cart)) {
        fgb_cart_destroy(cart);
        return NULL;
    }

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
    case CART_TYPE_MBC1_RAM_BATTERY:
        cart->has_ram_battery = true;
    case CART_TYPE_MBC1:
    case CART_TYPE_MBC1_RAM:
        cart->read = fgb_cart_read_mbc1;
        cart->write = fgb_cart_write_mbc1;
        cart->rom_bank = 1; // MBC1 starts with bank 1 selected
        break;
    case CART_TYPE_MBC2_BATTERY:
        cart->has_ram_battery = true;
    case CART_TYPE_MBC2:
        cart->read = fgb_cart_read_mbc2;
        cart->write = fgb_cart_write_mbc2;
        cart->ram_size_bytes = 512; // Allocating 512 bytes, only lower 4 bits of each byte are used
        cart->ram = calloc(1, cart->ram_size_bytes); // MBC2 has built-in 512 x 4 bits RAM
        if (!cart->ram) {
            log_error("Failed to allocate MBC2 RAM");
            fgb_cart_destroy(cart);
            return NULL;
        }
        break;
    case CART_TYPE_MBC3_RAM_BATTERY:
    case CART_TYPE_MBC3_TIMER_RAM_BATTERY:
        cart->has_ram_battery = true;
    case CART_TYPE_MBC3_TIMER_BATTERY:
    case CART_TYPE_MBC3:
    case CART_TYPE_MBC3_RAM:
        cart->read = fgb_cart_read_mbc3;
        cart->write = fgb_cart_write_mbc3;
        cart->tick = fgb_cart_tick_mbc3;
        cart->rom_bank = 1; // MBC3 starts with bank 1 selected
        break;
    case CART_TYPE_MBC5_RAM_BATTERY:
    case CART_TYPE_MBC5_RUMBLE_RAM_BATTERY:
        cart->has_ram_battery = true;
    case CART_TYPE_MBC5:
    case CART_TYPE_MBC5_RAM:
    case CART_TYPE_MBC5_RUMBLE:
    case CART_TYPE_MBC5_RUMBLE_RAM:
        cart->has_rumble = fgb_cart_has_rumble(cart);
        cart->read = fgb_cart_read_mbc5;
        cart->write = fgb_cart_write_mbc5;
        break;
    case CART_TYPE_ROM_RAM:
    case CART_TYPE_ROM_RAM_BATTERY:
    case CART_TYPE_HUC3:
    case CART_TYPE_HUC1_RAM_BATTERY:
    default:
        log_warn("Unsupported cart type 0x%02X. Game will not work properly", cart->header.cartridge_type);
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

const uint8_t* fgb_cart_get_battery_buffered_ram(const fgb_cart* cart) {
    if (cart->has_ram_battery && cart->ram) {
        return cart->ram;
    }

    return NULL;
}

bool fgb_cart_load_battery_buffered_ram(const fgb_cart* cart, const uint8_t* data, size_t size) {
    if (cart->ram_size_bytes != size) {
        log_error("Battery RAM size mismatch: expected %zu bytes, got %zu bytes", cart->ram_size_bytes, size);
        return false;
    }

    memcpy(cart->ram, data, size);

    return true;
}

size_t fgb_cart_get_ram_size(const fgb_cart* cart) {
    return cart->ram_size_bytes;
}

uint8_t fgb_cart_read(const fgb_cart* cart, uint16_t addr) {
    return cart->read(cart, addr);
}

void fgb_cart_write(fgb_cart* cart, uint16_t addr, uint8_t value) {
    cart->write(cart, addr, value);
}

void fgb_cart_tick(fgb_cart *cart) {
    if (cart->tick) {
        cart->tick(cart);
    }
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

bool fgb_cart_map_banks(fgb_cart* cart) {
    const size_t rom_banks = 2ull << cart->header.rom_size;
    const size_t ram_banks = cart->ram_size_bytes / FGB_CART_RAM_BANK_SIZE;

    for (size_t i = 0; i < rom_banks; i++) {
        cart->rom_banks[i] = &cart->rom[i * FGB_CART_ROM_BANK_SIZE];
    }

    if (cart->ram_size_bytes > 0) {
        cart->ram = malloc(cart->ram_size_bytes);
        if (!cart->ram) {
            log_error("Failed to allocate RAM");
            fgb_cart_destroy(cart);
            return false;
        }

        memset(cart->ram, 0, cart->ram_size_bytes);

        for (size_t i = 0; i < ram_banks; i++) {
            cart->ram_banks[i] = &cart->ram[i * FGB_CART_RAM_BANK_SIZE];
        }
    }

    return true;
}

uint8_t fgb_cart_read_rom_only(const fgb_cart* cart, uint16_t addr) {
    return cart->rom[addr];
}

void fgb_cart_write_rom_only(fgb_cart* cart, uint16_t addr, uint8_t value) {
    (void)cart;
    (void)addr;
    (void)value;
    log_warn("Attempt to write to ROM_ONLY cart at 0x%04X", addr);
}

#define seconds regs[RTC_S]
#define minutes regs[RTC_M]
#define hours regs[RTC_H]
#define days_low regs[RTC_DL]
#define days_high regs[RTC_DH]
#define halt(dh) ((dh >> 6) & 0x01)

uint8_t fgb_cart_read_mbc3(const fgb_cart* cart, uint16_t addr) {
    if (addr < 0x4000) {
        // Bank 0
        return cart->rom[addr];
    }

    if (addr < 0x8000) {
        // Switchable ROM bank
        return cart->rom_banks[cart->rom_bank][addr - 0x4000];
    }

    if (addr >= 0xA000 && addr < 0xC000 && cart->ram_enabled) {
        if (cart->ram_bank < 4) {
            // RAM bank
            const uint32_t offset = (addr - 0xA000) % cart->ram_size_bytes;
            return cart->ram_banks[cart->ram_bank][offset];
        }

        // RTC register
        switch (cart->ram_bank) {
        case RTC_REG_START + RTC_S:
            return cart->rtc.latch[RTC_S];
        case RTC_REG_START + RTC_M:
            return cart->rtc.latch[RTC_M];
        case RTC_REG_START + RTC_H:
            return cart->rtc.latch[RTC_H];
        case RTC_REG_START + RTC_DL:
            return cart->rtc.latch[RTC_DL];
        case RTC_REG_START + RTC_DH:
            return cart->rtc.latch[RTC_DH];
        default:
            return 0xFF;
        }
    }
    
    log_warn("Attempt to read from unmapped MBC3 memory at address 0x%04X", addr);
    return 0xFF;
}

void fgb_cart_write_mbc3(fgb_cart* cart, uint16_t addr, uint8_t value) {
    if (addr < 0x2000) {
        // Enable/Disable RAM
        cart->ram_enabled = (value & 0x0F) == 0x0A;
        return;
    }

    if (addr < 0x4000) {
        // Switch ROM Bank
        cart->rom_bank = value ? value & cart->rom_bank_mask : 1;
        return;
    }

    if (addr < 0x6000) {
        if (value <= 0x03) {
            // Switch RAM Bank
            cart->ram_bank = value;
        } else if (value >= 0x08 && value <= 0x0C) {
            // RTC Register select
            cart->ram_bank = value;
        } else {
            log_warn("Invalid MBC3 RAM bank/RTC register select value: 0x%02X", value);
        }

        return;
    }

    if (addr < 0x8000) {
        if (value == 1 && cart->rtc.last_latch == 0) {
            // Latch RTC data
            cart->rtc.latch[RTC_S] = cart->rtc.seconds;
            cart->rtc.latch[RTC_M] = cart->rtc.minutes;
            cart->rtc.latch[RTC_H] = cart->rtc.hours;
            cart->rtc.latch[RTC_DL] = cart->rtc.days_low;
            cart->rtc.latch[RTC_DH] = cart->rtc.days_high;
        }

        cart->rtc.last_latch = value;

        return;
    }

    if (addr >= 0xA000 && addr < 0xC000 && cart->ram_enabled) {
        if (cart->ram_bank < 4 && cart->ram_size_bytes > 0) {
            // RAM bank
            const uint32_t offset = (addr - 0xA000) % cart->ram_size_bytes;
            cart->ram_banks[cart->ram_bank][offset] = value;
            return;
        }

        // RTC register
        switch (cart->ram_bank) {
        case RTC_REG_START + RTC_S:
            cart->rtc.seconds = cart->rtc.latch[RTC_S] = value & 0x3F;
            cart->rtc.cycles = 0;
            break;
        case RTC_REG_START + RTC_M:
            cart->rtc.minutes = cart->rtc.latch[RTC_M] = value & 0x3F;
            break;
        case RTC_REG_START + RTC_H:
            cart->rtc.hours = cart->rtc.latch[RTC_H] = value & 0x1F;
            break;
        case RTC_REG_START + RTC_DL:
            cart->rtc.days_low = cart->rtc.latch[RTC_DL] = value;
            break;
        case RTC_REG_START + RTC_DH:
            cart->rtc.days_high = cart->rtc.latch[RTC_DH] = value & 0xC1; // Only bits 0, 6, and 7 are used
            break;
        default:
            break;
        }

        return;
    }

    log_warn("Attempt to write to unmapped MBC3 memory at address 0x%04X", addr);
}

void fgb_cart_tick_mbc3(fgb_cart *cart) {
    if (halt(cart->rtc.days_high)) {
        return;
    }

    cart->rtc.cycles++;
    if (cart->rtc.cycles >= FGB_CPU_CLOCK_SPEED) {
        cart->rtc.cycles = 0;

        // Note: The following code intentionally doesn't use '<' or '>=' to match the MBC3 behavior.
        // When writing 61 to RTC_S, the register continues incrementing to 62, 63, 0, 1, ...
        // However, when it overflows from 63 to 0, the minutes are not incremented until it actually reaches 60.
        // The same applies to minutes and hours.

        cart->rtc.seconds = (cart->rtc.seconds + 1) & 0x3F;
        if (cart->rtc.seconds != 60) {
            return;
        }

        cart->rtc.seconds = 0;
        cart->rtc.minutes = (cart->rtc.minutes + 1) & 0x3F;
        if (cart->rtc.minutes != 60) {
            return;
        }

        cart->rtc.minutes = 0;
        cart->rtc.hours = (cart->rtc.hours + 1) & 0x1F;
        if (cart->rtc.hours != 24) {
            return;
        }

        cart->rtc.hours = 0;
        uint16_t days = MAKE_RTC_DAYS(cart->rtc.days_high, cart->rtc.days_low);
        days++;
        if (days == 512) {
            days = 0;
            cart->rtc.days_high |= 0x80; // Set day carry bit
        }

        cart->rtc.days_low = (uint8_t)(days & 0xFF);
        cart->rtc.days_high = (cart->rtc.days_high & 0xFE) | ((days >> 8) & 0x01);
    }
}

#undef seconds
#undef minutes
#undef hours
#undef days_low
#undef days_high
#undef halt

uint8_t fgb_cart_read_mbc1(const fgb_cart* cart, uint16_t addr) {
    if (addr < 0x4000) {
        if (cart->mode == CART_MODE_SIMPLE) {
            // Bank 0
            return cart->rom[addr];
        }

        // Switchable ROM bank (using upper bits)
        return cart->rom_banks[cart->ram_bank][addr];
    }

    if (addr < 0x8000) {
        return cart->rom_banks[(cart->ram_bank << 5) | cart->rom_bank][addr - 0x4000];
    }

    if (addr >= 0xA000 && addr < 0xC000 && cart->ram_enabled && cart->ram_size_bytes > 0) {
        const uint32_t offset = (addr - 0xA000) % cart->ram_size_bytes;

        if (cart->mode == CART_MODE_SIMPLE) {
            return cart->ram_banks[0][offset];
        }

        return cart->ram_banks[cart->ram_bank][offset];
    }

    log_warn("Attempt to read from unmapped MBC1 memory at address 0x%04X", addr);
    return 0xFF;
}

void fgb_cart_write_mbc1(fgb_cart* cart, uint16_t addr, uint8_t value) {
    if (addr < 0x2000) {
        cart->ram_enabled = (value & 0x0F) == 0x0A;
        return;
    }

    if (addr < 0x4000) {
        cart->rom_bank = value
            ? value & cart->rom_bank_mask & 0x1F // Register is still 5 bits
            : 1;
        return;
    }

    if (addr < 0x6000) {
        cart->ram_bank = value & 0x03;
        return;
    }

    if (addr < 0x8000) {
        cart->mode = value & 0x1;
        return;
    }

    if (addr >= 0xA000 && addr < 0xC000 && cart->ram_enabled && cart->ram_size_bytes > 0) {
        const uint32_t offset = (addr - 0xA000) % cart->ram_size_bytes;

        if (cart->mode == CART_MODE_SIMPLE) {
            cart->ram_banks[0][offset] = value;
        } else {
            cart->ram_banks[cart->ram_bank][offset] = value;
        }

        return;
    }

    log_warn("Attempt to write to unmapped MBC1 memory at address 0x%04X", addr);
}

uint8_t fgb_cart_read_mbc2(const fgb_cart* cart, uint16_t addr) {
    if (addr < 0x4000) {
        // Bank 0
        return cart->rom[addr];
    }

    if (addr < 0x8000) {
        // Switchable ROM bank
        return cart->rom_banks[cart->rom_bank][addr - 0x4000];
    }

    if (addr >= 0xA000 && addr < 0xC000 && cart->ram_enabled) {
        return cart->ram[addr & 0x1FF] | 0xF0;
    }

    return 0xFF;
}

void fgb_cart_write_mbc2(fgb_cart* cart, uint16_t addr, uint8_t value) {
    if (addr < 0x4000) {
        if (addr & 0x100) {
            cart->rom_bank = value ? value & cart->rom_bank_mask & 0xF : 1;
        } else {
            cart->ram_enabled = (value & 0x0F) == 0x0A;
        }

        return;
    }

    if (addr >= 0xA000 && addr < 0xC000 && cart->ram_enabled) {
        cart->ram[addr & 0x1FF] = value & 0x0F;
    }
}

uint8_t fgb_cart_read_mbc5(const fgb_cart* cart, uint16_t addr) {
    if (addr < 0x4000) {
        return cart->rom[addr];
    }

    if (addr < 0x8000) {
        const int bank = cart->rom_bank_high << 8 | cart->rom_bank;
        return cart->rom_banks[bank][addr - 0x4000];
    }

    if (addr >= 0xA000 && addr < 0xC000 && cart->ram_enabled && cart->ram_size_bytes > 0) {
        return cart->ram_banks[cart->ram_bank][(addr - 0xA000) % cart->ram_size_bytes];
    }

    return 0xFF;
}

void fgb_cart_write_mbc5(fgb_cart* cart, uint16_t addr, uint8_t value) {
    if (addr < 0x2000) {
        cart->ram_enabled = (value & 0x0F) == 0x0A;
        return;
    }

    if (addr < 0x3000) {
        cart->rom_bank = value;
        return;
    }

    if (addr < 0x4000) {
        cart->rom_bank_high = value & 1;
        return;
    }

    if (addr < 0x6000) {
        if (cart->has_rumble) {
            cart->ram_bank = value & 0x7; // Bit 3 is mapped to the rumble motor
            cart->rumble_enabled = (value & 0x8) != 0;
        } else {
            cart->ram_bank = value & 0xF;
        }
    }

    if (addr >= 0xA000 && addr < 0xC000 && cart->ram_enabled && cart->ram_size_bytes > 0) {
        cart->ram_banks[cart->ram_bank][(addr - 0xA000) % cart->ram_size_bytes] = value;
    }
}

bool fgb_cart_has_rumble(const fgb_cart* cart) {
    const enum fgb_cart_type type = (enum fgb_cart_type)cart->header.cartridge_type;
    return type == CART_TYPE_MBC5_RUMBLE ||
        type == CART_TYPE_MBC5_RUMBLE_RAM ||
        type == CART_TYPE_MBC5_RUMBLE_RAM_BATTERY ||
        type == CART_TYPE_MBC7_SENSOR_RUMBLE_RAM_BATTERY;
}

size_t fgb_cart_get_rom_banks(enum fgb_cart_rom_size size) {
    switch (size) {
    case ROM_SIZE_32KIB: return 2;
    case ROM_SIZE_64KIB: return 4;
    case ROM_SIZE_128KIB: return 8;
    case ROM_SIZE_256KIB: return 16;
    case ROM_SIZE_512KIB: return 32;
    case ROM_SIZE_1MIB: return 64;
    case ROM_SIZE_2MIB: return 128;
    case ROM_SIZE_4MIB: return 256;
    case ROM_SIZE_8MIB: return 512;
    case ROM_SIZE_1_1MIB: return 72;
    case ROM_SIZE_1_2MIB: return 80;
    case ROM_SIZE_1_5MIB: return 96;
    }

    return 2;
}

