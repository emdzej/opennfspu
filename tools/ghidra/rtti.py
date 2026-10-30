#!/usr/bin/env python3
"""RTTI scan of Porsche.exe: classes, vtables and their methods (stdlib only).

    python3 -I tools/ghidra/rtti.py [porsche.exe] [out dir]   -> re/rtti.tsv, re/vtables.tsv

MSVC 6 RTTI: a type descriptor is (vftable pointer, spare, ".?AVName@@"); a complete object locator is
(signature 0, offset, cd offset, type descriptor, class hierarchy); the dword before each vtable points to
its locator. A vtable's methods run until the next vtable or the first value that is not a code address.
rtti.tsv: vtable, mangled name, offset. vtables.tsv: vtable, class (Outer::Inner), offset, methods.
"""
import os
import re
import struct
import sys

BASE = 0x400000
TEXT_END = 0x5b0000  # .text + nothing else holds methods (PACODE is hand-written code)


def sections(d):
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    n = struct.unpack_from('<H', d, pe + 6)[0]
    osz = struct.unpack_from('<H', d, pe + 20)[0]
    return [struct.unpack_from('<8sIIII', d, pe + 24 + osz + i * 40) for i in range(n)]


def demangle(m):
    parts = [p for p in m[4:-2].split('@') if p]
    return '::'.join(reversed(parts))


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else 'porsche.exe'
    out = sys.argv[2] if len(sys.argv) > 2 else 're'
    d = open(exe, 'rb').read()
    secs = sections(d)

    def off2va(o):
        for _n, _vs, va, rs, ra in secs:
            if ra <= o < ra + rs:
                return o - ra + va + BASE
        return None

    def va2off(v):
        for _n, _vs, va, rs, ra in secs:
            if va + BASE <= v < va + BASE + rs:
                return v - va - BASE + ra
        return None

    td = {}
    for m in re.finditer(rb'\.\?A[VU][A-Za-z0-9_@?$]+@@', d):
        va = off2va(m.start())
        if va:
            td[va - 8] = m.group().decode()
    col = {}
    for o in range(0, len(d) - 20, 4):
        sig, offs, _cd, ptd, pchd = struct.unpack_from('<IIIII', d, o)
        if sig == 0 and ptd in td and pchd > BASE:
            col[off2va(o)] = (td[ptd], offs)
    vts = []
    for o in range(0, len(d) - 4, 4):
        v = struct.unpack_from('<I', d, o)[0]
        if v in col:
            vts.append((off2va(o) + 4, col[v][0], col[v][1]))
    vts.sort()
    starts = {v[0] for v in vts}
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, 'rtti.tsv'), 'w') as r, open(os.path.join(out, 'vtables.tsv'), 'w') as w:
        for va, name, offs in vts:
            r.write('%08x\t%s\t%d\n' % (va, name, offs))
            o, methods, i = va2off(va), [], 0
            while i < 400:
                if i and va + 4 * i in starts:
                    break
                f = struct.unpack_from('<I', d, o + 4 * i)[0]
                if not (0x401000 <= f < TEXT_END):
                    break
                methods.append('%x' % f)
                i += 1
            w.write('%08x\t%s\t%d\t%s\n' % (va, demangle(name), offs, ','.join(methods)))
    print('%d type descriptors, %d locators, %d vtables' % (len(td), len(col), len(vts)))


if __name__ == '__main__':
    main()
