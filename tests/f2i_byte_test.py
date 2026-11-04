"""Translate an owned Maxwell fragment shader with 8-bit F2I destinations and execute the emitted conversions.

runtime/third_party/eglantine/shader_recompiler/frontend/ampere/lower_to_maxwell.h (F2I byte mode) lowers Ampere
F2I.S8 and F2I.U8 to a Maxwell F2I with destination size 0, which Maxwell itself does not define; the sign is bit 12.
Bananza packs skinned tangents as SNORM bytes through F2I.S8, and a translator that read size 0 as unsigned stored
every negative component as 0, which turned whole tangents into zero vectors and their normalisation into NaN
(tools/xlate/patches/0017-f2i-byte-destination.patch).

The fixture loads c[0][0] (dynamic, so nothing folds) and converts it four ways, as lower_to_maxwell.h encodes them:
R0 F2I.S8, R1 F2I.U8, R2 F2I.S16 and R3 F2I.S32, all truncating. The test executes the emitted SPIR-V for each input
with a small evaluator of exactly the operations it uses, and compares the four integer results with the
saturating conversions of the Ampere instructions. No game data or GPU is used.

Usage: python tests/f2i_byte_test.py --translator build/xlate/totk_xlate.exe [--spirv-val PATH]
"""
import argparse
import json
import math
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def fragment(instructions):
    header = [0] * 20
    header[0] = 5 << 10   # fragment stage
    header[18] = 15       # render target 0: R0, R1, R2, R3
    words = []
    for index in range(0, len(instructions), 3):
        words.extend([0, *instructions[index:index + 3]])   # Maxwell scheduling words
    return struct.pack('<20I', *header) + struct.pack('<' + 'Q' * len(words), *words) + bytes(256)


def f2i(dest, src, size, signed):
    # As lower_to_maxwell.h emits it: F2I Rd, Rb; destination size bits 8-9, source F32 (2 << 10), sign bit 12,
    # rounding bits 39-40 (3: truncate), FTZ bit 44; predicate PT.
    return (0x5cb0000000000000 | (size << 8) | (2 << 10) | (signed << 12) | (src << 20) | (3 << 39) | (1 << 44) |
            (7 << 16) | dest)


def shader():
    ldc = 0xef90000000000000 | (4 << 48) | (0 << 20) | (255 << 8) | (7 << 16) | 4   # LDC.32 R4, c[0][0]
    return fragment([ldc, f2i(0, 4, 0, 1), f2i(1, 4, 0, 0), f2i(2, 4, 1, 1), f2i(3, 4, 2, 1),
                     0xe30000000007000f])   # EXIT


