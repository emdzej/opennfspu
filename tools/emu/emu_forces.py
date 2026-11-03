# Runs Porsche.exe's 0x49bcb0 (the car's ground forces) under unicorn on a state dumped by the port
# (--param forcesdump=<tick>, forcesdump.patch) and compares the car afterwards with the port's. The world
# tree's calls are answered from the port's record: 0x473fc0 (a tracker's update) writes the tracker the
# port had after it, its polygon a stand-in object whose normal (+0x14) and vertex (+0x18) methods give the
# port's; 0x473290 (the roof lookup) returns the port's answer.
# Other car tasks dumped the same way (--param fdfn=<hex>) run as well: the dump names the function.
# emu_forces.py <exe> <dump> [fpucw hex]
import struct, sys
from unicorn import *
from unicorn.x86_const import *

exe = open(sys.argv[1], 'rb').read()
cw = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x007f
pre = post = desc = specs = None
fn = 0x49bcb0
parts, elems, upd, above, glob, racers, edges = {}, {}, [], [], {}, {}, []
self_i = -1
collided, nobjects, postracers = None, 0, {}
nodes_rec, nroad, rnodes = [], 0, {}
ranked = None
objraw, okinds = {}, []
import os
road_all = {}
aispecs = None
def lines():
    if os.environ.get('FD_ROAD') and os.path.exists(os.environ['FD_ROAD']):
        yield from open(os.environ['FD_ROAD'])
    yield from open(sys.argv[2])
for line in lines():
    q = line.split()
    if len(q) > 2 and q[0] == 'FD' and q[1].startswith('road') and q[1] not in ('road',):
        road_all[(q[1], q[2])] = q[3:] if q[1] == 'roadhdr' else bytes.fromhex(q[3]) if len(q) > 3 else b''
        continue
    if len(q) > 3 and q[0] == 'FD' and q[1] == 'aispecs':
        aispecs = bytes.fromhex(q[3])
        continue
for line in open(sys.argv[2]):
    p = line.split()
    if not p or p[0] != 'FD': continue
    tag, key = p[1], p[2]
    if (tag.startswith('road') and tag != 'road') or tag in ('aispecs', 'ocoll'): continue
    if tag in ('glob', 'postglob'):
        glob[tag] = dict(t.split('=', 1) for t in p[3:] if '=' in t)
        continue
    if tag == 'fn':
        fn = int(p[3], 16)
        continue
    if tag == 'collided':
        collided = [int(x) for x in p[3:]]
        continue
    if tag == 'objects':
        nobjects = int(p[3])
        continue
    if tag == 'objraw':
        objraw[int(key)] = (int(p[3]), bytes.fromhex(p[4]))
        continue
    if tag == 'okind':
        okinds.append(int(p[3]))
        continue
    if tag == 'ranked':
        ranked = p[3:]
        continue
    if tag == 'road':
        nroad = int(p[3])
        continue
    if tag == 'self':
        self_i = int(key)
        continue
    if tag == 'above':
        above.append(int(p[3]))
        continue
    data = bytes.fromhex(p[3]) if len(p) > 3 else b''
    if tag == 'pre': pre = data
    elif tag == 'post': post = data
    elif tag == 'desc': desc = data
    elif tag == 'specs': specs = data
    elif tag == 'part': parts[int(key)] = data
    elif tag == 'elem': elems[int(key, 16)] = data
    elif tag == 'upd': upd.append((int(key), data))
    elif tag == 'racer': racers[int(key)] = data
    elif tag == 'node': nodes_rec.append(data)
    elif tag == 'rnode': rnodes[int(key)] = data
    elif tag == 'postracer': postracers[int(key)] = data
    elif tag == 'edge': edges.append(data)

pe = struct.unpack_from('<I', exe, 0x3c)[0]
nsec = struct.unpack_from('<H', exe, pe + 6)[0]
optsz = struct.unpack_from('<H', exe, pe + 20)[0]
base = struct.unpack_from('<I', exe, pe + 24 + 28)[0]
imgsz = struct.unpack_from('<I', exe, pe + 24 + 56)[0]
mu = Uc(UC_ARCH_X86, UC_MODE_32)
mu.mem_map(base, (imgsz + 0xfff) & ~0xfff)
mu.mem_write(base, exe[:0x1000])
for i in range(nsec):
    o = pe + 24 + optsz + 40 * i
    vsz, va, rsz, roff = struct.unpack_from('<IIII', exe, o + 8)
    mu.mem_write(base + va, exe[roff:roff + min(rsz, vsz if vsz else rsz)])
