#include <stdio.h>
#include <stdlib.h>

#include <fgb/emu.h>
#include <fgb/cpu.h>

#include "ulog.h"

size_t file_size(FILE* f) {
    fseek(f, 0, SEEK_END);
    const size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    return size;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <path/to/rom.gb>\n", argv[0]);
        return 1;
    }

    FILE* f;
    errno_t err = fopen_s(&f, argv[1], "rb");
    if (err) {
        printf("Failed to open file %s\n", argv[1]);
        return 1;
    }

    const size_t size = file_size(f);
    uint8_t* data = malloc(size);
    fread(data, 1, size, f);

    fclose(f);

    fgb_emu* emu = fgb_emu_create(data, size);
    free(data);

    if (!emu) {
        printf("Could not create emulator. Exiting\n");
        return 1;
    }
    
    emu->cpu->trace = false;

    while (true) {
        fgb_cpu_step(emu->cpu);
    }
}
