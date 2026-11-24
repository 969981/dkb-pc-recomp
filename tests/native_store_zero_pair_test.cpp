// A 64-bit store of RZ to a register-plus-uniform address (STG.E.64 [R4+UR4+0x10], RZ, #439) must store
// zeros, not the address: lower_to_maxwell.h sums that address into R250-R251, so the zeros must be
// elsewhere. The 128-bit form would overlap the address pair and is refused.
#include <array>
#include <cstdio>
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
using namespace Shader::Ampere;
static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); } } while (0)
static LoweredProgram lower(Instruction op) {
    Instruction end{}; end.words[0] = (7u << 12) | 0x94d; end.words[2] = 7u << 23;
    return LowerToMaxwell(std::array{op, end}, "store-zero-pair");
}
int main() {
    // 0x986, PT, address R4, data RZ, offset 0x10, UR4, extended, size 5 (64 bits).
    Instruction store{{(4u << 24) | (7u << 12) | 0x986, (0x10u << 8) | 0xff, 0x0810a100 | (5u << 9) | 4, 0x000fca00}};
    auto a = lower(store);
    CHECK(bool(a));
    unsigned stores = 0;
    for (u64 op : a.instructions) {
        if ((op >> 48) != 0xeed8 + 5) continue; // STG with size 5 in bits 48-50
        ++stores;
        const u32 data = op & 0xff, address = (op >> 8) & 0xff;
        CHECK(data == 248);
        CHECK(address == 250);
    }
    CHECK(stores == 1);
    Instruction wide = store; wide.words[2] = 0x0810a100 | (6u << 9) | 4; // 128 bits
    CHECK(!lower(wide));
    Instruction uniform_only = wide; uniform_only.words[0] = (255u << 24) | (7u << 12) | 0x986;
    CHECK(bool(lower(uniform_only)));
    std::printf("native store zero pair: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
