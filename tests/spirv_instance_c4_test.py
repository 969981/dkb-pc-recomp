"""Vertex and fragment modules through the real spirv_instance_c4 (runtime/spirv_instance_c4.c), checked with spirv-val.
No Vulkan, GPU or game runs.

The modules have the layout the rewrite expects: a c4 uniform block (a struct of one vec4[4096], set 0 binding 4) read
through access chains with member 0 and an array index in the entry function. It checks that the vertex stage gains
gl_InstanceIndex and a flat int output at Location 29, that the fragment stage gains the matching flat input, that every
c4 read gets an OpIAdd of instance * 16 on its index, and that the results pass spirv-val. It also checks the refusals:
a stage that reads the instance itself, Location 29 taken, a fragment stage without c4, a chain to the array alone, a
read outside the entry function and a constant index past one 256-byte block. It does not execute the shaders.
"""
import argparse
from pathlib import Path
import struct
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
LOC = 29
OP_ENTRY_POINT, OP_LOAD, OP_STORE, OP_DECORATE, OP_ACCESS_CHAIN, OP_IADD, OP_IMUL = 15, 61, 62, 71, 65, 128, 132
DEC_BUILTIN, DEC_FLAT, DEC_LOCATION = 11, 14, 30
BUILTIN_INSTANCE_INDEX = 43


def instr(op, *words):
    return [(len(words) + 1) << 16 | op, *words]


def string(s):
    b = s.encode() + b'\0'
    b += b'\0' * (-len(b) % 4)
    return list(struct.unpack(f'<{len(b) // 4}I', b))


def module(vertex, reads=2, binding=4, instance=False, taken=False, short=False, helper=False, first=3):
    """Ids are fixed. reads (1 to 4): c4 reads in the entry function, each of component 1 of one vec4. instance: the
    stage already declares gl_InstanceIndex. taken: an output (vertex) or input (fragment) already sits at Location 29.
    short: one more read through a chain to the array alone. helper: one more read in a function ahead of main. first: the
    constant vec4 index of the first read; the later reads add 3 to the one before."""
    (MAIN, HELPER, VOID, FN, F32, I32, V4, ARR, STRUCT, PUNI, UBO, PUF, PUARR, PUV4, POUT, OUT, PIN_I, INST, PO_I, EXTRA,
     PI_I, K0, K1, K3, K4096, KF, *V) = range(1, 200)
    w = [0x07230203, 0x00010300, 0, 200, 0]
    w += instr(17, 1)                                                    # Shader
    w += instr(14, 0, 1)
    iface = [OUT] + ([INST] if instance else []) + ([EXTRA] if taken else [])
    w += instr(15, 0 if vertex else 4, MAIN, *string('main'), *iface)
    if not vertex:
        w += instr(16, MAIN, 7)                                          # OriginUpperLeft
    w += instr(71, OUT, 30, 0)
    if instance:
        w += instr(71, INST, DEC_BUILTIN, BUILTIN_INSTANCE_INDEX)
    if taken:
        w += instr(71, EXTRA, DEC_LOCATION, LOC) + instr(71, EXTRA, DEC_FLAT)
    w += instr(71, UBO, 34, 0) + instr(71, UBO, 33, binding)
    w += instr(71, ARR, 6, 16) + instr(71, STRUCT, 2) + instr(72, STRUCT, 0, 35, 0)
    w += instr(19, VOID) + instr(33, FN, VOID) + instr(22, F32, 32) + instr(21, I32, 32, 1) + instr(23, V4, F32, 4)
    w += instr(43, I32, K0, 0) + instr(43, I32, K1, 1) + instr(43, I32, K3, 3) + instr(43, I32, K4096, 4096)
    w += instr(43, I32, KF, first)
    w += instr(28, ARR, V4, K4096) + instr(30, STRUCT, ARR) + instr(32, PUNI, 2, STRUCT) + instr(59, PUNI, UBO, 2)
    w += instr(32, PUF, 2, F32) + instr(32, PUARR, 2, ARR) + instr(32, PUV4, 2, V4)
    w += instr(32, POUT, 3, V4) + instr(59, POUT, OUT, 3)
    if instance:
        w += instr(32, PIN_I, 1, I32) + instr(59, PIN_I, INST, 1)
    if taken:
        if vertex:
            w += instr(32, PO_I, 3, I32) + instr(59, PO_I, EXTRA, 3)
        else:
            w += instr(32, PI_I, 1, I32) + instr(59, PI_I, EXTRA, 1)
    if helper:
        w += instr(54, VOID, HELPER, 0, FN) + instr(248, V[0])
        w += instr(65, PUF, V[1], UBO, K0, K1, K0) + instr(OP_LOAD, F32, V[2], V[1]) + instr(253) + instr(56)
    w += instr(54, VOID, MAIN, 0, FN) + instr(248, V[10])
    # The array indices: first for the first read, then ids made by OpIAdd, so later reads index with a computed value.
    loads = []
    for k in range(reads):
        idx = KF if k == 0 else V[100 + k]
        if k:
            w += instr(OP_IADD, I32, idx, K3, KF if k == 1 else V[100 + k - 1])
        w += instr(OP_ACCESS_CHAIN, PUF, V[20 + k], UBO, K0, idx, K1) + instr(OP_LOAD, F32, V[40 + k], V[20 + k])
        loads.append(V[40 + k])
    if short:
        w += instr(OP_ACCESS_CHAIN, PUARR, V[60], UBO, K0) + instr(OP_ACCESS_CHAIN, PUV4, V[61], V[60], K3)
        w += instr(OP_LOAD, V4, V[62], V[61])
    if helper:
        w += instr(57, VOID, V[63], HELPER)
    loads += [loads[-1]] * (4 - len(loads))
    w += instr(80, V4, V[64], *loads[:4]) + instr(OP_STORE, OUT, V[64])
    w += instr(253) + instr(56)
    return w


