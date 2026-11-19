"""One loaded NSO module as the generator and its analyses see it: image at a guest base, .text as words,
function seeds, and bounded-cache instruction lifting.

The function seeds and the .eh_frame_hdr reader are adapted from LAS Recomp tools/analyze.py and tools/gen.py
@ da35b83 (MIT, Copyright (c) 2026 Peiikai).
"""
import struct
from collections import OrderedDict

import numpy as np

from lifter import Insn, Lifter, Unsupported, hx
from nso import Nso, R_AARCH64_RELATIVE, R_AARCH64_GLOB_DAT, R_AARCH64_JUMP_SLOT

LIFT_CACHE = 1 << 16  # lifted instructions kept per module; analyses revisit nearby addresses only


def _read_encoded(img, off, enc, pc_base):
    fmt, app = enc & 0x0F, enc & 0x70
    if fmt == 0x0B:  # sdata4
        v, size = struct.unpack_from('<i', img, off)[0], 4
    elif fmt == 0x03:  # udata4
        v, size = struct.unpack_from('<I', img, off)[0], 4
    elif fmt in (0x0C, 0x04):  # sdata8, udata8
        v, size = struct.unpack_from('<q' if fmt == 0x0C else '<Q', img, off)[0], 8
    else:
        raise ValueError(f'unsupported pointer encoding {enc:#x}')
    if app == 0x10:  # pcrel
        v += off
    elif app == 0x30:  # datarel
        v += pc_base
    return v, size


class Module:
    def __init__(self, name, path, base, simd='c'):
        self.name = name
        self.nso = Nso(str(path))
        self.base = base
        n = self.nso
        self.image = bytes(n.image)
        self.lo = base + n.text[0]
        self.hi = self.lo + n.text[1]
        self.words = np.frombuffer(self.image, dtype='<u4', count=n.text[1] // 4, offset=n.text[0])
        self.lifter = Lifter(base, simd)
        self._cache = OrderedDict()

    # ---- memory

    def in_text(self, a):
        return self.lo <= a < self.hi and a % 4 == 0

    def in_image(self, a, n=1):
        return self.base <= a and a + n <= self.base + len(self.image)

    def word(self, a):
        return int(self.words[(a - self.lo) >> 2])

    def read(self, a, n, signed=False):
        """Little-endian integer of n bytes at guest address a (unrelocated image)."""
        o = a - self.base
        return int.from_bytes(self.image[o:o + n], 'little', signed=signed)

    # ---- lifting

    def lift(self, a):
        """Lifted instruction at a; Unsupported becomes an rt_udf trap."""
        ins = self._cache.get(a)
        if ins is not None:
            self._cache.move_to_end(a)
            return ins
        try:
            ins = self.lifter.lift(self.word(a), a)
        except Unsupported as e:
            ins = Insn('trap', f'rt_udf(c, {hx(a)}); /* {e} */')
        self._cache[a] = ins
        if len(self._cache) > LIFT_CACHE:
            self._cache.popitem(last=False)
        return ins

    # ---- function seeds

    def eh_frame_functions(self):
        """Function starts from .eh_frame_hdr's binary search table (absolute addresses)."""
        img, hdr = self.image, self.nso.eh_start
        if not hdr or hdr >= len(img):
            return []
        _, ptr_enc, cnt_enc, tbl_enc = img[hdr:hdr + 4]
        off = hdr + 4
        _, sz = _read_encoded(img, off, ptr_enc, hdr)
        off += sz
        count, sz = _read_encoded(img, off, cnt_enc, hdr)
        off += sz
        starts = []
        for _ in range(count):
            loc, sz = _read_encoded(img, off, tbl_enc, hdr)
            off += sz
            _, sz = _read_encoded(img, off, tbl_enc, hdr)
            off += sz
            starts.append(self.base + loc)
        return starts

    def seeds(self):
        """Known function starts: .eh_frame_hdr, exported FUNC symbols, R_AARCH64_RELATIVE targets in .text."""
        starts = set(self.eh_frame_functions())
        for _, info, shndx, value, _ in self.nso.syms:
            if shndx and (info & 0xF) == 2:
                starts.add(self.base + value)
        for _, typ, _, add in self.nso.relocs:
            if typ == R_AARCH64_RELATIVE:
                starts.add(self.base + add)
        return sorted(a for a in starts if self.in_text(a))

    def import_stubs(self, names):
        """PLT stubs of the imports named in names (absolute addresses): `adrp x16, page; ldr x17, [x16, #off];
        add x16, x16, #off; br x17`, whose GOT slot page + off holds a JUMP_SLOT or GLOB_DAT relocation of one of
        those symbols."""
        n = self.nso
        slots = {self.base + off for off, typ, si, _ in n.relocs
                 if typ in (R_AARCH64_JUMP_SLOT, R_AARCH64_GLOB_DAT) and si and n.syms[si][0] in names}
        w = self.words
        adrp = np.flatnonzero(((w[:-3] & 0x9F00001F) == 0x90000010) & ((w[1:-2] & 0xFFC003FF) == 0xF9400211)
                              & (w[3:] == 0xD61F0220))
        stubs = set()
        for i in adrp.tolist():
            a, v = self.lo + 4 * i, int(w[i])
            imm = ((v >> 29) & 3) | (((v >> 5) & 0x7FFFF) << 2)
            page = (a & ~0xFFF) + ((imm - (1 << 21) if imm & (1 << 20) else imm) << 12)
            if page + ((int(w[i + 1]) >> 10) & 0xFFF) * 8 in slots:
                stubs.add(a)
        return stubs
