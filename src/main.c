#include "render.h"

#include <stdio.h>
#include <stdlib.h>
#include <threads.h>

#include <fgb/emu.h>
#include <fgb/cpu.h>

#include <GL/glew.h>
#include <GL/GL.h>
#include <GLFW/glfw3.h>
#include <cimgui.h>
#include <cimgui_impl.h>
#include <string.h>

#include <ulog.h>

#include "audio.h"
#include <fgb/cart.h>

size_t file_size(FILE* f) {
    fseek(f, 0, SEEK_END);
    const size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    return size;
}

#define DISASM_LINES 20
#define WINDOW_SCALE 4
#define APU_SAMPLE_RATE 48000 // 48 kHz
#define AUDIO_SAMPLE_RATE 48000 // 48 kHz

struct app {
    fgb_emu* emu;
    char* rom_path;
    bool running;
    bool display_screen;
    int block_to_display;
    double render_framerate;
    double emu_framerate;
    double emu_update_time;
    bool reset_keep_breakpoints;

    float main_scale;
    GLFWwindow* window;

    FILE* trace_file;

    uint16_t disasm_addr;
    char disasm_buffer[DISASM_LINES][64];
    char* disasm_buffer_ptrs[DISASM_LINES];
    uint16_t disasm_addrs[DISASM_LINES];

    uint32_t framebuffer_textures[PPU_FRAMEBUFFER_COUNT];
    uint32_t sprite_textures[PPU_OAM_SPRITES];
};

struct app g_app = {
    .emu = NULL,
    .running = true,
    .display_screen = true,
    .block_to_display = 0,
    .render_framerate = 0.0,
    .emu_framerate = 0.0,
    .reset_keep_breakpoints = true,
    .main_scale = 1.0f,
    .window = NULL,
    .disasm_addr = 0x0100,
    .disasm_buffer = { {0} },
    .disasm_buffer_ptrs = { NULL },
};

static bool emu_start(void);
static bool emu_stop(void);
static void emu_configure(void);
static void emu_try_save_ram(void);

static char* last_of(char* str, char c) {
    for (size_t i = strlen(str) - 1; i > 0; i--) {
        if (str[i] == c) {
            return &str[i];
        }
    }

    if (str[0] == c) {
        return str;
    }

    return NULL;
}