HEAP, STK, RET, STUB = 0x20000000, 0x10000000, 0x0dead000, 0x0dead100
mu.mem_map(HEAP, 0x1000000)
mu.mem_map(STK - 0x100000, 0x100000)
mu.mem_map(RET, 0x1000)
mu.mem_write(RET, b'\xf4' * 0x200)
at = HEAP
def alloc(data):
    global at
    a = at
    mu.mem_write(a, data)
    at = (at + len(data) + 0xff) & ~0xff
    return a
def w32(a, v): mu.mem_write(a, struct.pack('<i', v))
def wu32(a, v): mu.mem_write(a, struct.pack('<I', v))

# the polygons' stand-ins: an object [vtable], the vtable's slots 5 (normal) and 6 (vertex) at the stubs
STUB_N, STUB_V = STUB, STUB + 0x10
vt = alloc(struct.pack('<11I', *([RET] * 5 + [STUB_N, STUB_V] + [RET] * 4)))
fake, info = {}, {}
for addr, d in elems.items():
    nv = struct.unpack_from('<i', d, 0)[0]
    n = d[4:16]
    verts = alloc(d[16:16 + 12 * nv] + bytes(12))
    obj = alloc(struct.pack('<II', vt, 0))
    fake[addr] = obj
    info[obj] = (n, verts, nv)
def tracker(b):
    b = bytearray(b)
    e = struct.unpack_from('<I', b, 0x28)[0]
    struct.pack_into('<I', b, 0x28, fake.get(e, 0) if e else 0)
    struct.pack_into('<I', b, 0x2c, 0)
    return bytes(b)
TRACK = [0x7bc + 0xc4 * i for i in range(4)] + [0x8]

desc_a = alloc(desc)
specs_a = alloc(specs)
parts_a = [alloc(parts.get(i, bytes(0x200))) for i in range(20)]
img = bytearray(pre)
struct.pack_into('<I', img, 0x558, desc_a)
struct.pack_into('<I', img, 0x760, specs_a)
for i in range(20): struct.pack_into('<I', img, 0x768 + 4 * i, parts_a[i])
for o in TRACK: img[o:o + 0x30] = tracker(img[o:o + 0x30])
if aispecs: struct.pack_into('<I', img, 0x764, alloc(aispecs))
struct.pack_into('<I', img, 0x74c, alloc(bytes(0x800)))   # the renderer: a blank stand-in (hit effects only)
car = alloc(bytes(img))
idx = struct.unpack_from('<i', pre, 0x520)[0]
mu.mem_write(0x65808c + 0x580 * idx, desc)
g = glob['glob']
w32(0x606ac4, int(g['tick'])); w32(0x6573f8, int(g['rally'])); w32(0x657408, int(g['network']))
w32(0x657a2c, int(g['damage'])); w32(0x657400, int(g['derby'])); w32(0x6573e8, int(g['mode']))
for k, va in (('players', 0x657404), ('demo', 0x5e67b0), ('altgrip', 0x606ae0), ('started', 0x606ac0), ('laps', 0x6573f0), ('rtype', 0x6573ec), ('linear', 0x657494)):
    if k in g: w32(va, int(g[k]))
# the racers (the draft reads their positions), the car itself among them
w32(0x5e8ae8, len(racers))
for i in sorted(racers):
    if i == self_i: a = car
    else:
        b = bytearray(racers[i])
        struct.pack_into('<I', b, 0x558, desc_a)
        struct.pack_into('<I', b, 0x760, specs_a)
        for j in range(20): struct.pack_into('<I', b, 0x768 + 4 * j, parts_a[j])
        a = alloc(bytes(b))
    wu32(0x5e8b34 + 4 * i, a)
racer_a = {}
for i in sorted(racers): racer_a[i] = struct.unpack('<I', mu.mem_read(0x5e8b34 + 4 * i, 4))[0]
if ranked is not None:
    # the track's road width (0x5e6e34), reversed (0x657430), the cars in the race's order (0x5e8b88)
    wu32(0x5e6e34, int(ranked[0], 16))
    w32(0x657430, int(ranked[1]))
    order = [int(x) for x in ranked[2:]]
    if any(k < 0 for k in order): print('a ranked car not among the racers: unsupported')
    for j, k in enumerate(order): wu32(0x5e8b88 + 4 * j, racer_a.get(k, 0))
    w32(0x5e8b58, len(order))   # (the race's cars' count bounds the order)
