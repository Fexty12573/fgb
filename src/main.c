#include <stdio.h>
#include <stdlib.h>

#include <fgb/emu.h>
#include <fgb/cpu.h>
#include <GLFW/glfw3.h>

#include "ulog.h"

size_t file_size(FILE* f) {
    fseek(f, 0, SEEK_END);
    const size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    return size;
}

static fgb_emu* emu_init(const char* rom_path) {
    FILE* f;
    errno_t err = fopen_s(&f, rom_path, "rb");
    if (err) {
        printf("Failed to open file %s\n", rom_path);
        exit(1);
    }

    const size_t size = file_size(f);
    uint8_t* data = malloc(size);
    fread(data, 1, size, f);

    fclose(f);

    fgb_emu* emu = fgb_emu_create(data, size);
    free(data);

    return emu;
}

static GLFWwindow* window_init(void) {
    if (!glfwInit()) {
        printf("Failed to initialize GLFW\n");
        exit(1);
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(640, 480, "fgb", NULL, NULL);
    if (!window) {
        printf("Failed to create window\n");
        glfwTerminate();
        exit(1);
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable VSync

    return window;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <path/to/rom.gb>\n", argv[0]);
        return 1;
    }

    GLFWwindow* window = window_init();
    if (!window) {
        printf("Could not create window. Exiting\n");
        return 1;
    }

    fgb_emu* emu = emu_init(argv[1]);
    if (!emu) {
        printf("Could not create emulator. Exiting\n");
        return 1;
    }
    
    emu->cpu->trace = false;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, true);
        }

        fgb_cpu_step(emu->cpu);

        glfwSwapBuffers(window);
    }
}
