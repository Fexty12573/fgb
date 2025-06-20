#include <string.h>

#include <tester.h>
#include <fgb/cpu.h>

static fgb_cpu* cpu;
static int mem_access_count;
static struct mem_access mem_accesses[16];

static void mock_cpu_init(size_t tester_ins_mem_size, uint8_t* tester_ins_mem);
static void mock_cpu_set_state(struct state* state);
static void mock_cpu_get_state(struct state* state);
static int mock_cpu_step(void);
static uint8_t mock_mmu_read(const fgb_mmu* mmu, uint16_t addr);
static void mock_mmu_write(fgb_mmu* mmu, uint16_t addr, uint8_t value);
static uint16_t mock_mmu_read16(const fgb_mmu* mmu, uint16_t addr);
static void mock_mmu_reset(fgb_mmu* mmu);

struct tester_operations mock_cpu_ops = {
    .init = mock_cpu_init,
    .set_state = mock_cpu_set_state,
    .get_state = mock_cpu_get_state,
    .step = mock_cpu_step,
};

static void mock_cpu_init(size_t tester_ins_mem_size, uint8_t* tester_ins_mem) {
    const fgb_mmu_ops ops = {
        .reset = mock_mmu_reset,
        .write_u8 = mock_mmu_write,
        .read_u8 = mock_mmu_read,
        .read_u16 = mock_mmu_read16,
        .data = tester_ins_mem,
        .data_size = tester_ins_mem_size
    };

    cpu = fgb_cpu_create_with(NULL, &ops);
    mem_access_count = 0;
}

void mock_cpu_set_state(struct state* state) {
    cpu->regs.a = state->reg8.A;
    cpu->regs.f = state->reg8.F;
    cpu->regs.b = state->reg8.B;
    cpu->regs.c = state->reg8.C;
    cpu->regs.d = state->reg8.D;
    cpu->regs.e = state->reg8.E;
    cpu->regs.h = state->reg8.H;
    cpu->regs.l = state->reg8.L;

    cpu->regs.sp = state->SP;
    cpu->regs.pc = state->PC;
    cpu->halted = state->halted;
    cpu->ime = state->interrupts_master_enabled;

    mem_access_count = state->num_mem_accesses;
}

void mock_cpu_get_state(struct state* state) {
    state->reg8.A = cpu->regs.a;
    state->reg8.F = cpu->regs.f;
    state->reg8.B = cpu->regs.b;
    state->reg8.C = cpu->regs.c;
    state->reg8.D = cpu->regs.d;
    state->reg8.E = cpu->regs.e;
    state->reg8.H = cpu->regs.h;
    state->reg8.L = cpu->regs.l;

    state->SP = cpu->regs.sp;
    state->PC = cpu->regs.pc;
    state->halted = cpu->halted;
    state->interrupts_master_enabled = cpu->ime;

    state->num_mem_accesses = mem_access_count;
    memcpy(state->mem_accesses, mem_accesses, sizeof(struct mem_access) * mem_access_count);
}

int mock_cpu_step(void) {
    return fgb_cpu_execute(cpu);
}

uint8_t mock_mmu_read(const fgb_mmu* mmu, uint16_t addr) {
    if (addr < mmu->ext_data_size) {
        return mmu->ext_data[addr];
    }

    return 0xAA;
}

void mock_mmu_write(fgb_mmu* mmu, uint16_t addr, uint8_t value) {
    (void)mmu;
    struct mem_access* access = &mem_accesses[mem_access_count++];
    access->type = MEM_ACCESS_WRITE;
    access->addr = addr;
    access->val = value;
}

uint16_t mock_mmu_read16(const fgb_mmu* mmu, uint16_t addr) {
    const uint16_t lower = mock_mmu_read(mmu, addr);
    const uint16_t upper = mock_mmu_read(mmu, addr + 1);
    return (upper << 8) | lower;
}

void mock_mmu_reset(fgb_mmu* mmu) {
    (void)mmu; // Don't do anything
}
