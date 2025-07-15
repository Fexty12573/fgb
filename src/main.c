#include "render.h"

#include <stdio.h>
#include <stdlib.h>

#include <fgb/emu.h>
#include <fgb/cpu.h>

#include <GL/glew.h>
#include <GL/GL.h>
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

    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);

    GLFWwindow* window = glfwCreateWindow(640, 320, "fgb", NULL, NULL);
    if (!window) {
        printf("Failed to create window\n");
        glfwTerminate();
        exit(1);
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable VSync

    return window;
}

static void gl_init(void) {
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        printf("Failed to initialize GLEW: %s\n", glewGetErrorString(err));
        exit(1);
    }
}

static const char* const VERTEX_SHADER_SOURCE =
"#version 450 core\n"
"layout(location = 0) in vec3 position;\n"
"layout(location = 1) in vec2 texCoord;\n"
"out vec2 TexCoord;\n"
"void main() {\n"
"    gl_Position = vec4(position, 1.0);\n"
"    TexCoord = texCoord;\n"
"}\n";

static const char* const FRAGMENT_SHADER_SOURCE =
"#version 450 core\n"
"out vec4 color;\n"
"uniform sampler2D texture1;\n"
"in vec2 TexCoord;\n"
"void main() {\n"
"    color = texture(texture1, TexCoord);\n"
"}\n";

static uint32_t shader_init(void) {
    uint32_t vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &VERTEX_SHADER_SOURCE, NULL);
    glCompileShader(vertex_shader);

    int success;
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetShaderInfoLog(vertex_shader, 512, NULL, info_log);
        printf("Vertex Shader Compilation Failed: %s\n", info_log);
        return 0;
    }

    uint32_t fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &FRAGMENT_SHADER_SOURCE, NULL);

    glCompileShader(fragment_shader);
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetShaderInfoLog(fragment_shader, 512, NULL, info_log);
        printf("Fragment Shader Compilation Failed: %s\n", info_log);
        return 0;
    }

    uint32_t shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);

    glLinkProgram(shader_program);

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    return shader_program;
}

int block_to_display = 0;
fgb_emu* emu = NULL;

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_B && action == GLFW_PRESS) {
        block_to_display = (block_to_display + 1) % TILE_BLOCK_COUNT;
        log_info("Displaying block %d", block_to_display);
    }
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_T) {
            emu->cpu->trace = !emu->cpu->trace;
            log_info("CPU trace %s", emu->cpu->trace ? "enabled" : "disabled");
        }
        if (key == GLFW_KEY_R) {
            fgb_cpu_reset(emu->cpu);
            log_info("CPU reset");
        }
        if (key == GLFW_KEY_D) {
            fgb_cpu_disassemble(emu->cpu, emu->cpu->regs.pc, 10);
        }
        if (key == GLFW_KEY_S) {
            fgb_cpu_dump_state(emu->cpu);
        }
        if (key == GLFW_KEY_N) {
            if (emu->cpu->debugging) {
                emu->cpu->do_step = true;
            }
        }
        if (key == GLFW_KEY_C) {
            if (emu->cpu->debugging) {
                emu->cpu->debugging = false;
                log_info("Continuing execution");
            }
        }
        if (key == GLFW_KEY_P) { // Set breakpoint at current PC
            fgb_cpu_set_bp(emu->cpu, emu->cpu->regs.pc);
            log_info("Breakpoint set at 0x%04X", emu->cpu->regs.pc);
        }
        if (key == GLFW_KEY_O) { // Clear breakpoint at current PC
            fgb_cpu_clear_bp(emu->cpu, emu->cpu->regs.pc);
            log_info("Breakpoint cleared at 0x%04X", emu->cpu->regs.pc);
        }
    }
}

static void gl_debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
    switch (severity) {
    case GL_DEBUG_SEVERITY_HIGH:
        printf("OpenGL Error: %s (Source: %d, Type: %d, ID: %d)", message, source, type, id);
        break;

    case GL_DEBUG_SEVERITY_MEDIUM:
        printf("OpenGL Warning: %s (Source: %d, Type: %d, ID: %d)", message, source, type, id);
        break;

    case GL_DEBUG_SEVERITY_LOW:
        printf("OpenGL Info: %s (Source: %d, Type: %d, ID: %d)", message, source, type, id);
        break;

    case GL_DEBUG_SEVERITY_NOTIFICATION:
        printf("OpenGL Notification: %s (Source: %d, Type: %d, ID: %d)", message, source, type, id);
        break;
    }
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

    gl_init();

    uint32_t shader = shader_init();
    if (!shader) {
        printf("Could not create shader program. Exiting\n");
        return 1;
    }
    
    emu = emu_init(argv[1]);
    if (!emu) {
        printf("Could not create emulator. Exiting\n");
        return 1;
    }
    
    ulog_set_quiet(false);
    ulog_set_level(LOG_DEBUG);
    fgb_emu_set_log_level(emu, LOG_DEBUG);
    emu->cpu->trace = false;

    fgb_cpu_set_bp(emu->cpu, 0x231);

    glDebugMessageCallback(gl_debug_callback, NULL);

    const int tiles_per_row = 16; // Number of tiles per row in the texture
    uint32_t block_textures[TILE_BLOCK_COUNT] = { 0 };
    block_textures[0] = fgb_create_tile_block_texture(tiles_per_row);
    block_textures[1] = fgb_create_tile_block_texture(tiles_per_row);
    block_textures[2] = fgb_create_tile_block_texture(tiles_per_row);

    uint32_t va, vb, ib;
    fgb_create_quad(&va, &vb, &ib);

    const fgb_palette tile_pal = {
        .colors = {
            0xFF000000, // Color 0: Black
            0xFF606060, // Color 1: Dark Gray
            0xFFB0B0B0, // Color 2: Light Gray
            0xFFFFFFFF  // Color 3: White
        }
    };

    const fgb_palette obj_pal = {
        .colors = {
            0x00000000, // Color 0: Transparent
            0xFF000000, // Color 1: Black
            0xFF808080, // Color 2: Light Gray
            0xFFFFFFFF  // Color 3: White
        }
    };

    glfwSetKeyCallback(window, key_callback);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        fgb_cpu_step(emu->cpu);

        fgb_upload_tile_block_texture(block_textures[0], tiles_per_row, emu->ppu, 0, &tile_pal);
        fgb_upload_tile_block_texture(block_textures[1], tiles_per_row, emu->ppu, 1, &tile_pal);
        fgb_upload_tile_block_texture(block_textures[2], tiles_per_row, emu->ppu, 2, &tile_pal);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shader);
        glBindVertexArray(va);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, block_textures[block_to_display]);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, (void*)0);

        glfwSwapBuffers(window);
    }
}