if collided is not None:
    # the cars checked this tick before this one (0x628cdc, count 0x628f5c)
    if any(k < 0 for k in collided): print('collided with a car not among the racers (traffic): unsupported')
    w32(0x628f5c, len(collided))
    for j, k in enumerate(collided): wu32(0x628cdc + 4 * j, racer_a.get(k, 0))
def vec(data, n=None):
    b = alloc(data) if data else 0
    return alloc(struct.pack('<III', b, b + len(data), b + len(data)))
if ('roadhdr', '0') in road_all:
    # the whole road in the original's layout (0x47fbd0): the three vectors behind pointers
    nn, length, last, nseg, nj = (int(x) for x in road_all[('roadhdr', '0')])
    nodes_a = alloc(road_all[('roadnodes', '0')])
    nn_a = alloc(road_all.get(('roadnn', '0'), bytes(nn)))
    ne_a = alloc(road_all.get(('roadne', '0'), bytes(nn)))
    segs = bytearray(road_all[('roadseg', '0')])
    juncs = bytearray(road_all[('roadjunc', '0')])
    seg_a = alloc(bytes(len(segs)))
    jn_a = alloc(bytes(len(juncs)) or bytes(4))
    for i in range(nseg):
        for o in (0x20, 0x24):
            v = struct.unpack_from('<I', segs, 0x28 * i + o)[0]
            struct.pack_into('<I', segs, 0x28 * i + o, jn_a + 0x38 * (v - 1) if v else 0)
    for i in range(nj):
        for k in range(8):
            v = struct.unpack_from('<I', juncs, 0x38 * i + 4 * k)[0]
            struct.pack_into('<I', juncs, 0x38 * i + 4 * k, seg_a + v - 1 if v else 0)
    mu.mem_write(seg_a, bytes(segs))
    mu.mem_write(jn_a, bytes(juncs))
    segvec = alloc(struct.pack('<III', seg_a, seg_a + len(segs), seg_a + len(segs)))
    jnvec = alloc(struct.pack('<III', jn_a, jn_a + len(juncs), jn_a + len(juncs)))
    inner = [vec(road_all.get(('roadnl', str(i)), b'')) for i in range(nj)]
    nl = alloc(struct.pack('<%dI' % len(inner), *inner)) if inner else 0
    nearvec = alloc(struct.pack('<III', nl, nl + 4 * len(inner), nl + 4 * len(inner)))
    road = alloc(struct.pack('<iiiIIIIII', nn, length, last, nodes_a, nn_a, ne_a, segvec, jnvec, nearvec))
    wu32(0x628bc0, road)
    if ('roadline', '0') in road_all:
        # the AI's racing line (0x5e671c: +0x4c the .lsp file, +0x58 its points from +4)
        ln = alloc(road_all[('roadline', '0')])
        wu32(0x5e671c, alloc(bytes(0x4c) + struct.pack('<IIII', ln, 0, 0, ln + 4) + bytes(0x40)))
    nroad = 0
if nroad:
    # the road (0x628bc0: +0 the node count, +0xc the nodes, 0x50 bytes each): the nodes the car was on
    arr = alloc(bytes(nroad * 0x50))
    for k, d in rnodes.items(): mu.mem_write(arr + 0x50 * k, d)
    road = alloc(struct.pack('<iiiI', nroad, 0, 0, arr) + bytes(0x40))
    wu32(0x628bc0, road)
r = [int(x) for x in g['rand'].split(',')]
wu32(0x5d1028, r[0]); wu32(0x5d102c, r[1]); wu32(0x655a0c, r[2])

log = []
last_edge = [None]
def ret(uc, n, eax=None):
    sp = uc.reg_read(UC_X86_REG_ESP)
    ra = struct.unpack('<I', uc.mem_read(sp, 4))[0]
    if eax is not None: uc.reg_write(UC_X86_REG_EAX, eax)
    uc.reg_write(UC_X86_REG_ESP, sp + 4 + n)
    uc.reg_write(UC_X86_REG_EIP, ra)
