"""Unit tests for runtime/vk_texture.c tex_same_desc, takeback_free, epoch-view visibility, entry_pick_alias,
and runtime/vk_exec_draw.c token-0 memo gating (DKB_HOLD_TAKEBACK).

Extracts agen_idx, vk_texture_takebacks, vk_texture_view, vk_texture_view_active, tex_is_released,
tb_bucket_stale, tex_same_desc, takeback_free, entry_pick_alias, and vk_texture_slot_token_again verbatim
from runtime/vk_texture.c, plus slot_still and tex_info_lookup verbatim from runtime/vk_exec_draw.c, and
compiles a C fixture at -O0 and -O2 with -Wall -Wextra -Werror (no Vulkan loader or GPU).
"""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def extract_function(text, signature):
    i = text.index(signature)
    depth, k = 0, text.index('{', i)
    while True:
        depth += text[k] == '{'
        depth -= text[k] == '}'
        if text[k] == '}' and not depth:
            return text[i:k + 1]
        k += 1


def build_source():
    tex_src = (ROOT / 'runtime' / 'vk_texture.c').read_text(encoding='utf-8')
    draw_src = (ROOT / 'runtime' / 'vk_exec_draw.c').read_text(encoding='utf-8')
    tex_sigs = (
        'static uint32_t agen_idx(',
        'uint64_t vk_texture_takebacks(',
        'void vk_texture_view(',
        'int vk_texture_view_active(',
        'static inline int tex_is_released(',
        'static inline int tb_bucket_stale(',
        'static int same_but_swizzle(',
        'static int tex_same_desc(',
        'static int takeback_free(',
        'static Tex *entry_pick_alias(',
        'uint64_t vk_texture_slot_token_again(',
    )
    draw_sigs = (
        'static int slot_still(',
        'static void tex_info_lookup(',
    )
    extracted = [extract_function(tex_src, s) for s in tex_sigs]
    extracted_draw = [extract_function(draw_src, s) for s in draw_sigs]

    prelude = r'''#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "nvn_hmap.h"
#include "nvn_obj.h"
#include "vk.h"

#define AGEN_N 4096
#define RGEN_N 4096

typedef struct {
    uint64_t addr;
    nvn_texdesc d;
    uint8_t has_layout, layout[8];
} TexKey;

typedef struct Tex {
    VkTexImage pub;
    TexKey key;
    tex_t t;
    int released;
    uint64_t live_seq;
    struct Tex *next_at;
    int mock_gpu_content;
    int mock_destroyed;
} Tex;

static uint32_t agen[AGEN_N];
static uint64_t agen_tb[AGEN_N];
static uint32_t rgen[RGEN_N];
static uint64_t tex_tb_seq;
static _Thread_local uint64_t view_upto;

static int mock_knob = 1;
static int mock_ab = 1;

int rt_knob(const char *name, const char *def) {
    (void)name; (void)def;
    return mock_knob;
}
int rt_ab(void) { return mock_ab; }
void rt_fatal(const char *fmt, ...) { (void)fmt; abort(); }

static uint64_t n_tb_not_target, n_tb_desc, n_tb_upload;
static int takeback_mode = -1;
static _Thread_local int target_lookup;

// Mocks for entry_pick_alias (by_addr hash map, destroy_tex, alias helpers).
static nvn_hmap by_addr;
static int tex_frozen;
static int tex_alias = 1;
static int mock_destroy_count;

static void destroy_tex(Tex *e) {
    e->mock_destroyed++;
    mock_destroy_count++;
}
static int alias_compatible(const Tex *e, const Tex *p) {
    (void)e; (void)p;
    return 1;
}
static int alias_has_gpu_content(const Tex *p) { return p->mock_gpu_content; }

// Minimal VkExec / DrawCtx mocks for verbatim slot_still + tex_info_lookup extraction.
typedef struct VkExec {
    dpool_ref tex_pool;
    int nfbl;
} VkExec;

typedef struct {
    uint32_t context, gen, epoch, sgen;
    uint64_t h, tok;
    uint16_t type;
    uint8_t separate, valid;
    VkImage image;
    VkDescriptorImageInfo out;
} MockTdEntry;

typedef struct DrawCtx {
    uint32_t descriptor_serial;
    MockTdEntry TD[8192];
} DrawCtx;

static DrawCtx mock_dc = {.descriptor_serial = 1};
#define DC_OF(ex) (&mock_dc)

static uint64_t mock_token_val;
static int mock_full_calls, mock_lock_calls, mock_by_id_calls;

static int td_mode(void) { return 1; }
static int td_hash_on(void) { return 1; }
static int in_fbl(const VkExec *ex, VkImage img) { (void)ex; (void)img; return 0; }
static uint64_t token_of(const VkExec *ex, uint64_t h) { (void)ex; (void)h; return mock_token_val; }
void vk_texture_lock(void) { mock_lock_calls++; }
const VkTexImage *vk_texture_by_id_locked(const dpool_ref *pool, int id, view_t *view, int *has_view) {
    (void)pool; (void)id; (void)view; (void)has_view;
    mock_by_id_calls++;
    return NULL;
}
static void tex_info_full(VkExec *ex, const GfxShaderRes *r, uint64_t h, int storage, int separate,
                          VkDescriptorImageInfo *out, VkImage *image) {
    (void)ex; (void)r; (void)h; (void)storage; (void)separate;
    mock_full_calls++;
    *image = (VkImage)(uintptr_t)0x1234;
    *out = (VkDescriptorImageInfo){.sampler = (VkSampler)(uintptr_t)11, .imageView = (VkImageView)(uintptr_t)22,
                                   .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
}
'''

    cases = r'''
static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static void init_tex(tex_t *t, uint8_t pad_fill) {
    memset(t, pad_fill, sizeof *t);
    t->magic = MAGIC('T', 'E', 'X', ' ');
    t->d.target = NVN_TEXTURE_TARGET_2D;
    t->d.format = NVN_FORMAT_RGBA8;
    t->d.flags = NVN_TEXTURE_FLAGS_COMPRESSIBLE;
    t->d.samples = 1;
    t->d.width = 1280;
    t->d.height = 720;
    t->d.depth = 1;
    t->d.levels = 1;
    t->d.stride = 0;
    t->d.pkg_block_h = 4;
    t->d.swizzle_r = 2;
    t->d.swizzle_g = 3;
    t->d.swizzle_b = 4;
    t->d.swizzle_a = 5;
    t->d.swizzle_pad = pad_fill & 0xfffu;
    t->has_layout = 1;
    for (int i = 0; i < 8; i++) t->layout[i] = (uint8_t)(0x10 + i);
    t->L.size = 1280ull * 720ull * 4ull;
    t->L.layer_size = t->L.size;
    t->L.align = 512;
    t->L.layers = 1;
    t->L.bpb = 4;
    t->L.tileH = 4;
    t->L.tileD = 0;
    t->mem = (uint8_t *)(uintptr_t)0x200000000ull;
    t->pool = (pool_t *)(uintptr_t)0x100000000ull;
    t->off = 0x4000;
    t->serial = 1;
}

int main(void) {
    // 1. tex_same_desc ignores magic, serial, swizzle_pad, struct padding, and layout bytes when !has_layout.
    tex_t a, b;
    init_tex(&a, 0xaa);
    init_tex(&b, 0x55);
    b.magic = 0xdeadbeefu;
    b.serial = 999999ull;
    b.d.swizzle_pad = 0x777u;
    CHECK(tex_same_desc(&a, &b) == 1);

    a.has_layout = b.has_layout = 0;
    memset(a.layout, 0x11, sizeof a.layout);
    memset(b.layout, 0x22, sizeof b.layout);
    CHECK(tex_same_desc(&a, &b) == 1);

    // 2. tex_same_desc catches every semantic field change.
    init_tex(&a, 0x00);
    #define MUTATE_CHECK(stmt) do { b = a; stmt; CHECK(tex_same_desc(&a, &b) == 0); } while (0)
    MUTATE_CHECK(b.mem += 4096);
    MUTATE_CHECK(b.pool = (pool_t *)(uintptr_t)0x100001000ull);
    MUTATE_CHECK(b.off += 64);
    MUTATE_CHECK(b.has_layout = 0);
    MUTATE_CHECK(b.layout[7] ^= 1);
    MUTATE_CHECK(b.d.target = NVN_TEXTURE_TARGET_2D_ARRAY);
    MUTATE_CHECK(b.d.format = NVN_FORMAT_RGBA16F);
    MUTATE_CHECK(b.d.flags ^= 1);
    MUTATE_CHECK(b.d.samples = 4);
    MUTATE_CHECK(b.d.width += 1);
    MUTATE_CHECK(b.d.height += 1);
    MUTATE_CHECK(b.d.depth += 1);
    MUTATE_CHECK(b.d.levels += 1);
    MUTATE_CHECK(b.d.stride = 5120);
    MUTATE_CHECK(b.d.pkg_block_h = 2);
    MUTATE_CHECK(b.d.swizzle_r = 1);
    MUTATE_CHECK(b.d.swizzle_g = 1);
    MUTATE_CHECK(b.d.swizzle_b = 1);
    MUTATE_CHECK(b.d.swizzle_a = 1);
    MUTATE_CHECK(b.L.size += 512);
    MUTATE_CHECK(b.L.layer_size += 512);
    MUTATE_CHECK(b.L.align = 1024);
    MUTATE_CHECK(b.L.layers = 2);
    MUTATE_CHECK(b.L.bpb = 8);
    MUTATE_CHECK(b.L.tileH = 3);
    MUTATE_CHECK(b.L.tileD = 1);
    #undef MUTATE_CHECK

    // 3. takeback_free tests (including with by_addr siblings present: allowed via epoch-view).
    Tex e = {0}, sibling = {0};
    init_tex(&e.t, 0xaa);
    e.pub.mirrored = 1;
    e.pub.gpu_owned = 1;
    e.next_at = &sibling;   // sibling at same address must NOT block takeback_free anymore
    TexKey k = {0};
    k.addr = (uint64_t)(uintptr_t)e.t.mem;

    tex_t reinit;
    init_tex(&reinit, 0x55);
    reinit.serial = e.t.serial + 42;
    reinit.magic = 0x12345678u;

    mock_knob = 1, takeback_mode = -1, target_lookup = 1;
    CHECK(takeback_free(&e, &reinit, &k) == 1);
    CHECK(n_tb_not_target == 0 && n_tb_desc == 0 && n_tb_upload == 0);

    // Non-mirrored RT with gpu_owned=0 also passes.
    e.pub.mirrored = 0, e.pub.gpu_owned = 0;
    CHECK(takeback_free(&e, &reinit, &k) == 1);
    e.pub.mirrored = 1, e.pub.gpu_owned = 1;

    // Rejects !target_lookup and counts n_tb_not_target.
    target_lookup = 0;
    CHECK(takeback_free(&e, &reinit, &k) == 0);
    CHECK(n_tb_not_target == 1 && n_tb_desc == 0 && n_tb_upload == 0);
    target_lookup = 1;

    // Rejects descriptor mismatch and counts n_tb_desc.
    tex_t diff = reinit;
    diff.d.format = NVN_FORMAT_RGBA16F;
    CHECK(takeback_free(&e, &diff, &k) == 0);
    CHECK(n_tb_not_target == 1 && n_tb_desc == 1 && n_tb_upload == 0);

    // Rejects mirrored && !gpu_owned and counts n_tb_upload.
    e.pub.mirrored = 1, e.pub.gpu_owned = 0;
    CHECK(takeback_free(&e, &reinit, &k) == 0);
    CHECK(n_tb_not_target == 1 && n_tb_desc == 1 && n_tb_upload == 1);
    e.pub.gpu_owned = 1;

    // Knob OFF (0) and A/B block OFF (mode=2, rt_ab=0) decline early without bumping decline counters.
    uint64_t sum0 = n_tb_not_target + n_tb_desc + n_tb_upload;
    mock_knob = 0, takeback_mode = -1;
    CHECK(takeback_free(&e, &reinit, &k) == 0);
    mock_knob = 2, mock_ab = 0, takeback_mode = -1;
    CHECK(takeback_free(&e, &reinit, &k) == 0);
    CHECK(n_tb_not_target + n_tb_desc + n_tb_upload == sum0);
    mock_ab = 1;
    CHECK(takeback_free(&e, &reinit, &k) == 1);

    // 4. Epoch visibility (tex_is_released) across walk vs held slice vs newer pass.
    e.released = 1, e.live_seq = 0;
    vk_texture_view(0);
    CHECK(tex_is_released(&e) == 1);
    vk_texture_view(1);
    CHECK(tex_is_released(&e) == 1);

    // Taken back with hold_barrier (live_seq == 0): visible everywhere.
    e.released = 0, e.live_seq = 0;
    vk_texture_view(0);
    CHECK(tex_is_released(&e) == 0);
    vk_texture_view(1);
    CHECK(tex_is_released(&e) == 0);

    // Simulate pass A starting at tex_tb_seq == 4 -> view_upto = 5; then takeback bumps tex_tb_seq to 5.
    tex_tb_seq = 4;
    uint64_t pass_a_upto = vk_texture_takebacks() + 1;   // 5
    uint64_t seq = __atomic_add_fetch(&tex_tb_seq, 1, __ATOMIC_RELEASE);   // 5
    uint32_t aidx = agen_idx(k.addr);
    uint32_t other_aidx = (aidx + 1) & (AGEN_N - 1);
    e.released = 0, e.live_seq = seq;
    __atomic_store_n(&agen_tb[aidx], seq, __ATOMIC_RELEASE);
    uint64_t pass_b_upto = vk_texture_takebacks() + 1;   // 6

    // Walk (view_upto == 0) sees e as live and bucket not stale.
    vk_texture_view(0);
    CHECK(tex_is_released(&e) == 0);
    CHECK(tb_bucket_stale(aidx) == 0);

    // Held slice from pass A (started before takeback, view_upto == 5): e is hidden as released, bucket is stale.
    vk_texture_view(pass_a_upto);
    CHECK(tex_is_released(&e) == 1);
    CHECK(tb_bucket_stale(aidx) == 1);
    CHECK(tb_bucket_stale(other_aidx) == 0);

    // Token cache invalidation via vk_texture_slot_token_again on stale bucket vs unrelated bucket.
    agen[aidx] = 7, agen[other_aidx] = 9;
    rgen[1] = 3;
    VkSlotStamp st_hit = {.serial = 42, .ridx = 1, .rgen = 3, .aidx = aidx, .valid = 1};
    VkSlotStamp st_other = {.serial = 43, .ridx = 1, .rgen = 3, .aidx = other_aidx, .valid = 1};
    CHECK(vk_texture_slot_token_again(&st_hit) == 0);
    CHECK(vk_texture_slot_token_again(&st_other) == ((uint64_t)43 << 32 | 9));

    // Newer pass B (started after takeback, view_upto == 6): sees e as live and bucket not stale.
    vk_texture_view(pass_b_upto);
    CHECK(vk_texture_view_active() == 1);
    CHECK(tex_is_released(&e) == 0);
    CHECK(tb_bucket_stale(aidx) == 0);
    CHECK(vk_texture_slot_token_again(&st_hit) == ((uint64_t)42 << 32 | 7));
    vk_texture_view(0);
    CHECK(vk_texture_view_active() == 0);

    // 5. entry_pick_alias under view_upto: walk-live texture hidden from view_upto must be skipped as alias source
    //    AND NEVER destroyed; genuinely released textures are destroyed iff !tex_frozen; visible live ones alias.
    nvn_hm_init(&by_addr, sizeof(uint64_t));
    Tex target = {0}, p_hidden = {0}, p_rel = {0}, p_vis = {0};
    TexKey tk = {.addr = 0x200000000ull};
    tk.d.format = NVN_FORMAT_RGBA8;
    tk.d.width = 512;
    tk.d.height = 512;
    target.key = tk;

    // Case 5a: only a walk-live entry hidden by view_upto (released == 0, live_seq == 5 >= view_upto == 5).
    p_hidden.key = tk;
    p_hidden.released = 0;
    p_hidden.live_seq = 5;
    p_hidden.mock_gpu_content = 1;
    p_hidden.next_at = NULL;
    nvn_hm_put(&by_addr, &tk.addr, &p_hidden);
    tex_frozen = 0;
    tex_alias = 1;
    mock_destroy_count = 0;
    vk_texture_view(5);
    int alias = -1;
    Tex *src = entry_pick_alias(&target, &tk, &alias);
    CHECK(alias == 0 && src == NULL);
    CHECK(p_hidden.mock_destroyed == 0 && mock_destroy_count == 0);

    // Same entry under view_upto == 0 (walk) or view_upto == 6 (newer pass) IS visible and picked as alias source.
    vk_texture_view(0);
    src = entry_pick_alias(&target, &tk, &alias);
    CHECK(alias == 1 && src == &p_hidden && p_hidden.mock_destroyed == 0);
    vk_texture_view(6);
    src = entry_pick_alias(&target, &tk, &alias);
    CHECK(alias == 1 && src == &p_hidden && p_hidden.mock_destroyed == 0);

    // Case 5b: mixed chain (p_hidden -> p_rel -> p_vis) under view_upto == 5, tex_frozen == 0.
    // p_hidden is walk-live (NOT destroyed, NOT aliased); p_rel is genuinely released (destroyed);
    // p_vis is live at epoch 3 < 5 (NOT destroyed, chosen as alias source).
    p_rel.key = tk;
    p_rel.released = 1;
    p_rel.live_seq = 0;
    p_rel.mock_gpu_content = 1;
    p_vis.key = tk;
    p_vis.released = 0;
    p_vis.live_seq = 3;
    p_vis.mock_gpu_content = 1;
    p_hidden.next_at = &p_rel;
    p_rel.next_at = &p_vis;
    p_vis.next_at = NULL;
    vk_texture_view(5);
    tex_frozen = 0;
    src = entry_pick_alias(&target, &tk, &alias);
    CHECK(alias == 1 && src == &p_vis);
    CHECK(p_hidden.mock_destroyed == 0);
    CHECK(p_rel.mock_destroyed == 1 && mock_destroy_count == 1);
    CHECK(p_vis.mock_destroyed == 0);

    // Case 5c: tex_frozen == 1 keeps genuinely released p_rel alive too.
    p_rel.mock_destroyed = 0;
    mock_destroy_count = 0;
    tex_frozen = 1;
    src = entry_pick_alias(&target, &tk, &alias);
    CHECK(alias == 1 && src == &p_vis);
    CHECK(p_hidden.mock_destroyed == 0 && p_rel.mock_destroyed == 0 && mock_destroy_count == 0);
    tex_frozen = 0;
    vk_texture_view(0);
    free(by_addr.keys);
    free(by_addr.vals);

    // 6. Token-0 memo gating (slot_still + tex_info_lookup):
    //    view_upto == 0 caches token 0 and hits on repeat while gen is unchanged;
    //    view_upto != 0 refuses token-0 memo hits and skips caching token 0.
    VkExec ex = {0};
    GfxShaderRes res = {.slot = 0, .type = GFX_TEX_2D};
    VkDescriptorImageInfo out = {0};
    const uint64_t handle = (UINT64_C(7) << 20) | UINT64_C(99);
    const uint32_t gen = 10, ep = 1, sg = 2;
    int locked = 0;

    memset(mock_dc.TD, 0, sizeof mock_dc.TD);
    mock_full_calls = mock_lock_calls = mock_by_id_calls = 0;
    mock_token_val = 0;
    vk_texture_view(0);

    // First lookup at view_upto == 0 with tok == 0 populates memo (valid == 1, tok == 0).
    tex_info_lookup(&ex, &res, handle, 0, 0, gen, ep, sg, &locked, &out);
    CHECK(mock_full_calls == 1);
    // Repeat lookup with same gen hits memo without calling tex_info_full again.
    locked = 0;
    tex_info_lookup(&ex, &res, handle, 0, 0, gen, ep, sg, &locked, &out);
    CHECK(mock_full_calls == 1 && locked == 0);
    // Direct slot_still check: token 0 hits at same gen when !view_active, misses on gen change.
    uint32_t egen = gen;
    CHECK(slot_still(&ex, &egen, 0, handle, gen) == 1);
    CHECK(slot_still(&ex, &egen, 0, handle, gen + 1) == 0);

    // Switching to view_upto != 0 must NOT hit the previously cached token-0 entry, and must clear m->valid.
    vk_texture_view(5);
    egen = gen;
    CHECK(slot_still(&ex, &egen, 0, handle, gen) == 0);
    locked = 0;
    tex_info_lookup(&ex, &res, handle, 0, 0, gen, ep, sg, &locked, &out);
    CHECK(mock_full_calls == 2);
    // Second lookup under view_upto != 0 with tok == 0 must also miss (token 0 was not cached).
    locked = 0;
    tex_info_lookup(&ex, &res, handle, 0, 0, gen, ep, sg, &locked, &out);
    CHECK(mock_full_calls == 3);

    // Under view_upto != 0, a non-zero token STILL caches and hits normally.
    mock_token_val = (UINT64_C(12) << 32) | 5;
    locked = 0;
    tex_info_lookup(&ex, &res, handle, 0, 0, gen, ep, sg, &locked, &out);
    CHECK(mock_full_calls == 4);
    locked = 0;
    tex_info_lookup(&ex, &res, handle, 0, 0, gen, ep, sg, &locked, &out);
    CHECK(mock_full_calls == 4 && locked == 0);
    vk_texture_view(0);

    printf("hold_takeback: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
'''
    return '\n'.join([prelude, *extracted, *extracted_draw, cases])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', type=Path, required=True)
    parser.add_argument('--deps', type=Path, required=True)
    parser.add_argument('--out', type=Path, default=ROOT / 'build' / 'tests' / 'hold-takeback')
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    src = args.out / 'hold_takeback.c'
    exe = args.out / 'hold_takeback.exe'
    src.write_text(build_source(), encoding='utf-8')
    includes = ['-I', str(ROOT / 'runtime'), '-I', str(ROOT / 'runtime' / 'third_party'),
                '-I', str(args.deps / 'include'), '-I', str(args.deps / 'include' / 'vma')]
    for opt in ('-O0', '-O2'):
        subprocess.run([str(args.cc), '-std=gnu11', opt, '-Wall', '-Wextra', '-Werror',
                        *includes, str(src), '-o', str(exe)], check=True, timeout=120)
        run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=120)
        print(opt, run.stdout.strip())
        if run.stderr:
            print(run.stderr.strip(), file=sys.stderr)
        if run.returncode:
            return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
