// NVN 2 program stage -> the NVN 1 shape the rest of the host takes (gfx_shader.c, vk_exec_draw.c): Eglantine's
// lowering (runtime/third_party/eglantine, GPL-3.0-or-later) rewrites the Ampere code as Maxwell, laid out as TotK's
// translator reads it, and a synthetic control block carries the fields the host reads.
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

#include "shader_recompiler/frontend/ampere/program_capture.h"

extern "C" void rt_log(const char *fmt, ...);

namespace {
constexpr size_t kControl = 0x880;   // gfx_shader.c CONTROL_SIZE
constexpr uint64_t kMaxwellNop = 0x50b0000000070f00ull;

struct Lowered { std::vector<uint8_t> control; std::vector<uint64_t> code; };
std::mutex lock;
// ponytail: kept for the process (a few thousand small stages); free with the program if memory ever matters
std::map<std::pair<const void *, const void *>, Lowered *> cache;

void put32(std::vector<uint8_t> &c, size_t at, uint32_t v) { std::memcpy(c.data() + at, &v, 4); }

Lowered *lower(const uint8_t *control, const uint8_t *code) {
    uint32_t code_len, w0;
    std::memcpy(&code_len, control + 0x38, 4), std::memcpy(&w0, code, 4);
    const bool gfx = ((w0 & 31) == 1 || (w0 & 31) == 2) && ((w0 >> 5) & 31) == 4;   // SPH v4
    const uint32_t hdr = gfx ? 0x80 : 0;
    if (code_len <= hdr) return nullptr;

    std::vector<Shader::Ampere::Instruction> native;
    Shader::Ampere::ProgramCaptureState capture;
    for (uint32_t at = hdr; at + 16 <= code_len; at += 16) {
        Shader::Ampere::Instruction insn;
        std::memcpy(insn.words.data(), code + at, 16);
        native.push_back(insn);
        const auto r = capture.Observe(insn, static_cast<uint32_t>(native.size() - 1));
        if (r != Shader::Ampere::ProgramCaptureState::Result::Continue) break;
    }
    const auto lowered = Shader::Ampere::LowerToMaxwell(native, "full");
    if (!lowered) {
        rt_log("nvn2: Ampere lowering failed at +%#x: %.*s", lowered.error_offset, (int)lowered.error.size(),
               lowered.error.data());
        return nullptr;
    }

    auto *out = new Lowered;
    auto &m = out->code;
    // The lowering places instruction 0 at 0x10 modulo 32 past the program start, with a scheduling word at every
    // 32-byte boundary. Graphics: 0x30 bytes the host skips, then the first 0x50 bytes of the SPH, so instruction 0
    // lands at SPH+0x50. Compute (decoded from 0): a scheduling word and a NOP in front.
    size_t pos;
    if (gfx) {
        m.assign(6, 0);
        m.resize(16);
        std::memcpy(m.data() + 6, code, 0x50);
        pos = 0x50;
    } else {
        m = {0, kMaxwellNop};
        pos = 0x10;
    }
    for (const uint64_t insn : lowered.instructions) {
        if (pos % 32 == 0) m.push_back(0), pos += 8;
        m.push_back(insn), pos += 8;
    }
    m.push_back(0);

    out->control.assign(kControl, 0);
    const uint32_t len = static_cast<uint32_t>(m.size() * 8);
    const uint32_t type = (w0 >> 10) & 15;   // SPH shader type -> NVN stage
    const uint32_t stage = !gfx ? 5 : type == 5 ? 1 : type == 4 ? 2 : type == 2 ? 3 : type == 3 ? 4 : 0;
    put32(out->control, 0x700, len);   // no inline constants (no NVN 2 stage reads c[0x1])
    put32(out->control, 0x704, len);
    put32(out->control, 0x714, stage);
    if (!gfx) std::memcpy(out->control.data() + 0x748, control + 0x58, 12);   // local size (NVN 2 control +0x58)
    return out;
}
}  // namespace

// 1 and the replacement control and code for an NVN 2 stage, 0 when it does not lower.
extern "C" int nvn2_lower(const uint8_t *control, const uint8_t *code, const uint8_t **out_control,
                          const uint8_t **out_code) {
    std::lock_guard<std::mutex> g(lock);
    auto [it, fresh] = cache.try_emplace({control, code}, nullptr);
    if (fresh) it->second = lower(control, code);
    if (!it->second) return 0;
    *out_control = it->second->control.data();
    *out_code = reinterpret_cast<const uint8_t *>(it->second->code.data());
    return 1;
}
