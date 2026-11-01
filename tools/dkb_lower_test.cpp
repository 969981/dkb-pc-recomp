// Offline check of the NVN 2 shader path over a DKB_SHADER_DUMP folder (<hash>.ctl control, <hash>.bin code): every
// stage goes through runtime/nvn2_lower.cpp exactly as nvnProgramSetShaders sends it. Prints how many lower. With an
// output folder it also writes each lowered stage as TotK Recomp's translator takes it (what gfx_shader.c writes:
// graphics from +0x30, compute whole) and <out>/jobs.json for `totk_xlate <jobs.json> <outdir>`.
//
//   clang++ -std=c++20 -O1 -I runtime/third_party/eglantine tools/dkb_lower_test.cpp -o build/dkb_lower_test.exe
//   build/dkb_lower_test.exe <dump folder> [out folder]
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include "../runtime/nvn2_lower.cpp"

static std::map<std::string, int> reasons;
extern "C" void rt_log(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::string s{buf};
    ++reasons[s.substr(s.find(": ") == std::string::npos ? 0 : s.find(": ") + 2)];
}

static std::vector<uint8_t> slurp(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

int main(int argc, char **argv) {
    if (argc < 2) return std::fprintf(stderr, "usage: %s <dump folder> [out folder]\n", argv[0]), 2;
    const std::filesystem::path out = argc > 2 ? argv[2] : "";
    std::ofstream jobs;
    if (!out.empty()) std::filesystem::create_directories(out), jobs.open(out / "jobs.json"), jobs << "[\n";
    int ok = 0, total = 0;
    for (const auto &e : std::filesystem::directory_iterator(argv[1])) {
        if (e.path().extension() != ".bin") continue;
        auto code = slurp(e.path());
        auto ctl = slurp(std::filesystem::path(e.path()).replace_extension(".ctl"));
        if (ctl.size() < 0xd8) continue;
        ++total;
        const uint8_t *nctl, *ncode;
        if (!nvn2_lower(ctl.data(), code.data(), &nctl, &ncode)) continue;
        ++ok;
        if (out.empty()) continue;
        uint32_t len, stage;
        std::memcpy(&len, nctl + 0x704, 4), std::memcpy(&stage, nctl + 0x714, 4);
        const char *st = stage == 0 ? "vs" : stage == 1 ? "ps" : stage == 5 ? "cs" : nullptr;
        if (!st) continue;
        const uint32_t skip = stage == 5 ? 0 : 0x30;
        const auto key = e.path().stem().string();
        std::ofstream(out / (key + ".code"), std::ios::binary).write(reinterpret_cast<const char *>(ncode + skip), len - skip);
        uint32_t local[3];
        std::memcpy(local, nctl + 0x748, 12);
        jobs << (ok > 1 ? ",\n" : "") << "{\"key\":\"" << key << "\",\"stage\":\"" << st << "\",\"code\":\""
             << (out / (key + ".code")).generic_string() << "\",\"c1\":\"\"";
        if (stage == 5) jobs << ",\"local\":[" << local[0] << "," << local[1] << "," << local[2] << "]";
        jobs << "}";
    }
    if (!out.empty()) jobs << "\n]\n";
    std::printf("%d of %d stages lower (%.1f%%)\n", ok, total, total ? 100.0 * ok / total : 0.0);
    for (const auto &[why, n] : reasons) std::printf("%6d  %s\n", n, why.c_str());
    return 0;
}