def hook(uc, addr, size, ud):
    sp = uc.reg_read(UC_X86_REG_ESP)
    this = uc.reg_read(UC_X86_REG_ECX)
    if addr == 0x473fc0:
        off = this - car
        want = TRACK.index(off) if off in TRACK else -1
        if not upd: log.append('update with none recorded'); ret(uc, 8); return
        k, data = upd.pop(0)
        if k != want: log.append(f'update order: emu tracker {want}, port {k}')
        uc.mem_write(this, tracker(data))
        log.append(f'update {want}')
        ret(uc, 8)
    elif addr == 0x4743a0:
        # the tracker's road node search (thiscall: the point; ret 4): the port's tracker after it
        if nodes_rec: uc.mem_write(this, tracker(nodes_rec.pop(0)))
        else: log.append('node search with none recorded')
        log.append('node')
        ret(uc, 4)
    elif addr == 0x473290:
        v = above.pop(0) if above else 0
        log.append(f'above {v}')
        ret(uc, 8, v)
    elif addr == 0x474030:
        # the wall edge query (thiscall: segment, out edge, out flag; ret 0xc): the port asks once where
        # the original's Edges asks again after a miss (0x495380), with the same answer
        ra = struct.unpack('<I', uc.mem_read(sp, 4))[0]
        if ra == 0x495385 and last_edge[0] is not None: d = last_edge[0]
        elif edges: d = edges.pop(0)
        else: log.append('edge with none recorded'); d = bytes(24) + struct.pack('<I', 0)
        last_edge[0] = d
        out, fl = struct.unpack('<II', uc.mem_read(sp + 8, 8))
        f = struct.unpack_from('<I', d, 24)[0]
        uc.mem_write(out, d[:24])
        uc.mem_write(fl, struct.pack('<H', f & 0xffff))
        log.append('edge %d' % (f >> 16))
        ret(uc, 0xc, (uc.reg_read(UC_X86_REG_EAX) & ~0xff) | (f >> 16))
    elif addr == 0x4737d0:
        # the world's objects near the car (thiscall: point, radius; ret 8): the port's, in the original's
        # layout with copies of the original's vtables (cylinder 0x5b490c, box 0x5b4938), the hit kind
        # (slot 8, which asks the bundle) answered from the port's record
        if not objraw:
            if nobjects: log.append('objects unsupported')
            ret(uc, 8, 0)
            return
        ptrs = []
        for i in sorted(objraw):
            shape, raw = objraw[i]
            src = 0x5b490c if shape == 1 else 0x5b4938
            vt = bytearray(uc.mem_read(src - 4, 0x34))
            struct.pack_into('<I', vt, 4 + 0x20, STUB_OK)
            vta = alloc(bytes(vt)) + 4
            b = bytearray(raw)
            struct.pack_into('<I', b, 0, vta)
            a = alloc(bytes(b))
            obj_shape[a] = shape
            ptrs.append(a)
        arr = alloc(struct.pack('<%dI' % len(ptrs), *ptrs))
        log.append('objects %d' % len(ptrs))
        ret(uc, 8, alloc(struct.pack('<III', arr, arr + 4 * len(ptrs), arr + 4 * len(ptrs))))
    elif addr == STUB_OK:
        # an object's hit kind (thiscall: the velocity; ret 4)
        k = okinds.pop(0) if okinds else 0
        log.append('kind %d' % k)
        ret(uc, 4, k)
    elif addr == 0x5a10fb:
        # the RTTI cast (cdecl: object, 0, from, to, 0): the stand-in's shape
        o, _, _, to = struct.unpack('<IIII', uc.mem_read(sp + 4, 16))
        want = {0x5d0590: 1, 0x5d05b8: 2}.get(to)
        ret(uc, 0, o if obj_shape.get(o) == want else 0)
    elif addr in (0x59ea80, 0x59ee00, 0x532020, 0x532030):
        ret(uc, 0)   # the vector's frees and the small-block pool's lock
    elif addr == 0x43f3f0:
        # a hit's sparks (thiscall on the renderer's effects; ret 8): drawing only
        log.append('sparks')
        ret(uc, 8)
    elif addr == 0x5a520c:
        # the runtime's per-thread data (errno for a maths function's domain error): a scratch block
        ret(uc, 0, TLS)
    elif addr == STUB_N:
        n, verts, nv = info[this]
        out = struct.unpack('<I', uc.mem_read(sp + 4, 4))[0]
        uc.mem_write(out, n)
        ret(uc, 4)
    elif addr == STUB_V:
        n, verts, nv = info[this]
        i = struct.unpack('<i', uc.mem_read(sp + 4, 4))[0]
        ret(uc, 4, verts + 12 * (i % nv if i >= nv else i))   # (past the last wraps round, 0x485f80, 0x485a60)
