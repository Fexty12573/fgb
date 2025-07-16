#include "render.h"

#include <stdio.h>
#include <stdlib.h>
#include <threads.h>

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

struct {
    fgb_emu* emu;
    thrd_t emu_thread;
    bool running;
    bool display_screen;
    int block_to_display;
    double render_framerate;
    double emu_framerate;
} g_app = { NULL, { 0 }, true, true, 0, 0.0, 0.0 };

static fgb_emu* emu_init(const char* rom_path) {
    FILE* f;
    const errno_t err = fopen_s(&f, rom_path, "rb");
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

    GLFWwindow* window = glfwCreateWindow(SCREEN_WIDTH * 3, SCREEN_HEIGHT * 3, "fgb", NULL, NULL);
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
"    TexCoord = vec2(texCoord.x, 1.0 - texCoord.y);\n"
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

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }

    fgb_emu* emu = g_app.emu;
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
        if (key == GLFW_KEY_B) {
            g_app.block_to_display = (g_app.block_to_display + 1) % TILE_BLOCK_COUNT;
            log_info("Displaying block %d", g_app.block_to_display);
        }
        if (key == GLFW_KEY_V) {
            g_app.display_screen = !g_app.display_screen;
            log_info("Screen display %s", g_app.display_screen ? "enabled" : "disabled");
        }
    }

    // Joypad Input
    if (action == GLFW_REPEAT) {
        return; // Don't care about repeated key presses for joypad input
    }

    if (key >= GLFW_KEY_RIGHT && key <= GLFW_KEY_UP) {
        fgb_emu_set_button(emu, key - GLFW_KEY_RIGHT + BUTTON_RIGHT, !!action);
    }

    if (key == GLFW_KEY_SPACE) {
        fgb_emu_set_button(emu, BUTTON_A, !!action);
    }

    if (key == GLFW_KEY_LEFT_SHIFT) {
        fgb_emu_set_button(emu, BUTTON_B, !!action);
    }

    if (key == GLFW_KEY_ENTER) {
        fgb_emu_set_button(emu, BUTTON_START, !!action);
    }

    if (key == GLFW_KEY_BACKSPACE) {
        fgb_emu_set_button(emu, BUTTON_SELECT, !!action);
    }
}

static void gl_debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
    switch (severity) {
    case GL_DEBUG_SEVERITY_HIGH:
        printf("OpenGL Error: %s (Source: %u, Type: %u, ID: %u)", message, source, type, id);
        break;

    case GL_DEBUG_SEVERITY_MEDIUM:
        printf("OpenGL Warning: %s (Source: %u, Type: %u, ID: %u)", message, source, type, id);
        break;

    case GL_DEBUG_SEVERITY_LOW:
        printf("OpenGL Info: %s (Source: %u, Type: %u, ID: %u)", message, source, type, id);
        break;

    case GL_DEBUG_SEVERITY_NOTIFICATION:
        printf("OpenGL Notification: %s (Source: %u, Type: %u, ID: %u)", message, source, type, id);
        break;
    }
}

static int emu_run(void* arg) {
    const fgb_emu* emu = arg;

    const double frametime = 1.0 / FGB_SCREEN_REFRESH_RATE;
    struct timespec ts = { 0 };
    double error = 0.0;
    
    while (g_app.running) {
        const double start_time = glfwGetTime();

        fgb_cpu_step(emu->cpu);

        const double end_time = glfwGetTime();
        const double elapsed = end_time - start_time;
        const double to_sleep = frametime - elapsed + error;

        if (to_sleep > 0.0) {
            ts.tv_sec = (time_t)to_sleep;
            ts.tv_nsec = (long)((to_sleep - (time_t)to_sleep) * 1e9);
            thrd_sleep(&ts, NULL);
        }

        const double actual_frametime = glfwGetTime() - start_time;
        g_app.emu_framerate = 1.0 / actual_frametime;

        error = frametime - actual_frametime;
    }

    return 0;
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
    
    g_app.emu = emu_init(argv[1]);
    if (!g_app.emu) {
        printf("Could not create emulator. Exiting\n");
        return 1;
    }
    
    ulog_set_quiet(false);
    ulog_set_level(LOG_DEBUG);
    fgb_emu_set_log_level(g_app.emu, LOG_DEBUG);
    g_app.emu->cpu->trace = false;

    glDebugMessageCallback(gl_debug_callback, NULL);

    const int tiles_per_row = 16; // Number of tiles per row in the texture
    uint32_t block_textures[TILE_BLOCK_COUNT] = {
        [0] = fgb_create_tile_block_texture(tiles_per_row),
        [1] = fgb_create_tile_block_texture(tiles_per_row),
        [2] = fgb_create_tile_block_texture(tiles_per_row),
    };

    const uint32_t screen_texture = fgb_create_screen_texture();

    uint32_t va, vb, ib;
    fgb_create_quad(&va, &vb, &ib);

    const fgb_palette bg_pal = {
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

    g_app.emu->ppu->bg_palette = bg_pal;
    g_app.emu->ppu->obj_palette = obj_pal;

    glfwSetKeyCallback(window, key_callback);

    thrd_create(&g_app.emu_thread, emu_run, g_app.emu);

    double last_time = glfwGetTime();
    double last_title_update = last_time;

    while (!glfwWindowShouldClose(window)) {
        const double current_time = glfwGetTime();
        const double delta_time = current_time - last_time;
        last_time = current_time;

        if (current_time - last_title_update >= 1.0) {
            char title[256];
            snprintf(title, sizeof(title), "fgb - FPS: %.2f, Emu FPS: %.2f", g_app.render_framerate, g_app.emu_framerate);
            glfwSetWindowTitle(window, title);
            last_title_update = current_time;
        }

        g_app.render_framerate = 1.0 / delta_time;

        glfwPollEvents();

        if (g_app.display_screen) {
            fgb_upload_screen_texture(screen_texture, g_app.emu->ppu);
        } else {
            fgb_upload_tile_block_texture(block_textures[g_app.block_to_display], tiles_per_row, g_app.emu->ppu, g_app.block_to_display, &bg_pal);
        }

        // Render at monitor refresh rate (e.g., 165Hz)
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shader);
        glBindVertexArray(va);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_app.display_screen ? screen_texture : block_textures[g_app.block_to_display]);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);

        glfwSwapBuffers(window);
    }
}
