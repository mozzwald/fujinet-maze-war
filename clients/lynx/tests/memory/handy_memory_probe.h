// Test-only host observer for a private Handy build. Never linked into Lynx ROM.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "maze_memory_config.h"

struct MazeMemoryProbe {
    bool active = false;
    unsigned irq_depth = 0;
    unsigned instructions = 0, raw_min = MAZE_STACK_TOP;
    unsigned access_min = MAZE_STACK_TOP, hardware_used = 0;
    unsigned raw_pc = 0, access_pc = 0, hardware_pc = 0;
    unsigned software_written = 0, hardware_written = 0, scans = 0;
    unsigned char immutable[MAZE_CODE_END - MAZE_CODE_START];
    bool visited[MAZE_CHECKPOINT_COUNT] = {};
};

inline MazeMemoryProbe& maze_memory_probe() {
    static MazeMemoryProbe probe;
    return probe;
}
inline unsigned char maze_canary(unsigned address) {
    return (address ^ (address >> 8) ^ 0xa5) & 255;
}
inline void maze_memory_report(const char *event, unsigned pc) {
    const auto &p = maze_memory_probe();
    std::fprintf(stderr, "MAZE_MEMORY {\"event\":\"%s\",\"pc\":%u,"
        "\"instructions\":%u,\"raw_min\":%u,\"raw_pc\":%u,"
        "\"access_min\":%u,\"access_pc\":%u,\"hardware_used\":%u,"
        "\"hardware_pc\":%u,\"software_written\":%u,"
        "\"hardware_written\":%u,\"scans\":%u}\n",
        event, pc, p.instructions, p.raw_min, p.raw_pc, p.access_min,
        p.access_pc, p.hardware_used, p.hardware_pc, p.software_written,
        p.hardware_written, p.scans);
    std::fflush(stderr);
}
inline void maze_memory_fault(const char *event, unsigned address) {
    maze_memory_report(event, address);
    std::exit(86);
}
inline void maze_memory_scan(const unsigned char *ram, unsigned pc) {
    auto &p = maze_memory_probe();
    ++p.scans;
    for (unsigned a = MAZE_BSS_END; a < MAZE_STACK_BOTTOM; ++a)
        if (ram[a] != maze_canary(a)) maze_memory_fault("padding_corruption", a);
    for (unsigned a = MAZE_APPZP_END; a < 256; ++a)
        if (ram[a] != maze_canary(a)) maze_memory_fault("zero_page_corruption", a);
    if (std::memcmp(p.immutable, ram + MAZE_CODE_START, sizeof(p.immutable)))
        maze_memory_fault("immutable_corruption", pc);
    for (unsigned a = MAZE_STACK_BOTTOM; a < MAZE_STACK_TOP; ++a)
        if (ram[a] != maze_canary(a)) {
            unsigned used = MAZE_STACK_TOP - a;
            if (used > p.software_written) p.software_written = used;
            break;
        }
    for (unsigned a = 0x100; a < 0x200; ++a)
        if (ram[a] != maze_canary(a)) {
            unsigned used = 0x200 - a;
            if (used > p.hardware_written) p.hardware_written = used;
            break;
        }
}
// Called before CPU writes; Suzy bypasses this, so periodic scans also inspect
// guard regions and immutable memory independently of the CPU write path.
inline void maze_memory_write(unsigned address) {
    if (!maze_memory_probe().active) return;
    if (address >= MAZE_BSS_END && address < MAZE_STACK_BOTTOM)
        maze_memory_fault("padding_write", address);
    if (address >= MAZE_APPZP_END && address < 256)
        maze_memory_fault("zero_page_write", address);
    if (address >= MAZE_CODE_START && address < MAZE_CODE_END)
        maze_memory_fault("immutable_write", address);
}
inline void maze_memory_irq_enter() { ++maze_memory_probe().irq_depth; }
inline void maze_memory_irq_exit() {
    auto &p = maze_memory_probe();
    if (p.irq_depth) --p.irq_depth;
}
// PUSH is observed individually, including IRQ entry inside a CPU Update.
inline void maze_memory_push(unsigned sp, unsigned pc) {
    auto &p = maze_memory_probe();
    if (!p.active) return;
    unsigned used = 256 - sp;
    if (used > p.hardware_used) {
        p.hardware_used = used; p.hardware_pc = pc;
        maze_memory_report("hardware_peak", pc);
    }
    if (used == 256) maze_memory_fault("hardware_stack_exhausted", pc);
}
inline void maze_memory_instruction(unsigned char *ram, unsigned pc, unsigned hwsp) {
    auto &p = maze_memory_probe();
    unsigned sp = ram[MAZE_C_SP] | (ram[MAZE_C_SP + 1] << 8);
    if (!p.active) {
        // Arm immediately after crt0 initializes c_sp, before constructors.
        if (pc != MAZE_ARM_PC) return;
        if (sp != MAZE_STACK_TOP || hwsp != 255)
            maze_memory_fault("unexpected_initial_stack", pc);
        unsigned hash = 2166136261u;
        for (unsigned a = MAZE_CODE_START; a < MAZE_CODE_END; ++a)
            hash = (hash ^ ram[a]) * 16777619u;
        if (hash != MAZE_EXPECTED_HASH && !std::getenv("MAZE_MEMORY_ALLOW_PATCHED_ROM"))
            maze_memory_fault("rom_map_mismatch", pc);
        p.active = true;
        std::memcpy(p.immutable, ram + MAZE_CODE_START, sizeof(p.immutable));
        for (unsigned a = MAZE_BSS_END; a < MAZE_STACK_TOP; ++a) ram[a] = maze_canary(a);
        for (unsigned a = MAZE_APPZP_END; a < 256; ++a) ram[a] = maze_canary(a);
        for (unsigned a = 0x100; a < 0x200; ++a) ram[a] = maze_canary(a);
        maze_memory_report("armed", pc);
    }
    ++p.instructions;
    if (sp < p.raw_min) {
        p.raw_min = sp; p.raw_pc = pc;
        maze_memory_report("software_raw_peak", pc);
    }
    // A 16-bit c_sp update can expose a torn word between byte writes. Keep
    // raw_min as a conservative lower bound and also report stable call/
    // return/jump and actual indirect-stack-access boundaries separately.
    // Assembly IRQ handlers may interrupt a half-written c_sp word, but do
    // not use the software stack. Exclude their control flow from access_min.
    unsigned op = ram[pc & 65535];
    bool access = op == 0x20 || op == 0x60 || op == 0x4c || op == 0x6c ||
        ((op == 0xb1 || op == 0x91 || op == 0xb2 || op == 0x92) &&
         ram[(pc + 1) & 65535] == MAZE_C_SP);
    if (access && !p.irq_depth && sp < p.access_min) {
        p.access_min = sp; p.access_pc = pc;
        maze_memory_report("software_access_peak", pc);
    }
    if (access && !p.irq_depth && (sp < MAZE_STACK_BOTTOM || sp > MAZE_STACK_TOP))
        maze_memory_fault("software_stack_out_of_bounds", pc);
    for (unsigned i = 0; i < MAZE_CHECKPOINT_COUNT; ++i) {
        if (!p.visited[i] && pc == maze_checkpoints[i]) {
            p.visited[i] = true;
            maze_memory_scan(ram, pc);
            maze_memory_report(maze_checkpoint_names[i], pc);
        }
    }
    if (!(p.instructions & 0x3ffff)) {
        maze_memory_scan(ram, pc);
        maze_memory_report("sample", pc);
    }
}