static bool endswith(const char* str, const char* suffix) {
    if (!str || !suffix) {
        return false;
    }

    const size_t str_len = strlen(str);
    const size_t suffix_len = strlen(suffix);
    if (suffix_len > str_len) {
        return false;
    }

    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

static char* save_path_from_rom(char* rom_path) {
    const char* ext = last_of(rom_path, '.');
    const size_t len = ext ? ext - rom_path : strlen(rom_path);
    char* save_path = malloc(len + 5); // +5 for ".sav" and null terminator

    strcpy(save_path, rom_path);
    strcpy(save_path + len, ".sav");

    return save_path;
}

static void set_disasm_addr(uint16_t addr) {
    g_app.disasm_addr = addr;
    for (int i = 0; i < DISASM_LINES; i++) {
        g_app.disasm_addrs[i] = addr;
        addr = fgb_cpu_disassemble_one(g_app.emu->cpu, addr, g_app.disasm_buffer_ptrs[i], sizeof(*g_app.disasm_buffer));
    }
}

static void on_breakpoint(fgb_cpu* cpu, size_t bp, uint16_t addr) {
    (void)cpu;
    (void)bp;

    if (addr < g_app.disasm_addrs[0] || addr > g_app.disasm_addrs[DISASM_LINES - 1]) {
        set_disasm_addr(addr);
    }

    (void)fflush(stdout);
    (void)fflush(stderr);
}

static void on_step(fgb_cpu* cpu) {
    // Behavior depends on where we are stepping from/to.
    // If we step just outside of the current disassembly view,
    // we need to update the view, but only so that the current
    // PC is visible.
    const uint16_t pc = cpu->regs.pc;
    const uint16_t last_addr = g_app.disasm_addrs[DISASM_LINES - 1];
    const uint16_t after_last_addr = fgb_cpu_disassemble_one(cpu, last_addr, NULL, 0);

    if (pc == after_last_addr) {
        // Stepped just past the end, shift down
        set_disasm_addr(g_app.disasm_addrs[1]);
    } else if (pc < g_app.disasm_addrs[0] || pc > last_addr) {
        // Stepped outside the current view, reset to PC
        set_disasm_addr(pc);
    }

    (void)fflush(stdout);
    (void)fflush(stderr);
}

static void log_cpu_trace(fgb_cpu* cpu, uint16_t addr, uint32_t depth, const char* disasm) {
    (void)cpu;
    (void)fprintf(g_app.trace_file, "0x%04X:%*s%s\n", addr, 2 * (depth + 1), "", disasm);
}

static fgb_emu* emu_init(const char* rom_path) {
    if (!endswith(rom_path, ".gb")) {
        log_error("Unsupported ROM format: %s (only .gb supported)", rom_path);
        exit(1);
    }

    FILE* f;
    errno_t err = fopen_s(&f, rom_path, "rb");
    if (err) {
        printf("Failed to open file %s\n", rom_path);
        exit(1);
    }

    const size_t size = file_size(f);
    uint8_t* data = malloc(size);
    if (!data) {
        printf("Failed to allocate memory for ROM\n");
        fclose(f);
        exit(1);
    }

    fread(data, 1, size, f);
    fclose(f);

    fgb_emu* emu = fgb_emu_create(data, size, APU_SAMPLE_RATE, fgb_audio_push_samples, fgb_audio_get_driver());
    free(data);

    g_app.rom_path = _strdup(rom_path);

    // Try to load battery-backed RAM
    char* save_path = save_path_from_rom(g_app.rom_path);
    err = fopen_s(&f, save_path, "rb");
    free(save_path);

    if (err) {
        // File does not exist, that's fine
        return emu;
    }

    const size_t save_size = file_size(f);
    uint8_t* save_data = malloc(save_size);
    if (!save_data) {
        printf("Failed to allocate memory for save data\n");
        fclose(f);
        return emu; // Not really a fatal error so just continue
    }

    fread(save_data, 1, save_size, f);
    fclose(f);

    fgb_cart_load_battery_buffered_ram(emu->cart, save_data, save_size);
    free(save_data);

    return emu;
}

static GLFWwindow* window_init(void) {
    if (!glfwInit()) {
        printf("Failed to initialize GLFW\n");
        exit(1);
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);

    g_app.main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());

    GLFWwindow* window = glfwCreateWindow(
        (int)(SCREEN_WIDTH * WINDOW_SCALE * g_app.main_scale),
        (int)(SCREEN_HEIGHT * WINDOW_SCALE * g_app.main_scale),
        "fgb", NULL, NULL
    );

    if (!window) {
        printf("Failed to create window\n");
        glfwTerminate();
        exit(1);
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable VSync

    g_app.window = window;
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

static void imgui_init(void) {
    igCreateContext(NULL);

    ImGuiIO* io = igGetIO_Nil();
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGuiStyle* style = igGetStyle();
    ImGuiStyle_ScaleAllSizes(style, g_app.main_scale);
    style->FontScaleDpi = g_app.main_scale;

    io->ConfigDpiScaleFonts = true;
    io->ConfigDpiScaleViewports = true;

    ImGui_ImplGlfw_InitForOpenGL(g_app.window, true);
    ImGui_ImplOpenGL3_Init("#version 450 core");

    igStyleColorsDark(style);
}

static void render_tilesets(int tiles_per_row, const uint32_t* block_textures) {
    igBegin("Tilesets", NULL, ImGuiWindowFlags_AlwaysAutoResize);
    for (int i = 0; i < TILE_BLOCK_COUNT; i++) {
        const int tile_rows = TILES_PER_BLOCK / tiles_per_row;
        igImage(
            (ImTextureRef) { NULL, block_textures[i] },
            (ImVec2) { 5 * tiles_per_row * TILE_WIDTH, 5 * tile_rows * TILE_HEIGHT },
            (ImVec2) { 0, 0 },
            (ImVec2) { 1, 1 }
        );
    }
    igEnd();
}

static void render_line_sprites(void) {
    const fgb_emu* emu = g_app.emu;
    const fgb_ppu* ppu = emu->ppu;

    igBegin("Line Sprites", NULL, ImGuiWindowFlags_None);

    if (igBeginTable("##line-sprites", 10, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingFixedFit, (ImVec2) { 0, 0 }, 0.0f)) {
        for (int i = 0; i < PPU_OAM_SPRITES; i++) {
            const fgb_sprite* sprite = (const fgb_sprite*) &ppu->oam[i * sizeof(fgb_sprite)];

            igTableNextColumn();
            igImage(
                (ImTextureRef) { NULL, g_app.sprite_textures[i] },
                (ImVec2) { 5 * PPU_SPRITE_W, 5 * PPU_SPRITE_H16 },
                (ImVec2) { 0, 0 },
                (ImVec2) { 1, 1 }
            );

            if (igBeginItemTooltip()) {
                igText("Sprite %d", i);
                igText("X: %d", sprite->x);
                igText("Y: %d", sprite->y);
                igText("Tile: %02X", sprite->tile);
                igText("Flags: %02X", sprite->flags);
                igText("  Palette: %d", sprite->palette);
                igText("  X Flip: %d", sprite->x_flip);
                igText("  Y Flip: %d", sprite->y_flip);
                igText("  Priority: %d", sprite->priority);
                igEndTooltip();
            }
        }

        igEndTable();
    }

    igEnd();
}

static void render_cpu_options(void) {
    igBegin("CPU", NULL, ImGuiWindowFlags_None);

    if (igButton("Reset", (ImVec2) { 0, 0 })) {
        uint16_t bps[FGB_CPU_MAX_BREAKPOINTS];
        memcpy(bps, g_app.emu->cpu->breakpoints, sizeof(bps));

        fgb_emu_reset(g_app.emu);

        if (g_app.reset_keep_breakpoints) {
            memcpy(g_app.emu->cpu->breakpoints, bps, sizeof(bps));
        }
    }

    igSameLine(0.0f, -1.0f);
    if (igButton("Reset Paused", (ImVec2) { 0, 0 })) {
        uint16_t bps[FGB_CPU_MAX_BREAKPOINTS];
        memcpy(bps, g_app.emu->cpu->breakpoints, sizeof(bps));

        fgb_emu_reset(g_app.emu);

        if (g_app.reset_keep_breakpoints) {
            memcpy(g_app.emu->cpu->breakpoints, bps, sizeof(bps));
        }

        g_app.emu->cpu->debugging = true;
        set_disasm_addr(g_app.emu->cpu->regs.pc);
    }

    igSameLine(0.0f, -1.0f);
    igCheckbox("Keep Breakpoints on Reset", &g_app.reset_keep_breakpoints);

    igCheckbox("Test Mode", &g_app.emu->cpu->test_mode);

    igInputInt("Trace", &g_app.emu->cpu->trace_count, 1, 100, ImGuiInputTextFlags_None);

    if (igCheckbox("Hide Background", &g_app.emu->ppu->debug.hide_bg)) {
        log_info("Background rendering %s", g_app.emu->ppu->debug.hide_bg ? "disabled" : "enabled");
    }

    if (igCheckbox("Hide Sprites", &g_app.emu->ppu->debug.hide_sprites)) {
        log_info("Sprite rendering %s", g_app.emu->ppu->debug.hide_sprites ? "disabled" : "enabled");
    }

    if (igCheckbox("Hide Window", &g_app.emu->ppu->debug.hide_window)) {
        log_info("Window rendering %s", g_app.emu->ppu->debug.hide_window ? "disabled" : "enabled");
    }

    if (igCheckbox("Display Screen", &g_app.display_screen)) {
        log_info("Screen display %s", g_app.display_screen ? "enabled" : "disabled");
    }

    if (igButton("Reset CPU", (ImVec2) { 0, 0 })) {
        fgb_cpu_reset(g_app.emu->cpu);
        log_info("CPU reset");
    }

    if (igButton("Dump CPU State", (ImVec2) { 0, 0 })) {
        fgb_cpu_dump_state(g_app.emu->cpu);
    }

    igBeginChild_Str("Disassembly", (ImVec2) { 0, 0 }, ImGuiChildFlags_Borders, ImGuiWindowFlags_None);

    if (igInputScalar("Address", ImGuiDataType_U16, &g_app.disasm_addr, NULL, NULL, "%04X", ImGuiInputTextFlags_CharsHexadecimal)) {
        set_disasm_addr(g_app.disasm_addr);
    }

    igSameLine(0.0f, -1.0f);

    if (igButton("Goto PC", (ImVec2) { 0, 0 })) {
        set_disasm_addr(g_app.emu->cpu->regs.pc);
    }

    igBeginDisabled(!g_app.emu->cpu->debugging);

    if (igButton("Step Over", (ImVec2) { 0, 0 })) {
        g_app.emu->cpu->do_step = true;
    }

    igSameLine(0.0f, -1.0f);
    
    if (igButton("Continue", (ImVec2) { 0, 0 })) {
        g_app.emu->cpu->debugging = false;
    }

    igEndDisabled();

    igSameLine(0.0f, -1.0f);

    if (igButton("Pause", (ImVec2) { 0, 0 })) {
        g_app.emu->cpu->debugging = true;
        log_info("Execution stopped at 0x%04X", g_app.emu->cpu->regs.pc);
        set_disasm_addr(g_app.emu->cpu->regs.pc);
    }

    igPushStyleColor_U32(ImGuiCol_CheckMark, 0xFF0000FF);

    if (igBeginTable("Disassembly Table", 4, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingFixedFit, (ImVec2) { 0, 0 }, 0.0f)) {
        for (int i = 0; i < DISASM_LINES; i++) {
            igPushID_Int(i);

            igTableNextColumn();

            const uint16_t addr = g_app.disasm_addrs[i];
            bool selected = fgb_cpu_get_bp_at(g_app.emu->cpu, addr) != -1;
            if (igCheckbox("##breakpoint", &selected)) {
                if (selected) {
                    fgb_cpu_set_bp(g_app.emu->cpu, addr);
                    log_info("Breakpoint set at 0x%04X", addr);
                } else {
                    fgb_cpu_clear_bp(g_app.emu->cpu, addr);
                    log_info("Breakpoint cleared at 0x%04X", addr);
                }
            }

            igTableNextColumn();

            if (g_app.emu->cpu->regs.pc == addr) {
                igTextUnformatted("->", NULL);
            } else {
                igTextUnformatted("  ", NULL);
            }

            igTableNextColumn();

            igText("0x%04X", addr);

            igTableNextColumn();

            igTextUnformatted(g_app.disasm_buffer_ptrs[i], NULL);

            igPopID();
        }
        igEndTable();
    }

    igPopStyleColor(1);

    fgb_cpu* volatile cpu = g_app.emu->cpu;

    igPushID_Str("CPU_UI");

    if (igBeginTable("cpu_table", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV, (ImVec2) { 0, 0 }, 0.0f)) {
        // Intentional copy to allow editing without risk of Race Conditions
        const fgb_cpu_regs before_regs = cpu->regs;
        fgb_cpu_regs regs = before_regs;

        // Left column: registers
        igTableNextRow(0, 0);
        igTableSetColumnIndex(0);

        igSeparatorText("Registers");
        igPushID_Str("Regs");
        igPushItemWidth(48.0f); // narrow inputs

        if (igBeginTable("regs_left", 4, ImGuiTableFlags_SizingFixedSame, (ImVec2) { 0, 0 }, 0.0f)) {
            // Row: A / F   | inputs | AF
            igTableNextRow(0, 0);
            igTableSetColumnIndex(0);
            igTextUnformatted("A / F", NULL);

            igTableSetColumnIndex(1);
            igInputScalar("##A", ImGuiDataType_U8, &regs.a, NULL, NULL, "%02X", ImGuiInputTextFlags_CharsHexadecimal);

            igTableSetColumnIndex(2);
            igInputScalar("##F", ImGuiDataType_U8, &regs.f, NULL, NULL, "%02X", ImGuiInputTextFlags_CharsHexadecimal);

            igTableSetColumnIndex(3);
            igText("AF: %04X", cpu->regs.af);

            // Row: B / C   | inputs | BC
            igTableNextRow(0, 0);
            igTableSetColumnIndex(0);
            igTextUnformatted("B / C", NULL);

            igTableSetColumnIndex(1);
            igInputScalar("##B", ImGuiDataType_U8, &regs.b, NULL, NULL, "%02X", ImGuiInputTextFlags_CharsHexadecimal);

            igTableSetColumnIndex(2);
            igInputScalar("##C", ImGuiDataType_U8, &regs.c, NULL, NULL, "%02X", ImGuiInputTextFlags_CharsHexadecimal);

            igTableSetColumnIndex(3);
            igText("BC: %04X", cpu->regs.bc);

            // Row: D / E   | inputs | DE
            igTableNextRow(0, 0);
            igTableSetColumnIndex(0);
            igTextUnformatted("D / E", NULL);

            igTableSetColumnIndex(1);
            igInputScalar("##D", ImGuiDataType_U8, &regs.d, NULL, NULL, "%02X", ImGuiInputTextFlags_CharsHexadecimal);

            igTableSetColumnIndex(2);
            igInputScalar("##E", ImGuiDataType_U8, &regs.e, NULL, NULL, "%02X", ImGuiInputTextFlags_CharsHexadecimal);

            igTableSetColumnIndex(3);
            igText("DE: %04X", cpu->regs.de);

            // Row: H / L   | inputs | HL
            igTableNextRow(0, 0);
            igTableSetColumnIndex(0);
            igTextUnformatted("H / L", NULL);

            igTableSetColumnIndex(1);
            igInputScalar("##H", ImGuiDataType_U8, &regs.h, NULL, NULL, "%02X", ImGuiInputTextFlags_CharsHexadecimal);

            igTableSetColumnIndex(2);
            igInputScalar("##L", ImGuiDataType_U8, &regs.l, NULL, NULL, "%02X", ImGuiInputTextFlags_CharsHexadecimal);

            igTableSetColumnIndex(3);
            igText("HL: %04X", cpu->regs.hl);

            igTableNextRow(0, 0);
            igTableSetColumnIndex(0);
            igTextUnformatted("SP", NULL);

            igTableSetColumnIndex(1);
            igInputScalar("##SP", ImGuiDataType_U16, &regs.sp, NULL, NULL, "%04X", ImGuiInputTextFlags_CharsHexadecimal);

            igTableNextRow(0, 0);
            igTableSetColumnIndex(0);
            igTextUnformatted("PC", NULL);

            igTableSetColumnIndex(1);
            igInputScalar("##PC", ImGuiDataType_U16, &regs.pc, NULL, NULL, "%04X", ImGuiInputTextFlags_CharsHexadecimal);

            igEndTable();
        }

        igPopItemWidth();
        igPopID(); // Regs

        if (memcmp(&before_regs, &regs, sizeof(fgb_cpu_regs)) != 0) {
            // Registers were modified, apply changes
            cpu->regs = regs;
        }

        // Right column: flags + misc
        igTableSetColumnIndex(1);

        igSeparatorText("Flags");
        igPushID_Str("Flags");

        bool c = fgb_cpu_get_flag(cpu, CPU_FLAG_C);
        bool h = fgb_cpu_get_flag(cpu, CPU_FLAG_H);
        bool n = fgb_cpu_get_flag(cpu, CPU_FLAG_N);
        bool z = fgb_cpu_get_flag(cpu, CPU_FLAG_Z);

        // Put flags in a compact 4-column table
        if (igBeginTable("flags_tbl", 4, ImGuiTableFlags_SizingFixedFit, (ImVec2) { 0, 0 }, 0.0f)) {
            igTableNextRow(0, 0);
            igTableSetColumnIndex(0); if (igCheckbox("C", &c)) { fgb_cpu_set_flag(cpu, CPU_FLAG_C, c); }
            igTableSetColumnIndex(1); if (igCheckbox("H", &h)) { fgb_cpu_set_flag(cpu, CPU_FLAG_H, h); }
            igTableSetColumnIndex(2); if (igCheckbox("N", &n)) { fgb_cpu_set_flag(cpu, CPU_FLAG_N, n); }
            igTableSetColumnIndex(3); if (igCheckbox("Z", &z)) { fgb_cpu_set_flag(cpu, CPU_FLAG_Z, z); }
            igEndTable();
        }

        igPopID(); // Flags

        igSeparatorText("Misc");

        bool ime = cpu->ime;
        if (igCheckbox("IME", &ime)) {
            cpu->ime = ime;
        }

        igText("Mode: %d", cpu->mode);
        igText("IE: %02X", cpu->interrupt.enable);
        igText("IF: %02X", cpu->interrupt.flags);
        igText("T-Cycles: %llu", cpu->total_cycles);
        igText("M-Cycles: %llu", cpu->total_cycles / 4);

        // Timer
        fgb_timer timer = cpu->timer;
        bool modified = false;

        igTableNextRow(0, 0);
        igTableSetColumnIndex(0);

        if (igBeginTable("timer_tbl", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit, (ImVec2) { 0.0f, 0 }, 0.0f)) {
            igTableSetupColumn("DIV", ImGuiTableColumnFlags_WidthFixed, 64.0f, 0);
            igTableSetupColumn("DIV Reg", ImGuiTableColumnFlags_WidthFixed, 64.0f, 0);
            igTableSetupColumn("TIMA", ImGuiTableColumnFlags_WidthFixed, 64.0f, 0);
            igTableSetupColumn("TMA", ImGuiTableColumnFlags_WidthFixed, 64.0f, 0);
            igTableSetupColumn("TAC", ImGuiTableColumnFlags_WidthFixed, 64.0f, 0);
            igTableSetupColumn("Reload", ImGuiTableColumnFlags_WidthFixed, 64.0f, 0);
            igTableHeadersRow();
            igTableNextRow(0, 0);

            uint8_t divreg = (timer.divider >> 8) & 0xFF;

            igTableNextColumn();
            modified |= igInputScalar("##div", ImGuiDataType_U16, &timer.divider, NULL, NULL, "%d", ImGuiInputTextFlags_None);
            igTableNextColumn();
            modified |= igInputScalar("##divreg", ImGuiDataType_U8, &divreg, NULL, NULL, "%d", ImGuiInputTextFlags_None);
            igTableNextColumn();
            modified |= igInputScalar("##tima", ImGuiDataType_U8, &timer.counter, NULL, NULL, "%d", ImGuiInputTextFlags_None);
            igTableNextColumn();
            modified |= igInputScalar("##tma", ImGuiDataType_U8, &timer.modulo, NULL, NULL, "%d", ImGuiInputTextFlags_None);
            igTableNextColumn();
            modified |= igInputScalar("##tac", ImGuiDataType_U8, &timer.control, NULL, NULL, "%02X", ImGuiInputTextFlags_CharsHexadecimal);
            igTableNextColumn();
            modified |= igInputScalar("##reload", ImGuiDataType_U8, &timer.ticks_since_overflow, NULL, NULL, "%d", ImGuiInputTextFlags_None);

            timer.divider = (uint16_t)((uint16_t)divreg << 8) | (timer.divider & 0x00FF);
            
            igEndTable();
        }

        if (modified) {
            // Timer was modified, apply changes
            cpu->timer = timer;
        }

        igEndTable();
    }

    igPopID(); // CPU_UI

    igEndChild();

    if (igIsItemHovered(ImGuiHoveredFlags_None)) {
        // Scroll address with mouse wheel
        float wheel = igGetIO_Nil()->MouseWheel;
        if (wheel != 0.0f) {
            set_disasm_addr(g_app.disasm_addr + (int)(-wheel * 4));
        }
    }

    igEnd();
}

static void render_ppu_options(void) {
    igBegin("PPU", NULL, ImGuiWindowFlags_None);

    fgb_ppu* ppu = g_app.emu->ppu;

    if (igBeginTable("ppu_table", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV, (ImVec2) { 0, 0 }, 0.0f)) {
        igTableNextRow(0, 0);
        igTableSetColumnIndex(0);
        igText("Mode: %d", ppu->stat.mode);

        igTableSetColumnIndex(1);
        ImVec4 color;
        igColorConvertU32ToFloat4(&color, ppu->debug.window_color);
        if (igColorEdit4("Window Color", &color.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
            ppu->debug.window_color = igColorConvertFloat4ToU32(color);
        }

        igTableNextRow(0, 0);
        igTableSetColumnIndex(0);
        igText("Scanline: %d", ppu->ly);

        igTableSetColumnIndex(1);
        igTextUnformatted("-----", NULL);

        igTableNextRow(0, 0);
        igTableSetColumnIndex(0);
        igText("Cycle: %d", ppu->mode_cycles);

        igTableSetColumnIndex(1);
        igTextUnformatted("-----", NULL);

        igTableNextRow(0, 0);
        igTableSetColumnIndex(0);
        igText("App Framerate: %.2f FPS", g_app.render_framerate);
        igText("Emu Framerate: %.2f FPS", g_app.emu_framerate);
        igText("Emu Frametime: %.2fus", g_app.emu_update_time);

        igTableNextColumn();
        igTableNextColumn();

        struct { uint8_t x, y; } window_pos = {
            .x = ppu->window_pos.x,
            .y = ppu->window_pos.y,
        };

        igSetNextItemWidth(200.0f);
        if (igInputScalarN("Window Pos", ImGuiDataType_U8, &window_pos.x, 2, NULL, NULL, "%d", ImGuiInputTextFlags_None)) {
            ppu->window_pos.x = window_pos.x;
            ppu->window_pos.y = window_pos.y;
        }

        igEndTable();

        if (igCollapsingHeader_BoolPtr("Front Buffer", NULL, ImGuiTreeNodeFlags_None)) {
            igImage((ImTextureRef){NULL, g_app.framebuffer_textures[0]},
                (ImVec2){SCREEN_WIDTH * 2.0f, SCREEN_HEIGHT * 2.0f},
                (ImVec2){0, 0},
                (ImVec2){1, 1}
            );
        }

        if (igCollapsingHeader_BoolPtr("Back Buffer", NULL, ImGuiTreeNodeFlags_None)) {
            igImage((ImTextureRef){NULL, g_app.framebuffer_textures[1]},
                (ImVec2){SCREEN_WIDTH * 2.0f, SCREEN_HEIGHT * 2.0f},
                (ImVec2){0, 0},
                (ImVec2){1, 1}
            );
        }

        if (igCollapsingHeader_BoolPtr("Tile Map 0 ($9800)", NULL, ImGuiTreeNodeFlags_None)) {
            for (int y = 0; y < 32; y++) {
                for (int x = 0; x < 32; x++) {
                    const uint8_t tile_index = ppu->vram[0x1800 + y * 32 + x];
                    float r, g, b;
                    igColorConvertHSVtoRGB((float)tile_index / 255.0f, 1.0f, 1.0f, &r, &g, &b);
                    igTextColored((ImVec4) { r, g, b, 1 }, "%02X", tile_index);
                    if (igBeginItemTooltip()) {
                        igText("(%d, %d)", x, y);
                        igEndTooltip();
                    }
                    igSameLine(0, -1);
                }

                igNewLine();
            }
        }

        if (igCollapsingHeader_BoolPtr("Tile Map 1 ($9C00)", NULL, ImGuiTreeNodeFlags_None)) {
            for (int y = 0; y < 32; y++) {
                for (int x = 0; x < 32; x++) {
                    const uint8_t tile_index = ppu->vram[0x1C00 + y * 32 + x];
                    float r, g, b;
                    igColorConvertHSVtoRGB((float)tile_index / 255.0f, 1.0f, 1.0f, &r, &g, &b);
                    igTextColored((ImVec4) { r, g, b, 1 }, "%02X", tile_index);
                    if (igBeginItemTooltip()) {
                        igText("(%d, %d)", x, y);
                        igEndTooltip();
                    }
                    igSameLine(0, -1);
                }

                igNewLine();
            }
        }
    }

    igEnd();
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }

    fgb_emu* emu = g_app.emu;
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_T) {
            emu->cpu->trace_count = emu->cpu->trace_count == 0 ? -1 : 0;
            log_info("CPU trace %s", emu->cpu->trace_count > 0 ? "enabled" : "disabled");
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

static void drop_callback(GLFWwindow* window, int count, const char** paths) {
    (void)window;
    if (count != 1) {
        log_warn("Please drop a single ROM file");
        return;
    }

    emu_stop();
    fgb_emu_destroy(g_app.emu);

    log_info("Loading ROM: %s", paths[0]);
    g_app.emu = emu_init(paths[0]);

    emu_configure();
    emu_start();
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

static void setup_dockspace(void) {
    // Get the main viewport
    const ImGuiViewport* viewport = igGetMainViewport();

    // Set up a fullscreen, non-movable, non-resizable, non-collapsible window for the dockspace
    const ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration;

    igSetNextWindowPos(viewport->Pos, 0, (ImVec2) { 0, 0 });
    igSetNextWindowSize(viewport->Size, 0);
    igSetNextWindowViewport(viewport->ID);

    igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, 0.0f);
    igPushStyleVar_Float(ImGuiStyleVar_WindowBorderSize, 0.0f);
    igBegin("DockSpaceWindow", NULL, window_flags);
    igPopStyleVar(2);

    // Create the dockspace
    const ImGuiID dockspace_id = igGetID_Str("MyDockspace");
    igDockSpace(dockspace_id, (ImVec2) { 0, 0 }, ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoUndocking, NULL);
    igEnd();
}

bool emu_start(void) {
    return true;
}

bool emu_stop(void) {
    g_app.emu->cpu->mode = CPU_MODE_NORMAL; // Ensure CPU is not in halt or stop mode
    return true;
}

void emu_configure(void) {
    set_disasm_addr(0x100);
    fgb_cpu_set_bp_callback(g_app.emu->cpu, on_breakpoint);
    fgb_cpu_set_step_callback(g_app.emu->cpu, on_step);
    fgb_cpu_set_trace_callback(g_app.emu->cpu, log_cpu_trace);

    ulog_set_quiet(false);
    ulog_set_level(LOG_DEBUG);
    fgb_emu_set_log_level(g_app.emu, LOG_DEBUG);
    g_app.emu->cpu->trace_count = 0;

    fgb_ppu_set_color_mode(g_app.emu->ppu, PPU_COLOR_MODE_TINTED);
}

void emu_try_save_ram(void) {
    const uint8_t* ram = fgb_cart_get_battery_buffered_ram(g_app.emu->cart);
    if (ram) {
        char* save_path = save_path_from_rom(g_app.rom_path);
        FILE* file = fopen(save_path, "wb");
        if (!file) {
            log_error("Could not open save file for writing: %s", save_path);
            return;
        }

        free(save_path);

        fwrite(ram, 1, fgb_cart_get_ram_size(g_app.emu->cart), file);
        fclose(file);
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

    imgui_init();

    g_app.trace_file = fopen("cpu_trace.log", "w");
    if (!g_app.trace_file) {
        printf("Could not open trace log file. Exiting\n");
        return 1;
    }

    if (!fgb_audio_init(AUDIO_SAMPLE_RATE, APU_SAMPLE_RATE)) {
        return 1;
    }
    
    g_app.emu = emu_init(argv[1]);
    if (!g_app.emu) {
        printf("Could not create emulator. Exiting\n");
        return 1;
    }

    for (int i = 0; i < DISASM_LINES; i++) {
        g_app.disasm_buffer_ptrs[i] = g_app.disasm_buffer[i];
    }

    emu_configure();

    glDebugMessageCallback(gl_debug_callback, NULL);

    const int tiles_per_row = 16; // Number of tiles per row in the texture
    const uint32_t block_textures[TILE_BLOCK_COUNT] = {
        [0] = fgb_create_tile_block_texture(tiles_per_row),
        [1] = fgb_create_tile_block_texture(tiles_per_row),
        [2] = fgb_create_tile_block_texture(tiles_per_row),
    };

    fgb_create_oam_textures(g_app.sprite_textures, PPU_OAM_SPRITES);

    const uint32_t screen_texture = fgb_create_screen_texture();

    uint32_t va, vb, ib;
    fgb_create_quad(&va, &vb, &ib);

    g_app.framebuffer_textures[0] = screen_texture; // Front buffer
    g_app.framebuffer_textures[1] = fgb_create_screen_texture(); // Back buffer

    glfwSetKeyCallback(window, key_callback);
    glfwSetDropCallback(window, drop_callback);

    if (!emu_start()) {
        return 1;
    }

    const double emu_frametime = 1.0 / FGB_SCREEN_REFRESH_RATE;

    double last_time = glfwGetTime();
    double last_title_update = last_time;
    double accumulator = 0.0;

    while (!glfwWindowShouldClose(window)) {
        const double current_time = glfwGetTime();
        const double delta_time = current_time - last_time;
        last_time = current_time;
        accumulator += delta_time;

        while (accumulator >= emu_frametime) {
            accumulator -= emu_frametime;
            fgb_cpu_run_frame(g_app.emu->cpu);
        }

        if (current_time - last_title_update >= 1.0) {
            char title[256];
            snprintf(
                title,
                sizeof(title),
                "fgb - FPS: %.2f, Emu FPS: %.2f, Emu Upd: %.02fus",
                g_app.render_framerate,
                g_app.emu_framerate,
                g_app.emu_update_time * 1e6
            );
            glfwSetWindowTitle(window, title);
            last_title_update = current_time;
        }

        g_app.render_framerate = 1.0 / delta_time;

        const fgb_palette* bg_pal = &g_app.emu->ppu->bg_palette;
        const fgb_palette* obj_pal = &g_app.emu->ppu->obj_palette;

        glfwPollEvents();

        fgb_upload_screen_texture(screen_texture, g_app.emu->ppu);
        fgb_upload_back_buffer_texture(g_app.framebuffer_textures[1], g_app.emu->ppu);
        fgb_upload_tile_block_texture(block_textures[0], tiles_per_row, g_app.emu->ppu, 0, bg_pal);
        fgb_upload_tile_block_texture(block_textures[1], tiles_per_row, g_app.emu->ppu, 1, bg_pal);
        fgb_upload_tile_block_texture(block_textures[2], tiles_per_row, g_app.emu->ppu, 2, obj_pal);
        fgb_upload_oam_textures(g_app.sprite_textures, PPU_OAM_SPRITES, g_app.emu->ppu);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        igNewFrame();

        setup_dockspace();

        igBegin("Viewport", NULL, ImGuiWindowFlags_NoDecoration);

        ImVec2 content;
        igGetContentRegionAvail(&content);

        ImVec2 image_size;
        if (content.x < content.y) {
            image_size.x = content.x;
            image_size.y = content.x / ASPECT_RATIO;
        } else {
            image_size.x = content.y * ASPECT_RATIO;
            image_size.y = content.y;
        }

        igSetCursorPos((ImVec2) {
            .x = (content.x - image_size.x) / 2.0f,
            .y = (content.y - image_size.y) / 2.0f
        });

        ImVec2 screen_pos;
        igGetCursorScreenPos(&screen_pos);
        
        igImage(
            (ImTextureRef){ NULL, screen_texture },
            image_size,
            (ImVec2){ 0, 0 },
            (ImVec2){ 1, 1 }
        );

        if (igBeginItemTooltip()) {
            ImVec2 mouse_pos;
            igGetMousePos(&mouse_pos);

            const float u = (mouse_pos.x - screen_pos.x) / image_size.x;
            const float v = (mouse_pos.y - screen_pos.y) / image_size.y;

            const int px = (int)(u * SCREEN_WIDTH);
            const int py = (int)(v * SCREEN_HEIGHT);

            igText("Pixel: (%d, %d)", px, py);

            igEndTooltip();
        }

        igEnd();

        render_tilesets(tiles_per_row, block_textures);
        render_line_sprites();
        render_cpu_options();
        render_ppu_options();

        igRender();

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(igGetDrawData());

        GLFWwindow* backup_current_context = glfwGetCurrentContext();
        igUpdatePlatformWindows();
        igRenderPlatformWindowsDefault(NULL, NULL);
        glfwMakeContextCurrent(backup_current_context);

        glfwSwapBuffers(window);
    }

    g_app.running = false;

    emu_try_save_ram();
    fgb_emu_destroy(g_app.emu);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();

    fclose(g_app.trace_file);
    free(g_app.rom_path);

    igDestroyContext(NULL);

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
