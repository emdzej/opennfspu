"""Reference decoders for Need for Speed: Porsche Unleashed data formats (Python 3, standard library only).

Used to check our understanding of the formats against the data before the C port, and as a reference
to compare the C code with. Format notes live in docs/formats.md; the authority is the original
executable (Porsche.exe, 2,129,965 bytes, CRC-32 fd2f5573). Community notes on EA's formats (BIGF, SHPI,
RefPack, CRP) informed the first guesses; every layout here is checked against the data.

  python3 tools/nfsfmt.py viv    <file.viv> [out dir]      list (and extract) a BIGF / C0FB archive
  python3 tools/nfsfmt.py fsh    <file.fsh> <out dir>      every SHPI image as PNG (RefPack is undone)
  python3 tools/nfsfmt.py sheet  <file.fsh> <out.png>      the images composed at their stored x, y
  python3 tools/nfsfmt.py unref  <in> <out>                undo RefPack (0x10FB) compression
  python3 tools/nfsfmt.py crp    <file.crp> [out.obj]      CRP article/part table (and an OBJ export)
  python3 tools/nfsfmt.py render <file.crp> <out.png> [top|side|front] [name regex]   flat-shaded preview
  python3 tools/nfsfmt.py scan   <game dir>                magic and compression inventory of every file
"""
import os
import re
import struct
import sys
import zlib


# ---------------------------------------------------------------------------------------------- output