def module(path):
    data = path.read_bytes()
    words = struct.unpack('<' + 'I' * (len(data) // 4), data)
    assert words[0] == 0x07230203
    out, i = [], 5
    while i < len(words):
        n, op = words[i] >> 16, words[i] & 0xffff
        assert n and i + n <= len(words)
        out.append((op, list(words[i + 1:i + n])))
        i += n
    return out


def f32(u):
    return struct.unpack('<f', struct.pack('<I', u & 0xffffffff))[0]


def u32(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def s32(u):
    return u - (1 << 32) if u & 0x80000000 else u


def evaluate(code, c0):
    """Run the straight-line fragment body on raw 32-bit words; returns the four output words. Booleans are 0 or 1.
    Covers the default translator and --defined-float-minmax, whose patch 0014 emits the unsigned clamp's FMax as
    integer selection."""
    types, consts, ptr_storage, var_storage, values, outputs = {}, {}, {}, {}, {}, {}
    glsl, chains, body = None, {}, False

    def val(x):
        return values[x] if x in values else consts[x]

    for op, a in code:
        if op == 11 and bytes(struct.pack('<%dI' % (len(a) - 1), *a[1:])).rstrip(b'\0') == b'GLSL.std.450':
            glsl = a[0]
        elif op == 21:
            types[a[0]] = ('int', a[2])
        elif op == 22:
            types[a[0]] = ('float', 0)
        elif op == 23:
            types[a[0]] = ('vector', a[2])
        elif op == 32:
            ptr_storage[a[0]] = a[1]
        elif op == 43:
            consts[a[1]] = a[2]
        elif op == 59:
            var_storage[a[1]] = a[2]
        elif op == 54:
            body = True
        elif not body:
            continue
        elif op == 65:   # OpAccessChain: remember the root variable and the constant indices
            chains[a[1]] = (a[2], [consts.get(x) for x in a[3:]])
        elif op == 61:   # OpLoad
            root, _ = chains.get(a[2], (a[2], []))
            sc = var_storage.get(root)
            if sc == 2:      # Uniform: the only one is c[0]; the fixture reads its first word
                values[a[1]] = u32(c0)
            elif sc == 7:    # Function variable
                values[a[1]] = values.get(('var', a[2]), 0)
            else:
                raise AssertionError('load from storage class %s' % sc)
        elif op == 62:   # OpStore
            root, idx = chains.get(a[0], (a[0], []))
            if var_storage.get(root) == 3:
                outputs[idx[-1] if idx else len(outputs)] = values[a[1]]
            else:
                values[('var', a[0])] = values[a[1]]
        elif op == 124:  # OpBitcast keeps the bits
            values[a[1]] = val(a[2])
        elif op in (110, 109):   # OpConvertFToS, OpConvertFToU: the fixture's inputs stay in range here
            x = f32(val(a[2]))
            assert not math.isnan(x), 'conversion reached with a NaN'
            values[a[1]] = int(x) & 0xffffffff
        elif op == 169:  # OpSelect
            values[a[1]] = val(a[3]) if val(a[2]) else val(a[4])
        elif op in (170, 171, 172, 173):   # OpIEqual, OpINotEqual, OpUGreaterThan, OpSGreaterThan
            x, y = val(a[2]), val(a[3])
            values[a[1]] = int(x == y if op == 170 else x != y if op == 171 else x > y if op == 172 else
                               s32(x) > s32(y))
        elif op in (197, 198, 199):   # OpBitwiseOr, OpBitwiseXor, OpBitwiseAnd
            x, y = val(a[2]), val(a[3])
            values[a[1]] = x | y if op == 197 else x ^ y if op == 198 else x & y
        elif op == 200:  # OpNot
            values[a[1]] = ~val(a[2]) & 0xffffffff
        elif op == 12 and a[2] == glsl:
            inst, args = a[3], [val(x) for x in a[4:]]
            if inst == 3:      # Trunc
                values[a[1]] = u32(math.trunc(f32(args[0]))) if math.isfinite(f32(args[0])) else args[0]
            elif inst == 40:   # FMax (no NaN reaches it in these vectors)
                values[a[1]] = u32(max(f32(args[0]), f32(args[1])))
            elif inst == 37:   # FMin
                values[a[1]] = u32(min(f32(args[0]), f32(args[1])))
            elif inst == 45:   # SClamp
                v = min(max(s32(args[0]), s32(args[1])), s32(args[2]))
                values[a[1]] = v & 0xffffffff
            elif inst == 44:   # UClamp
                values[a[1]] = min(max(args[0], args[1]), args[2])
            elif inst == 43:   # FClamp
                values[a[1]] = u32(min(max(f32(args[0]), f32(args[1])), f32(args[2])))
            else:
                raise AssertionError('unsupported GLSL.std.450 instruction %d' % inst)
        elif op in (248, 253, 56, 249):   # OpLabel, OpReturn, OpFunctionEnd, OpBranch
            pass
        else:
            raise AssertionError('unsupported shader-body opcode %d' % op)
    assert set(outputs) == {0, 1, 2, 3}, outputs
    return [outputs[i] for i in range(4)]


def saturate(x, lo, hi):
    return max(lo, min(hi, math.trunc(x)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--translator', required=True, type=Path)
    parser.add_argument('--spirv-val', type=Path)
    args = parser.parse_args()
    out = ROOT / 'build/test/f2i-byte'
    out.mkdir(parents=True, exist_ok=True)
    code = out / 'f2i_byte.bin'
    code.write_bytes(shader())
    jobs = out / 'jobs.json'
    jobs.write_text(json.dumps([{'key': 'f2i_byte', 'stage': 'ps', 'code': str(code.resolve()), 'debug': False}]))
    run = subprocess.run([str(args.translator.resolve()), str(jobs.resolve()), str(out.resolve())],
                         capture_output=True, encoding='utf-8')
    if run.returncode:
        raise AssertionError('translator failed: ' + run.stderr.encode('ascii', 'backslashreplace').decode())
    spirv = out / 'f2i_byte.spv'
    if args.spirv_val:
        subprocess.run([str(args.spirv_val.resolve()), '--target-env', 'vulkan1.2', str(spirv)], check=True)
    code = module(spirv)
    failures, checks = 0, 0
    # Tangent-like values (component x 127, truncated), zero, the ends of each range and beyond them. Values outside
    # the 32-bit range are left out: SPIR-V leaves that conversion undefined, and this patch does not change it.
    for x in (-63.5, -127.0, -1.0, -0.75, 0.0, 0.5, 1.0, 63.5, 127.0, 128.0, 200.0, -129.0, -300.0, 255.0, 256.0,
              40000.0, -40000.0):
        got = evaluate(code, x)
        want = [saturate(x, -128, 127) & 0xffffffff, saturate(x, 0, 255), saturate(x, -32768, 32767) & 0xffffffff,
                saturate(x, -2 ** 31, 2 ** 31 - 1) & 0xffffffff]
        for name, g, w in zip(('S8', 'U8', 'S16', 'S32'), got, want):
            checks += 1
            if g != w:
                failures += 1
                print('FAIL F2I.%s(%r): got %d, want %d' % (name, x, s32(g), s32(w)))
    print('f2i byte: %d checks, %d failures' % (checks, failures))
    return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(main())