TLS = alloc(bytes(0x100))
STUB_OK = STUB + 0x20
obj_shape = {}
wu32(0x5e4fe8, alloc(bytes(0x400)))   # the small-block pool the vector goes back to
for a in (0x473fc0, 0x473290, 0x474030, 0x4737d0, 0x4743a0, 0x5a520c, 0x43f3f0, 0x5a10fb, 0x59ea80, 0x59ee00, 0x532020, 0x532030, STUB_OK, STUB_N, STUB_V): mu.hook_add(UC_HOOK_CODE, hook, begin=a, end=a)
def bad(uc, access, addr, size, value, ud):
    print('bad access', hex(addr), 'at', hex(uc.reg_read(UC_X86_REG_EIP)))
    return False
mu.hook_add(UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED | UC_HOOK_MEM_FETCH_UNMAPPED, bad)

stub = 0x0dead800
mu.mem_write(stub, b'\xd9\x6c\x24\x04\xc3')   # fldcw [esp+4]; ret
sp = STK - 0x1000
mu.mem_write(sp, struct.pack('<IH', RET, cw))
mu.reg_write(UC_X86_REG_ESP, sp)
mu.emu_start(stub, RET, count=100)
sp = STK - 0x2000
mu.mem_write(sp, struct.pack('<II', RET, car))
mu.reg_write(UC_X86_REG_ESP, sp)
mu.emu_start(fn, RET, count=50_000_000)
if mu.reg_read(UC_X86_REG_EIP) != RET:
    print('did not return', hex(mu.reg_read(UC_X86_REG_EIP))); sys.exit(2)

emu = bytes(mu.mem_read(car, len(post)))
want = bytearray(post)
for o in TRACK: want[o:o + 0x30] = tracker(want[o:o + 0x30])
skip = set(range(0x558, 0x55c)) | set(range(0x74c, 0x750)) | set(range(0x760, 0x768)) | set(range(0x768, 0x7b8))
for o in TRACK: skip |= set(range(o + 0x28, o + 0x30))
def same(o, a, b):
    # +0x434, the car hit: the original's pointer, the port's index plus one (read only against 0)
    if o == 0x434: return (a == bytes(4)) == (b == bytes(4))
    return a == b
diffs = []
for o in range(0, len(want) - 3, 4):
    if o in skip: continue
    a, b = emu[o:o + 4], bytes(want[o:o + 4])
    if not same(o, a, b):
        diffs.append((o, struct.unpack('<f', a)[0], struct.unpack('<f', b)[0], struct.unpack('<i', a)[0], struct.unpack('<i', b)[0]))
print('calls', log, 'left', len(upd), len(above), len(edges))
changed = sum(1 for o in range(0, len(want) - 3, 4) if o not in skip and pre[o:o + 4] != bytes(want[o:o + 4]))
print('port changed', changed, 'words')
print('differing words', len(diffs))
for o, fa, fb, ia, ib in diffs[:60]:
    print(f'+0x{o:04x} emu {fa:.9g} port {fb:.9g} (int {ia} {ib})')
for i, d in sorted(postracers.items()):
    if i == self_i or i not in racer_a: continue
    e = bytes(mu.mem_read(racer_a[i], len(d)))
    n = sum(1 for o in range(0, len(d) - 3, 4) if o not in skip and not same(o, e[o:o + 4], d[o:o + 4]))
    if n:
        print('racer', i, 'differing words', n)
        for o in range(0, len(d) - 3, 4):
            if o not in skip and not same(o, e[o:o + 4], d[o:o + 4]):
                print(f'  racer {i} +0x{o:04x} emu {struct.unpack_from("<f", e, o)[0]:.9g} port {struct.unpack_from("<f", d, o)[0]:.9g}')
print('rand emu', struct.unpack('<I', mu.mem_read(0x5d1028, 4))[0], 'port', glob['postglob']['rand'])
