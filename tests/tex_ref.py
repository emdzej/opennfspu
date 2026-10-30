"""Reference for tests/tex_conv_test.c: per FSH image of the game, the CRC-32 of
  img  the image as ARGB8888 (SHAPE_converttype to 0x7d): EA's rules, i.e. 16-bit pixels (and palette
       entries) of value 0 become 0 (transparent black), 32-bit palettes are used as stored (indices past
       the palette read on in the file), images without a palette are left out;
  tex  for square power-of-two images, the pixels the GL driver gets from DTEX (formats chosen as DTEX's
       scoring does for that driver: 8-bit + palette -> ARGB4444, the others keep their format).
Decoding comes from tools/nfsfmt.py.

  python3 -I tests/tex_ref.py <game dir> > /tmp/tex.ref
  OPENNFSPU_TEXREF=/tmp/tex.ref ./build/tex_conv_test
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import nfsfmt  # noqa: E402

PAL_TYPES = (0x22, 0x23, 0x24, 0x29, 0x2a, 0x2c, 0x2d, 0x2e)


def x5(c):
    return c << 3 | c >> 2


def x6(c):
    return c << 2 | c >> 4


def argb_565(v):
    if v == 0:
        return 0
    return 0xff000000 | x5(v >> 11) << 16 | x6(v >> 5 & 63) << 8 | x5(v & 31)


def argb_1555(v):
    if v == 0:
        return 0
    return (0xff000000 if v & 0x8000 else 0) | x5(v >> 10 & 31) << 16 | x5(v >> 5 & 31) << 8 | x5(v & 31)


def argb_4444(v):
    out = 0
    for c in (3, 2, 1, 0):
        q = v >> (4 * c) & 15
        out = out << 8 | q << 4 | q
    return out


def to4444(argb):
    return (argb >> 28) << 12 | (argb >> 20 & 15) << 8 | (argb >> 12 & 15) << 4 | (argb >> 4 & 15)


def palette(d, rec, chain):
    """256 ARGB entries (None past what EA's palette code fills) of the entry's palette, or None."""
    for r in chain[1:]:
        if r.type in PAL_TYPES:
            n = struct.unpack_from("<h", d, r.off + 4)[0]
            base = r.off + 16
            if r.type == 0x2a:
                return [struct.unpack_from("<I", d, base + 4 * i)[0] if base + 4 * i + 4 <= len(d) else 0
                        for i in range(256)]
            if r.type in (0x29, 0x2d):
                pal = [0] * 256
                for i in range(n):
                    v = struct.unpack_from("<H", d, base + 2 * i)[0]
                    pal[i] = argb_565(v) if r.type == 0x29 else argb_1555(v)
                return pal
            return "unsupported"
    return None


def main(root):
    for dirpath, _, files in sorted(os.walk(root)):
        for f in sorted(files):
            if not f.lower().endswith(".fsh"):
                continue
            path = os.path.join(dirpath, f)
            rel = os.path.relpath(path, root).replace(os.sep, "/")
            raw = open(path, "rb").read()
            if nfsfmt.is_refpack(raw):
                raw = nfsfmt.unrefpack(raw)
            if raw[:4] != b"SHPI":
                continue
            d, ents = nfsfmt.fsh_entries(raw)
            for i, (_, chain) in enumerate(ents):
                r = chain[0]
                t = r.type & 0x7f
                px = nfsfmt.unrefpack(r.data) if r.type & 0x80 else r.data
                w, h = r.w, r.h
                n = w * h
                square = w == h and w and not w & (w - 1)
                img = tex = None
                if t == 0x7b:
                    pal = palette(d, r, chain)
                    if pal in (None, "unsupported"):
                        continue
                    argb = [pal[px[k]] for k in range(n)]
                    img = b"".join(struct.pack("<I", v) for v in argb)
                    tex = b"".join(struct.pack("<H", to4444(v)) for v in argb)
                elif t == 0x7d:
                    img = tex = bytes(px[:4 * n])
                elif t in (0x78, 0x7e, 0x6d):
                    vals = struct.unpack_from("<%dH" % n, px)
                    f32 = {0x78: argb_565, 0x7e: argb_1555, 0x6d: argb_4444}[t]
                    img = b"".join(struct.pack("<I", f32(v)) for v in vals)
                    tex = bytes(px[:2 * n])
                elif t == 0x7f:
                    img = b"".join(struct.pack("<I", 0xff000000 | px[3 * k + 2] << 16 | px[3 * k + 1] << 8 | px[3 * k])
                                   for k in range(n))
                    tex = None   # 888 isn't a driver format; DTEX picks 8888 or 565 by score: not checked here
                else:
                    continue
                print("img\t%s\t%d\t%d\t%d\t%08x" % (rel, i, w, h, zlib.crc32(img)))
                if tex is not None and square and not r.type & 0x80:   # packed: never a texture
                    print("tex\t%s\t%d\t%d\t%d\t%08x" % (rel, i, w, h, zlib.crc32(tex)))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "game")