def png(path, w, h, px, alpha=False):
    """Writes an 8-bit PNG; px is bytes of w*h*3 (RGB) or w*h*4 (RGBA)."""
    n = 4 if alpha else 3
    raw = b"".join(b"\0" + px[y * w * n:(y + 1) * w * n] for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6 if alpha else 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


# ---------------------------------------------------------------------------------------------- RefPack

def is_refpack(d):
    return len(d) >= 5 and (d[0] & 0x3e) == 0x10 and d[1] == 0xfb


def unrefpack(d):
    """EA's RefPack (QFS) LZ77. Header: flags byte (0x10 | 0x80 = 4-byte sizes | 0x01 = compressed size
    present), 0xfb, then the uncompressed size big-endian (3 or 4 bytes), optionally preceded by the
    compressed size. Four command forms copy 0-3 / 0-112 literals and an (offset, length) match."""
    flags = d[0]
    big = 4 if flags & 0x80 else 3
    p = 2
    if flags & 0x01:
        p += big
    size = int.from_bytes(d[p:p + big], "big")
    p += big
    out = bytearray()
    n = len(d)
    while p < n:
        b0 = d[p]
        if b0 < 0x80:                          # 2 bytes: 0-3 literals, match 3-10, offset up to 1024
            b1 = d[p + 1]
            p += 2
            lit = b0 & 3
            ln = ((b0 >> 2) & 7) + 3
            off = ((b0 & 0x60) << 3) + b1 + 1
        elif b0 < 0xc0:                        # 3 bytes: match 4-67, offset up to 16384
            b1, b2 = d[p + 1], d[p + 2]
            p += 3
            lit = b1 >> 6
            ln = (b0 & 0x3f) + 4
            off = ((b1 & 0x3f) << 8) + b2 + 1
        elif b0 < 0xe0:                        # 4 bytes: match 5-1028, offset up to 131072
            b1, b2, b3 = d[p + 1], d[p + 2], d[p + 3]
            p += 4
            lit = b0 & 3
            ln = ((b0 & 0x0c) << 6) + b3 + 5
            off = ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1
        elif b0 < 0xfc:                        # 1 byte: 4-112 literals, no match
            p += 1
            lit = ((b0 & 0x1f) << 2) + 4
            ln = 0
            off = 0
        else:                                  # end: 0-3 trailing literals
            p += 1
            lit = b0 & 3
            out += d[p:p + lit]
            break
        out += d[p:p + lit]
        p += lit
        if ln:
            s = len(out) - off
            if off >= ln:
                out += out[s:s + ln]
            else:
                for i in range(ln):
                    out.append(out[s + i])
    if len(out) != size:
        raise ValueError("refpack: %d bytes, header says %d" % (len(out), size))
    return bytes(out)


def load(path):
    """File bytes with RefPack undone if present."""
    d = open(path, "rb").read()
    return unrefpack(d) if is_refpack(d) else d


# ---------------------------------------------------------------------------------------------- VIV

def viv_entries(d):
    """[(name, offset, size)] of a BIGF archive (big-endian count, header size, per entry u32 offset,
    u32 size, name NUL) or of the compact C0FB form (u16 header size, u16 count, per entry 24-bit
    offset, 24-bit size, name NUL; all big-endian)."""
    out = []
    if d[:4] in (b"BIGF", b"BIG4"):
        count, _hdr = struct.unpack_from(">II", d, 8)
        p = 16
        for _ in range(count):
            off, size = struct.unpack_from(">II", d, p)
            e = d.index(b"\0", p + 8)
            out.append((d[p + 8:e].decode("latin-1"), off, size))
            p = e + 1
    elif d[:2] == b"\xc0\xfb":
        _hdr, count = struct.unpack_from(">HH", d, 2)
        p = 6
        for _ in range(count):
            off = int.from_bytes(d[p:p + 3], "big")
            size = int.from_bytes(d[p + 3:p + 6], "big")
            e = d.index(b"\0", p + 6)
            out.append((d[p + 6:e].decode("latin-1"), off, size))
            p = e + 1
    else:
        raise ValueError("not a VIV archive: %r" % d[:4])
    return out


def cmd_viv(path, outdir=None):
    d = open(path, "rb").read()
    ents = viv_entries(d)
    for name, off, size in ents:
        print("%08x %9d %-4s %s" % (off, size, d[off:off + 4].hex(), name))
        if outdir:
            os.makedirs(outdir, exist_ok=True)
            with open(os.path.join(outdir, name.replace("/", "_").replace("\\", "_")), "wb") as f:
                f.write(d[off:off + size])
    print(len(ents), "entries")


# ---------------------------------------------------------------------------------------------- FSH

# SHPI image record codes (low byte of the record's first u32; the upper 24 bits are the distance to
# the next record of the same entry, 0 = last). Bit 0x80 on an image code: the pixels are RefPack.
FSH_BPP = {0x7b: 1, 0x7d: 4, 0x7e: 2, 0x78: 2, 0x6d: 2, 0x7f: 3}
FSH_PAL = (0x22, 0x24, 0x29, 0x2a, 0x2d)


def _c565(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2, 255)


def _c1555(v):
    r, g, b = (v >> 10) & 31, (v >> 5) & 31, v & 31
    return (r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2, 255 if v & 0x8000 else 0)


def _c4444(v):
    return (((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17, (v >> 12) * 17)


def fsh_pixel(t, d, o):
    """RGBA of one pixel of a direct-colour format at d[o:]."""
    if t == 0x7d:
        b, g, r, a = d[o:o + 4]
        return (r, g, b, a)
    if t == 0x7f:
        b, g, r = d[o:o + 3]
        return (r, g, b, 255)
    v = d[o] | d[o + 1] << 8
    return {0x78: _c565, 0x7e: _c1555, 0x6d: _c4444}[t](v)


def fsh_palette(t, d, o, n):
    out = []
    for i in range(n):
        if t == 0x2a:
            b, g, r, a = d[o + 4 * i:o + 4 * i + 4]
            out.append((r, g, b, a))
        elif t == 0x24:
            b, g, r = d[o + 3 * i:o + 3 * i + 3]
            out.append((r, g, b, 255))
        elif t == 0x22:                         # 6 bits per channel
            r, g, b = d[o + 3 * i:o + 3 * i + 3]
            out.append((r << 2, g << 2, b << 2, 255))
        else:
            v = d[o + 2 * i] | d[o + 2 * i + 1] << 8
            out.append(_c565(v) if t == 0x29 else _c1555(v))
    return out


class FshRecord:
    __slots__ = ("off", "code", "type", "next", "w", "h", "cx", "cy", "x", "y", "data")


def fsh_entries(d):
    """SHPI: 'SHPI', u32 total size, u32 count, 4-char directory id ('GIMX'), then count x (4-char name,
    u32 offset). Each entry is a chain of records: u32 code, u16 width, u16 height, i16 centre x/y,
    u16 x/y (position in the art's source sheet; for palettes centre x = colour count)."""
    if is_refpack(d):
        d = unrefpack(d)
    assert d[:4] == b"SHPI", d[:4]
    _size, count = struct.unpack_from("<II", d, 4)
    ents = []
    for i in range(count):
        name = d[16 + 8 * i:20 + 8 * i].rstrip(b"\0").decode("latin-1")
        q = struct.unpack_from("<I", d, 20 + 8 * i)[0]
        chain = []
        while True:
            r = FshRecord()
            code, r.w, r.h, r.cx, r.cy, r.x, r.y = struct.unpack_from("<IHHhhHH", d, q)
            r.off, r.code, r.type, r.next = q, code, code & 0xff, code >> 8
            end = q + r.next if r.next else None
            r.data = d[q + 16:end] if end else d[q + 16:]
            chain.append(r)
            if not r.next:
                break
            q += r.next
        ents.append((name, chain))
    return d, ents


def fsh_image(chain, shared_pal=None):
    """(w, h, RGBA bytes) of an entry's first record."""
    r = chain[0]
    t = r.type & 0x7f
    data = unrefpack(r.data) if r.type & 0x80 else r.data
    w, h = r.w, r.h
    out = bytearray(w * h * 4)
    if t == 0x7b:
        pal = None
        for a in chain[1:]:
            if a.type in FSH_PAL:
                pal = fsh_palette(a.type, a.data, 0, a.cx or a.w)
        pal = pal or shared_pal or [(i, i, i, 255) for i in range(256)]
        for i in range(w * h):
            out[4 * i:4 * i + 4] = bytes(pal[data[i]] if data[i] < len(pal) else (255, 0, 255, 255))
    else:
        bpp = FSH_BPP[t]
        for i in range(w * h):
            out[4 * i:4 * i + 4] = bytes(fsh_pixel(t, data, i * bpp))
    return w, h, bytes(out)


def cmd_fsh(path, outdir):
    d, ents = fsh_entries(open(path, "rb").read())
    os.makedirs(outdir, exist_ok=True)
    shared = None
    for name, chain in ents:                    # a lone palette entry ('!pal') serves every 8-bit image
        if chain[0].type in FSH_PAL:
            shared = fsh_palette(chain[0].type, chain[0].data, 0, chain[0].cx or chain[0].w)
    for i, (name, chain) in enumerate(ents):
        r = chain[0]
        desc = " ".join("%02x" % a.type for a in chain)
        print("%3d %-4s %4dx%-4d centre %d,%d at %d,%d  [%s]" % (i, name, r.w, r.h, r.cx, r.cy, r.x, r.y, desc))
        if (r.type & 0x7f) not in FSH_BPP:
            continue
        w, h, px = fsh_image(chain, shared)
        safe = "".join(c if c.isalnum() else "_" for c in name)
        png(os.path.join(outdir, "%03d_%s.png" % (i, safe)), w, h, px, alpha=True)


def cmd_sheet(path, out):
    """Every image of an FSH composed at its stored (x, y): the source sheet the artist cut it from."""
    d, ents = fsh_entries(open(path, "rb").read())
    imgs = []
    for name, chain in ents:
        r = chain[0]
        if (r.type & 0x7f) in FSH_BPP:
            imgs.append((r.x & 0xfff, r.y & 0xfff) + fsh_image(chain))
    W = max(x + w for x, y, w, h, _ in imgs)
    H = max(y + h for x, y, w, h, _ in imgs)
    can = bytearray(W * H * 4)
    for x, y, w, h, px in imgs:
        for j in range(h):
            for i in range(w):
                s = px[(j * w + i) * 4:(j * w + i) * 4 + 4]
                if s[3]:
                    o = ((y + j) * W + x + i) * 4
                    can[o:o + 4] = s[:3] + b"\xff"
    png(out, W, H, bytes(can), alpha=True)
    print(len(imgs), "images on", W, "x", H)


# ---------------------------------------------------------------------------------------------- CRP

# A CRP is RefPack-compressed EA "chunk" data. Every chunk entry is 16 bytes: u32 id, u32 info, u32 count,
# u32 offset (the generic helpers are at 0x59f280..0x59f620 in Porsche.exe).
#   info bit 0: the id's low 16 bits are an index; lookups compare id & 0xffff2020 | 0x2020 ('pr  ')
#   info bit 1: offset is relative to the entry (directories: in 16-byte units; items: in bytes)
#   info bit 3: entries sorted by id (binary search, 0x59f390)
#   directory: info >> 5 = number of child directories, count = number of items after them
#   item:      info >> 8 = data size in bytes, count = element count
# The root ('Car ' / 'Trak') holds articles ('Arti': one object each: parts by LOD and damage state) and
# shared items (materials 'mt', render methods 'RMth', bounding/window/... data).

def tag_name(i):
    """Readable form of an id: 4CCs are stored as little-endian u32s ('Arti' = 0x41727469)."""
    return struct.pack(">I", i).decode("latin-1")


class Chunk:
    __slots__ = ("d", "at", "id", "info", "count", "off")

    def __init__(self, d, at):
        self.d, self.at = d, at
        self.id, self.info, self.count, self.off = struct.unpack_from("<IIII", d, at)

    @property
    def indexed(self):
        return bool(self.info & 1)

    @property
    def index(self):
        return self.id & 0xffff if self.indexed else None

    @property
    def tag(self):
        if self.indexed:
            return struct.pack(">H", self.id >> 16).decode("latin-1")
        return tag_name(self.id)

    def base(self):
        return self.at + self.off * 16 if self.info & 2 else self.off

    def children(self):
        b = self.base()
        return [Chunk(self.d, b + 16 * i) for i in range(self.info >> 5)]

    def items(self):
        b = self.base() + 16 * (self.info >> 5)
        return [Chunk(self.d, b + 16 * i) for i in range(self.count)]

    def data(self):
        o = self.at + self.off if self.info & 2 else self.off
        return o, self.info >> 8

    def find(self, tag, index=None):
        for c in self.items():
            if c.tag == tag and (index is None or c.index == index):
                return c
        return None

    def __repr__(self):
        return "%s%s" % (self.tag, "" if self.index is None else "[%04x]" % self.index)


def crp_load(path):
    d = load(path)
    root = Chunk(d, 0)
    # The root's own entry is the file header: id 'Car '/'Trak', children = articles.
    root.off, root.info = 1, root.info | 2         # header: articles start right after it
    return d, root


def crp_prim(d, pr):
    """One primitive list ('pr'): (material, streams {kind: (part index, list)}, triangles [(v, uv) x3]).
    Layout: u32 type (low byte 3 = triangle list), u32 0x80000000 | material, f32 bounding sphere
    (x, y, z, r), 16 zero bytes, u32 stream count, u32 index-list count, then the streams (16 bytes:
    u32 stream id, u32 0, u16 part size, u16 kind (0 vt, 1 nm, 2 uv, 3 df, '$n' per-triangle), u16 part
    index, u16 index list or 0xffff = element k uses entry k), then the index lists (u32 id 'Iv'/'Iu' +
    index, u32 offset into the u8 index area that follows). Element count = the pr's item count."""
    o, size = pr.data()
    w = struct.unpack_from("<12I", d, o)
    material = w[1] & 0xffff
    nstreams, nlists = w[10], w[11]
    streams = {}
    p = o + 48
    for _ in range(nstreams):
        _sid, _z, _nbytes, kind, idx, lst = struct.unpack_from("<IIHHHH", d, p)
        streams[kind] = (idx, lst)
        p += 16
    offs = []
    for _ in range(nlists):
        _lid, off = struct.unpack_from("<II", d, p)
        offs.append(off)
        p += 8
    n = pr.count

    def elems(kind):
        if kind not in streams:
            return list(range(n))
        lst = streams[kind][1]
        if lst == 0xffff:
            return list(range(n))
        return list(d[p + offs[lst]:p + offs[lst] + n])

    vi, ui = elems(0), elems(2)
    tris = [tuple((vi[k + j], ui[k + j]) for j in range(3)) for k in range(0, n - 2, 3)]
    return material, {k: v[0] for k, v in streams.items()}, tris


def crp_floats(d, part, n):
    o, _ = part.data()
    return [struct.unpack_from("<%df" % n, d, o + i * 4 * n) for i in range(part.count)]


def crp_article_name(d, art):
    nm = art.find("Name")
    if nm is None:
        return ""
    o, n = nm.data()
    return d[o:o + n].split(b"\0")[0].decode("latin-1")


def crp_meshes(path, lod=None, names=None):
    """[(article index, name, vertices, uvs, triangles, material)] for the first damage state, the most
    detailed LOD (lowest pr index) unless lod is given."""
    d, root = crp_load(path)
    out = []
    for ai, art in enumerate(root.children()):
        prs = [c for c in art.items() if c.tag == "pr"]
        name = crp_article_name(d, art)
        if not prs or (names and not re.fullmatch(names, name)):
            continue
        want = min(c.index for c in prs) & 0xf000 if lod is None else lod
        for pr in prs:
            if pr.index & 0xf000 != want or pr.index & 0xfff:
                continue
            mat, streams, tris = crp_prim(d, pr)
            vt = art.find("vt", streams.get(0, 0))
            uv = art.find("uv", streams.get(2, 0))
            if vt is None:
                continue
            verts = crp_floats(d, vt, 4)
            uvs = crp_floats(d, uv, 2) if uv else []
            out.append((ai, name or repr(pr), verts, uvs, tris, mat))
    return out


def cmd_crp(path, obj=None):
    d, root = crp_load(path)
    print(tag_name(root.id), "articles", root.info >> 5, "shared items", root.count, "unpacked", len(d))
    for c in root.items():
        print("  shared", c, "count", c.count, "bytes", c.info >> 8)
    for ai, art in enumerate(root.children()[:8]):
        print("  article", ai, " ".join("%r:%d" % (c, c.count) for c in art.items()))
    if obj:
        meshes = crp_meshes(path)
        with open(obj, "w") as f:
            base = 1
            ubase = 1
            for ai, name, verts, uvs, tris, mat in meshes:
                f.write("o art%d_%s\nusemtl m%d\n" % (ai, name, mat))
                for v in verts:
                    f.write("v %.5f %.5f %.5f\n" % v[:3])
                for t in uvs:
                    f.write("vt %.5f %.5f\n" % (t[0], 1 - t[1]))
                for tri in tris:
                    f.write("f " + " ".join("%d/%d" % (base + v, ubase + u) if uvs else "%d" % (base + v)
                                            for v, u in tri) + "\n")
                base += len(verts)
                ubase += len(uvs)
        print(len(meshes), "primitive lists ->", obj)


def cmd_render(path, out, view="top", names=None, size=768):
    """Flat-shaded preview: orthographic, z-buffered, one colour per article. names: a regex of article
    names to draw (cars carry alternatives: Body/Body1/Body2, cabrio parts, the interior, the driver)."""
    meshes = crp_meshes(path, names=names)
    pts = [v for m in meshes for v in m[2]]
    ax = {"top": (0, 2, 1), "side": (2, 1, 0), "front": (0, 1, 2)}[view]   # x width, y up, z length (m)
    xs = [p[ax[0]] for p in pts]
    ys = [p[ax[1]] for p in pts]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    sc = (size - 8) / max(x1 - x0, y1 - y0, 1e-6)
    W = int((x1 - x0) * sc) + 8
    H = int((y1 - y0) * sc) + 8
    zb = [-1e30] * (W * H)
    img = bytearray(b"\x20\x20\x28" * (W * H))
    for ai, name, verts, uvs, tris, mat in meshes:
        h = (ai * 2654435761 + mat * 40503) & 0xffffff
        base = (96 + (h & 0x7f), 96 + ((h >> 8) & 0x7f), 96 + ((h >> 16) & 0x7f))
        for tri in tris:
            P = [verts[v] for v, _ in tri]
            if max(v for v, _ in tri) >= len(verts):
                continue
            a = [P[1][k] - P[0][k] for k in range(3)]
            b = [P[2][k] - P[0][k] for k in range(3)]
            nrm = (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
            ln = sum(c * c for c in nrm) ** 0.5 or 1
            shade = 0.35 + 0.65 * abs(nrm[1] / ln if view == "top" else nrm[ax[2]] / ln)
            col = bytes(int(c * shade) for c in base)
            S = [((p[ax[0]] - x0) * sc + 4, (y1 - p[ax[1]]) * sc + 4, p[ax[2]] * (1 if view != "top" else 1)) for p in P]
            if view == "top":
                S = [(s[0], (p[ax[1]] - y0) * sc + 4, p[1]) for s, p in zip(S, P)]
            minx, maxx = max(int(min(s[0] for s in S)), 0), min(int(max(s[0] for s in S)) + 1, W - 1)
            miny, maxy = max(int(min(s[1] for s in S)), 0), min(int(max(s[1] for s in S)) + 1, H - 1)
            (ax_, ay, az), (bx, by, bz), (cx, cy, cz) = S
            den = (by - cy) * (ax_ - cx) + (cx - bx) * (ay - cy)
            if abs(den) < 1e-9:
                continue
            for y in range(miny, maxy + 1):
                for x in range(minx, maxx + 1):
                    l1 = ((by - cy) * (x - cx) + (cx - bx) * (y - cy)) / den
                    l2 = ((cy - ay) * (x - cx) + (ax_ - cx) * (y - cy)) / den
                    l3 = 1 - l1 - l2
                    if l1 < -1e-4 or l2 < -1e-4 or l3 < -1e-4:
                        continue
                    z = l1 * az + l2 * bz + l3 * cz
                    i = y * W + x
                    if z > zb[i]:
                        zb[i] = z
                        img[i * 3:i * 3 + 3] = col
    png(out, W, H, bytes(img))
    print(len(meshes), "lists", sum(len(m[4]) for m in meshes), "triangles ->", out, "%dx%d" % (W, H))


def cmd_scan(root):
    """Per extension: file count, total size, leading magics, how many are RefPack-compressed."""
    import collections
    by = collections.defaultdict(list)
    for r, _ds, fs in os.walk(root):
        for f in fs:
            by[f.rsplit(".", 1)[-1].lower() if "." in f else ""].append(os.path.join(r, f))
    for ext, paths in sorted(by.items(), key=lambda kv: -len(kv[1])):
        mags = collections.Counter()
        packed = 0
        total = 0
        for p in paths:
            with open(p, "rb") as fh:
                head = fh.read(4)
            total += os.path.getsize(p)
            packed += is_refpack(head + b"\0")
            mags[head if head.isascii() and head.strip(b"\0").isalnum() else head[:2].hex()] += 1
        print("%-6s %5d files %9d KB  refpack %3d  %s" % (ext, len(paths), total >> 10, packed,
              ", ".join("%r x%d" % kv for kv in mags.most_common(3))))


def cmd_unref(src, dst):
    with open(dst, "wb") as f:
        f.write(unrefpack(open(src, "rb").read()))


COMMANDS = {
    "viv": lambda a: cmd_viv(*a[:2]),
    "fsh": lambda a: cmd_fsh(a[0], a[1]),
    "sheet": lambda a: cmd_sheet(a[0], a[1]),
    "crp": lambda a: cmd_crp(*a[:2]),
    "render": lambda a: cmd_render(a[0], a[1], *(a[2:4] or ["top"])),
    "unref": lambda a: cmd_unref(a[0], a[1]),
    "scan": lambda a: cmd_scan(a[0]),
}

if __name__ == "__main__":
    if len(sys.argv) < 3 or sys.argv[1] not in COMMANDS:
        sys.exit(__doc__)
    COMMANDS[sys.argv[1]](sys.argv[2:])
