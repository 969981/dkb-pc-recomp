// Offline check of the Ampere-to-Maxwell lowering (runtime/third_party/eglantine) over a DKB_SHADER_DUMP folder:
// every <hash>.bin (code; graphics programs start with the 0x80-byte SPH v4) is captured as the runtime will capture
// it and lowered. Prints how many lower, and the failure reasons with the opcodes they stop at.
//
//   clang++ -std=c++20 -O1 -I runtime/third_party/eglantine tools/dkb_lower_test.cpp -o build/dkb_lower_test.exe
//   build/dkb_lower_test.exe <dump folder> [tensor mode, e.g. full]
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "shader_recompiler/frontend/ampere/program_capture.h"

using namespace Shader::Ampere;

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <dump folder>\n", argv[0]);
        return 2;
    }
    int ok = 0, total = 0;
    size_t in_insns = 0, out_insns = 0;
    std::map<std::string, int> reasons;
    std::map<std::string, std::map<unsigned, int>> reason_ops;
    for (const auto &e : std::filesystem::directory_iterator(argv[1])) {
        if (e.path().extension() != ".bin") continue;
        std::ifstream f(e.path(), std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(f)), {});
        if (bytes.size() < 16) continue;
        u32 w0;
        std::memcpy(&w0, bytes.data(), 4);
        const bool graphics = ((w0 & 31) == 1 || (w0 & 31) == 2) && ((w0 >> 5) & 31) == 4;
        const size_t start = graphics ? 0x80 : 0;
        std::vector<Instruction> native;
        ProgramCaptureState capture;
        for (size_t at = start; at + 16 <= bytes.size(); at += 16) {
            Instruction insn;
            std::memcpy(insn.words.data(), bytes.data() + at, 16);
            native.push_back(insn);
            const auto r = capture.Observe(insn, static_cast<u32>(native.size() - 1));
            if (r == ProgramCaptureState::Result::Complete || r == ProgramCaptureState::Result::InvalidTarget) break;
        }
        ++total;
        const auto lowered = LowerToMaxwell(native, argc > 2 ? argv[2] : "");
        in_insns += native.size();
        if (lowered) {
            ++ok;
            out_insns += lowered.instructions.size();
        } else {
            std::string why{lowered.error};
            ++reasons[why];
            const size_t i = lowered.error_offset / sizeof(Instruction);
            if (i < native.size()) ++reason_ops[why][native[i].Bits(0, 12)];
        }
    }
    std::printf("%d of %d programs lower (%.1f%%); %zu native instructions, %zu Maxwell out for the lowered ones\n",
                ok, total, total ? 100.0 * ok / total : 0.0, in_insns, out_insns);
    for (const auto &[why, n] : reasons) {
        std::printf("%6d  %s:", n, why.c_str());
        int shown = 0;
        for (const auto &[op, k] : reason_ops[why])
            if (shown++ < 12) std::printf(" %03x x%d", op, k);
        std::printf("\n");
    }
    return 0;
}
