"""Run the real shader-request guards against synthetic short/old/new controls, without a GPU or translator.

The short control ends at a protected page: reading the NVN 1 layout before rejecting NVN 2 must fault.
Usage: python tests/nvn2_shader_control_test.py [--cc clang]
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

from nvn2_compute_c0_test import function

ROOT = Path(__file__).resolve().parents[1]


def source():
    production = (ROOT / 'runtime' / 'gfx_shader.c').read_text()
    # Stub only translation/cache dependencies reached after the real request guards.
    before = r'''
#include <stdint.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
#define NVN2_C0_LAYOUT 0x860
#define CODE_SKIP 0x30
typedef int UseMask;
/* Production Req and private GfxShader layouts are inserted below. */
void rt_log(const char *message, ...) { (void)message; }
static void add_res(GfxShader *s, int kind, const char *o, const char *e) { (void)s; (void)kind; (void)o; (void)e; }
static void reflect_spirv(GfxShader *s) { (void)s; }
static const char *error;
static int fail(const char *why, const char *detail) { (void)detail; error = why; return 0; }
/* The hash provider keeps the production signature for older-source compatibility. */
static void paths(void) {}
static int use_find(const uint8_t *hash, UseMask *mask) { (void)hash; (void)mask; return 0; }
static void build_key(const uint8_t *hash, const GfxShaderAnswers *answers, const UseMask *mask,
                      uint8_t *key, uint8_t *iid) { (void)hash; (void)answers; (void)mask; (void)key; (void)iid; }
'''
    after = r'''
int main(void) {
    size_t page;
    uint8_t *mapping;
#ifdef _WIN32
    SYSTEM_INFO si; GetSystemInfo(&si); page = si.dwPageSize;
    mapping = VirtualAlloc(NULL, page * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    DWORD old;
    if (!mapping || !VirtualProtect(mapping + page, page, PAGE_NOACCESS, &old)) return 2;
#else
    page = (size_t)sysconf(_SC_PAGESIZE);
    mapping = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED || mprotect(mapping + page, page, PROT_NONE)) return 2;
#endif
    uint8_t *short_control = mapping + page - 0xd8;
    const uint32_t header[2] = {0x98761234u, 0xc9u};
    memcpy(short_control, header, sizeof header);
    uint8_t code[8] = {0}, full[0x880] = {0};
    Req req = {0};
    GfxShaderAnswers answers = {0};
    int checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "shader control: failed check %d\n", checks); return 1; } } while (0)
    CHECK(!req_init(&req, GFX_STAGE_CS, short_control, code, sizeof code, NULL, &answers) &&
          error && strstr(error, "NVN 2 program"));
    memcpy(full + NVN2_C0_LAYOUT, "DKC0", 4);
    error = NULL;
    CHECK(!req_init(&req, GFX_STAGE_CS, full, code, sizeof code, NULL, &answers) &&
          error && strstr(error, "old NVN 2 compute control"));
    const uint32_t length = sizeof code;
    memcpy(full + 0x700, &length, 4); memcpy(full + 0x704, &length, 4);
    memcpy(full + NVN2_C0_LAYOUT, "DKC1", 4);
    CHECK(req_init(&req, GFX_STAGE_CS, full, code, sizeof code, NULL, &answers) && req.require_c0);
    memset(full + NVN2_C0_LAYOUT, 0, 4);
    CHECK(req_init(&req, GFX_STAGE_CS, full, code, sizeof code, NULL, &answers) && !req.require_c0);
#ifdef _WIN32
    VirtualFree(mapping, 0, MEM_RELEASE);
#else
    munmap(mapping, page * 2);
#endif
    const char *profiles[] = {
        "{\"stage\":\"cs\"}",
        "{\"stage\":\"cs\"}",
        "{\"stage\":\"cs\",\"c0\":[7,1,2,3]}",
        "{\"stage\":\"cs\",\"c0_version\":2,\"c0\":[7,1,2,3],\"c0_roots\":[3,0,5,0,0,0,0,0]}",
        "{\"stage\":\"cs\",\"c0_version\":2,\"c0\":[7,1,2,3],\"c0_roots\":[65,0,0,0,0,0,0,0]}"
    };
    const int require[] = {1,0,1,1,1}, expected[] = {0,1,0,1,0};
    for (unsigned i = 0; i < sizeof profiles / sizeof *profiles; i++) {
        GfxShader s = {.reflection=(char *)profiles[i]};
        int ok = reflect(&s, require[i]);
        CHECK(ok == expected[i] && (i != 3 || (s.has_c0 && s.c0.roots[0] == 3 && s.c0.roots[2] == 5)));
        for (int k = 0; k < 4; k++) free(s.res[k]);
    }
    printf("shader control/profile: %d checks, 0 failures\n", checks);
    return 0;
}
'''
    req_start = production.index('typedef struct {', production.index('// A validated request:'))
    req = production[req_start:production.index('} Req;', req_start) + len('} Req;')]
    shader_start = production.index('struct GfxShader {')
    shader = production[shader_start:production.index('\n};', shader_start) + len('\n};')]
    before = before.replace('/* Production Req and private GfxShader layouts are inserted below. */', req + '\n' + shader)
    hash_start = production.index('static int build_code_hash(')
    hash_signature = production[hash_start:production.index('{', hash_start)]
    extra = '(void)geometry_input;' if 'geometry_input' in hash_signature else ''
    # Use the actual parameter names; newer source calls the length code_size.
    size_name = 'code_size' if 'code_size' in hash_signature else 'length'
    hash_provider = hash_signature + '{ (void)stage; (void)control; (void)code; (void)' + size_name + ';' + extra + r'''
    (void)c1; (void)c1_len; (void)hash; return 1;
}
'''
    before = before.replace('/* The hash provider keeps the production signature for older-source compatibility. */', hash_provider)
    names = ['static const char *json_key(', 'static long json_int(', 'static int json_objects(', 'int gfx_shader_is_nvn2(']
    if 'static int geometry_json(' not in production:
        names.append('static void json_str(')
    pieces = [function(production, name) for name in names]
    if 'static int geometry_json(' in production:
        # This contiguous production block includes JSON literals with braces, which the older function extractor
        # does not lex. Keep the actual helpers and topology tables together instead of copying their behavior.
        start = production.index('static const char *json_space(')
        end = production.index('// The objects of the flat array', start)
        pieces.append('#define COUNT(a) (sizeof(a) / sizeof(*(a)))\n' + production[start:end])
        enum_start = production.index('enum { OP_ENTRY_POINT')
        pieces.append(production[enum_start:production.index('};', enum_start) + 2])
        pieces.append(function(production, 'static int geometry_spirv('))
        pieces.append('static uint32_t host_gs_sg;\n')
    pieces.append(function(production, 'static int req_init('))
    pieces.append(function(production[production.rindex('static int reflect('):], 'static int reflect('))
    return before + '\n'.join(pieces) + after


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC') or shutil.which('clang') or shutil.which('gcc'))
    args = parser.parse_args()
    if not args.cc:
        parser.error('pass --cc with a C compiler')
    out = ROOT / 'build' / 'test' / 'nvn2-shader-control'
    out.mkdir(parents=True, exist_ok=True)
    src = out / 'control.c'
    src.write_text(source())
    exe = out / ('control.exe' if os.name == 'nt' else 'control')
    subprocess.run([args.cc, '-O2', '-Wall', '-Wextra', '-I', str(ROOT / 'runtime'), str(src), '-o', str(exe)], check=True)
    return subprocess.run([str(exe)]).returncode


if __name__ == '__main__':
    raise SystemExit(main())
