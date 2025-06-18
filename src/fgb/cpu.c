#include "cpu.h"

#include <stdlib.h>
#include <string.h>


static int fgb_cpu_execute(fgb_cpu* cpu);

struct fgb_init_value {
    uint16_t addr;
    uint8_t value;
};
static const struct fgb_init_value fgb_init_table[] = {
    { 0xFF00, 0xCF }, // P1
    { 0xFF01, 0x00 }, // SB
    { 0xFF02, 0x7E }, // SC
    { 0xFF04, 0x18 }, // DIV
    { 0xFF05, 0x00 }, // TIMA
    { 0xFF06, 0x00 }, // TMA
    { 0xFF07, 0xF8 }, // TAC
    { 0xFF0F, 0xE1 }, // IF
    { 0xFF10, 0x80 }, // NR10
    { 0xFF11, 0xBF }, // NR11
    { 0xFF12, 0xF3 }, // NR12
    { 0xFF13, 0xFF }, // NR13
    { 0xFF14, 0xBF }, // NR14
    { 0xFF16, 0x3F }, // NR21
    { 0xFF17, 0x00 }, // NR22
    { 0xFF18, 0xFF }, // NR23
    { 0xFF19, 0xBF }, // NR24
    { 0xFF1A, 0x7F }, // NR30
    { 0xFF1B, 0xFF }, // NR31
    { 0xFF1C, 0x9F }, // NR32
    { 0xFF1D, 0xFF }, // NR33
    { 0xFF1E, 0xBF }, // NR34
    { 0xFF20, 0xFF }, // NR41
    { 0xFF21, 0x00 }, // NR42
    { 0xFF22, 0x00 }, // NR43
    { 0xFF23, 0xBF }, // NR44
    { 0xFF24, 0x77 }, // NR50
    { 0xFF25, 0xF3 }, // NR51
    { 0xFF26, 0xF1 }, // NR52
    { 0xFF40, 0x91 }, // LCDC
    { 0xFF41, 0x81 }, // STAT
    { 0xFF42, 0x00 }, // SCY
    { 0xFF43, 0x00 }, // SCX
    { 0xFF44, 0x00 }, // LY
    { 0xFF45, 0x00 }, // LYC
    { 0xFF46, 0xFF }, // DMA
    { 0xFF47, 0xFC }, // BGP
    { 0xFF48, 0xFF }, // OBP0
    { 0xFF49, 0xFF }, // OBP1
    { 0xFF4A, 0x00 }, // WY
    { 0xFF4B, 0x00 }, // WX
};


fgb_cpu* fgb_cpu_create(void) {
    fgb_cpu* cpu = malloc(sizeof(fgb_cpu));
    if (!cpu) {
        return NULL;
    }

    fgb_cpu_reset(cpu);

    return NULL;
}

void fgb_cpu_destroy(fgb_cpu* cpu) {
    free(cpu);
}

void fgb_cpu_reset(fgb_cpu* cpu) {
    memset(cpu, 0, sizeof(fgb_cpu));

    cpu->regs.pc = 0x0100;
    cpu->regs.sp = 0xFFFE;

    cpu->regs.af = 0x01B0;
    cpu->regs.bc = 0x0013;
    cpu->regs.de = 0x00D8;
    cpu->regs.hl = 0x014D;
    
    for (size_t i = 0; i < sizeof(fgb_init_table) / sizeof(fgb_init_table[0]); i++) {
        fgb_mem_write(&cpu->memory, fgb_init_table[i].addr, fgb_init_table[i].value);
    }
}

void fgb_cpu_step(fgb_cpu* cpu) {
    int cycles = 0;

    while (cycles < FGB_CYCLES_PER_FRAME) {
        
    }
}

int fgb_cpu_execute(fgb_cpu* cpu) {
    return 0;
}