def ops(words):
    i, out = 5, []
    while i < len(words):
        n = words[i] >> 16
        out.append((words[i] & 0xffff, words[i + 1:i + n]))
        i += n
    return out


class InstanceC4(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        args.out.mkdir(parents=True, exist_ok=False)
        harness = args.out / 'harness.c'
        harness.write_text(r'''#include <stdio.h>
#include <stdlib.h>
#include "spirv_instance_c4.h"
int main(int argc, char **argv) {
    if (argc != 7) return 2;
    static uint32_t w[1 << 20];
    FILE *f = fopen(argv[1], "rb");
    size_t n = fread(w, 4, 1 << 20, f);
    fclose(f);
    uint64_t out_n = 0;
    unsigned chains = 0;
    uint32_t *o = spirv_instance_c4(w, n, atoi(argv[3]), (uint32_t)strtoul(argv[4], NULL, 0),
                                    (uint32_t)strtoul(argv[5], NULL, 0), (uint32_t)atoi(argv[6]), &out_n, &chains);
    if (!o) return 3;
    f = fopen(argv[2], "wb");
    fwrite(o, 4, out_n, f);
    fclose(f);
    free(o);
    printf("%u\n", chains);
    return 0;
}
''', encoding='utf-8')
        cls.exe = args.out / 'harness.exe'
        subprocess.run([str(args.cc), '-O1', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'runtime'), str(harness),
                        str(ROOT / 'runtime/spirv_instance_c4.c'), '-o', str(cls.exe)], check=True, capture_output=True,
                       text=True, timeout=120)

    def rewrite(self, name, words, vertex, set_=0, binding=4):
        """The rewritten module's instructions and the chain count, or None when the rewrite refuses."""
        src = args.out / (name + '.in.spv')
        dst = args.out / (name + '.out.spv')
        src.write_bytes(struct.pack(f'<{len(words)}I', *words))
        pre = subprocess.run([str(args.spirv_val), '--target-env', 'vulkan1.3', str(src)], capture_output=True, text=True, timeout=30)
        self.assertEqual(pre.returncode, 0, name + ' input: ' + pre.stdout + pre.stderr)
        run = subprocess.run([str(self.exe), str(src), str(dst), str(int(vertex)), str(set_), str(binding), str(LOC)],
                             capture_output=True, text=True, timeout=30)
        if run.returncode == 3:
            return None
        self.assertEqual(run.returncode, 0, run.stderr)
        data = dst.read_bytes()
        check = subprocess.run([str(args.spirv_val), '--target-env', 'vulkan1.3', str(dst)], capture_output=True, text=True, timeout=30)
        self.assertEqual(check.returncode, 0, name + ': ' + check.stdout + check.stderr)
        return ops(list(struct.unpack(f'<{len(data) // 4}I', data))), int(run.stdout)

    def check_reads(self, found, chains, reads):
        """Every c4 chain's index is an OpIAdd whose second operand is the OpIMul of the instance by 16."""
        self.assertEqual(chains, reads)
        muls = [a[1] for o, a in found if o == OP_IMUL]
        self.assertEqual(len(muls), 1)
        adds = {a[1]: a for o, a in found if o == OP_IADD and a[3] == muls[0]}
        rewritten = [a for o, a in found if o == OP_ACCESS_CHAIN and len(a) >= 5 and a[4] in adds]
        self.assertEqual(len(rewritten), reads)
        self.assertEqual(len(adds), reads)

    def decorations(self, found):
        return [a for o, a in found if o == OP_DECORATE]

    def test_vertex_stage_offsets_reads_and_writes_the_instance(self):
        found, chains = self.rewrite('vs', module(vertex=True, reads=3), vertex=True)
        self.check_reads(found, chains, 3)
        dec = self.decorations(found)
        inst = [a[0] for a in dec if a[1:] == [DEC_BUILTIN, BUILTIN_INSTANCE_INDEX]]
        out = [a[0] for a in dec if a[1:] == [DEC_LOCATION, LOC]]
        self.assertEqual(len(inst), 1)
        self.assertEqual(len(out), 1)
        self.assertIn([out[0], DEC_FLAT], dec)
        entry = next(a for o, a in found if o == OP_ENTRY_POINT)
        self.assertIn(inst[0], entry)
        self.assertIn(out[0], entry)
        self.assertTrue(any(o == OP_STORE and a[0] == out[0] for o, a in found))

    def test_vertex_stage_without_c4_still_passes_the_instance_on(self):
        found, chains = self.rewrite('vs-noc4', module(vertex=True, reads=2), vertex=True, set_=0xffffffff)
        self.assertEqual(chains, 0)
        muls = [a[1] for o, a in found if o == OP_IMUL]
        self.assertFalse(any(o == OP_IADD and a[3] in muls for o, a in found))
        self.assertTrue(any(a[1:] == [DEC_LOCATION, LOC] for a in self.decorations(found)))

    def test_fragment_stage_reads_the_flat_input(self):
        found, chains = self.rewrite('fs', module(vertex=False, reads=2), vertex=False)
        self.check_reads(found, chains, 2)
        dec = self.decorations(found)
        self.assertFalse(any(a[1:] == [DEC_BUILTIN, BUILTIN_INSTANCE_INDEX] for a in dec))
        src = [a[0] for a in dec if a[1:] == [DEC_LOCATION, LOC]]
        self.assertEqual(len(src), 1)
        self.assertIn([src[0], DEC_FLAT], dec)
        entry = next(a for o, a in found if o == OP_ENTRY_POINT)
        self.assertIn(src[0], entry)

    def test_a_stage_that_reads_the_instance_is_refused(self):
        self.assertIsNone(self.rewrite('vs-inst', module(vertex=True, instance=True), vertex=True))

    def test_a_taken_location_is_refused(self):
        self.assertIsNone(self.rewrite('vs-taken', module(vertex=True, taken=True), vertex=True))
        self.assertIsNone(self.rewrite('fs-taken', module(vertex=False, taken=True), vertex=False))

    def test_a_fragment_stage_without_c4_is_refused(self):
        self.assertIsNone(self.rewrite('fs-noset', module(vertex=False), vertex=False, set_=0xffffffff))
        self.assertIsNone(self.rewrite('fs-other', module(vertex=False, binding=5), vertex=False))

    def test_a_chain_to_the_array_alone_is_refused(self):
        self.assertIsNone(self.rewrite('vs-short', module(vertex=True, short=True), vertex=True))

    def test_a_read_outside_the_entry_function_is_refused(self):
        self.assertIsNone(self.rewrite('fs-helper', module(vertex=False, helper=True), vertex=False))

    def test_a_constant_index_past_one_block_is_refused(self):
        # vec4 15 is the block's last; 16 belongs to the next instance in the merged range. A negative constant index
        # is not valid SPIR-V (spirv-val refuses it), so it needs no case.
        found, chains = self.rewrite('vs-k15', module(vertex=True, reads=1, first=15), vertex=True)
        self.check_reads(found, chains, 1)
        self.assertIsNone(self.rewrite('vs-k16', module(vertex=True, reads=1, first=16), vertex=True))
        self.assertIsNone(self.rewrite('fs-k16', module(vertex=False, reads=2, first=16), vertex=False))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', type=Path, required=True)
    parser.add_argument('--spirv-val', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args, rest = parser.parse_known_args()
    raise SystemExit(not unittest.main(argv=[__file__] + rest, exit=False).result.wasSuccessful())
