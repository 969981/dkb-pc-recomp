"""recomp/module.py Module.import_stubs on a synthetic image: two PLT stubs, one whose GOT slot holds setjmp.

Usage: import_stubs_test.py
"""
import sys
from pathlib import Path
from types import SimpleNamespace

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'recomp'))
from module import Module  # noqa: E402
from nso import R_AARCH64_GLOB_DAT, R_AARCH64_JUMP_SLOT  # noqa: E402


def stub(a, slot):
    """adrp x16, page(slot); ldr x17, [x16, #lo(slot)]; add x16, x16, #lo(slot); br x17, placed at a."""
    imm = ((slot & ~0xFFF) - (a & ~0xFFF)) >> 12 & 0x1FFFFF
    lo = slot & 0xFFF
    return [0x90000010 | (imm & 3) << 29 | (imm >> 2) << 5, 0xF9400211 | (lo // 8) << 10,
            0x91000210 | lo << 10, 0xD61F0220]


def main():
    base, lo = 0x8000000, 0x8001000
    words = [0xD503201F] * 4 + stub(lo + 0x10, base + 0x5008) + stub(lo + 0x20, base + 0x5010) + [0xD503201F] * 4
    m = object.__new__(Module)
    m.base, m.lo, m.words = base, lo, np.array(words, dtype='<u4')
    m.nso = SimpleNamespace(syms=[('', 0, 0, 0, 0), ('setjmp', 0, 0, 0, 0), ('memcpy', 0, 0, 0, 0)],
                            relocs=[(0x5008, R_AARCH64_JUMP_SLOT, 1, 0), (0x5010, R_AARCH64_GLOB_DAT, 2, 0)])
    assert m.import_stubs({'setjmp'}) == {lo + 0x10}, m.import_stubs({'setjmp'})
    assert m.import_stubs({'setjmp', 'memcpy'}) == {lo + 0x10, lo + 0x20}
    assert m.import_stubs({'longjmp'}) == set()
    print('import_stubs: ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
