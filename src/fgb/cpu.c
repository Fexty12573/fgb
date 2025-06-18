#include "cpu.h"

#include <stdlib.h>
#include <string.h>


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

    cpu->memory[0xFF00] = 0xCF; // P1
    cpu->memory[0xFF01] = 0x00; // SB
    cpu->memory[0xFF02] = 0x7E; // SC
    cpu->memory[0xFF04] = 0x18; // DIV
    cpu->memory[0xFF05] = 0x00; // TIMA
    cpu->memory[0xFF06] = 0x00; // TMA
    cpu->memory[0xFF07] = 0xF8; // TAC
    cpu->memory[0xFF0F] = 0xE1; // IF
    cpu->memory[0xFF10] = 0x80; // NR10
    cpu->memory[0xFF11] = 0xBF; // NR11
    cpu->memory[0xFF12] = 0xF3; // NR12
    cpu->memory[0xFF13] = 0xFF; // NR13
    cpu->memory[0xFF14] = 0xBF; // NR14
    cpu->memory[0xFF16] = 0x3F; // NR21
    cpu->memory[0xFF17] = 0x00; // NR22
    cpu->memory[0xFF18] = 0xFF; // NR23
    cpu->memory[0xFF19] = 0xBF; // NR24
    cpu->memory[0xFF1A] = 0x7F; // NR30
    cpu->memory[0xFF1B] = 0xFF; // NR31
    cpu->memory[0xFF1C] = 0x9F; // NR32
    cpu->memory[0xFF1D] = 0xFF; // NR33
    cpu->memory[0xFF1E] = 0xBF; // NR34
    cpu->memory[0xFF20] = 0xFF; // NR41
    cpu->memory[0xFF21] = 0x00; // NR42
    cpu->memory[0xFF22] = 0x00; // NR43
    cpu->memory[0xFF23] = 0xBF; // NR44
    cpu->memory[0xFF24] = 0x77; // NR50
    cpu->memory[0xFF25] = 0xF3; // NR51
    cpu->memory[0xFF26] = 0xF1; // NR52
    cpu->memory[0xFF40] = 0x91; // LCDC
    cpu->memory[0xFF41] = 0x81; // STAT
    cpu->memory[0xFF42] = 0x00; // SCY
    cpu->memory[0xFF43] = 0x00; // SCX
    cpu->memory[0xFF44] = 0x00; // LY
    cpu->memory[0xFF45] = 0x00; // LYC
    cpu->memory[0xFF46] = 0xFF; // DMA
    cpu->memory[0xFF47] = 0xFC; // BGP
    cpu->memory[0xFF48] = 0xFF; // OBP0
    cpu->memory[0xFF49] = 0xFF; // OBP1
    cpu->memory[0xFF4A] = 0x00; // WY
    cpu->memory[0xFF4B] = 0x00; // WX
    cpu->memory[0xFFFF] = 0x00; // IE
}

