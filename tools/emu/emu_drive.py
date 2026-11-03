# Runs Porsche.exe's 0x49ef50 (the car's drive step) under unicorn on a state dumped by the port
# (--param drivedump=<tick>) and compares the car afterwards with the port's.
# emu_drive.py <exe> <dump> [fpucw hex]
import struct, sys
from unicorn import *
from unicorn.x86_const import *

exe = open(sys.argv[1], 'rb').read()
dump = {}
for line in open(sys.argv[2]):
    p = line.split(' ', 3)
    if p[0] != 'DD': continue
    if p[1] in ('glob', 'postglob', 'self', 'mid'):
        dump[p[1]] = line.strip()
        continue
    dump[(p[1], int(p[2]))] = bytes.fromhex(p[3].strip())
cw = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x027f

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

HEAP, STK, RET = 0x20000000, 0x10000000, 0x0dead000
mu.mem_map(HEAP, 0x400000)
mu.mem_map(STK - 0x100000, 0x100000)
mu.mem_map(RET, 0x1000)
mu.mem_write(RET, b'\xf4')
at = HEAP
def alloc(data):
    global at
    a = at
    mu.mem_write(a, data)
    at = (at + len(data) + 0xff) & ~0xff
    return a

def kv(s):
    out = {}
    for t in s.split()[2:]:
        if '=' in t:
            k, v = t.split('=', 1)
            out[k] = v
    return out
g = kv(dump['glob'])
self_i = int(dump['self'].split()[2])

desc = alloc(dump[('desc', 0)])
specs = alloc(dump[('specs', 0)])
parts = [alloc(dump.get(('part', i), bytes(0x200))) for i in range(20)]
def car_image(b):
    b = bytearray(b)
    struct.pack_into('<I', b, 0x558, desc)
    struct.pack_into('<I', b, 0x760, specs)
    for i in range(20): struct.pack_into('<I', b, 0x768 + 4 * i, parts[i])
    return bytes(b)
nr = int(g['nracers'])
racers = []
car = None
for i in range(nr):
    img = car_image(dump[('precar', 0)] if i == self_i else dump[('racer', i)])
    a = alloc(img)
    racers.append(a)
    if i == self_i: car = a
def w32(a, v): mu.mem_write(a, struct.pack('<i', v))
def wu32(a, v): mu.mem_write(a, struct.pack('<I', v))
idx = struct.unpack_from('<i', dump[('precar', 0)], 0x520)[0]
mu.mem_write(0x65808c + 0x580 * idx, dump[('desc', 0)])
print('desc index', idx)
w32(0x5e8ae8, nr)
for i, a in enumerate(racers): wu32(0x5e8b34 + 4 * i, a)
w32(0x606ac4, int(g['tick']))
w32(0x6573f8, int(g['rally']))
w32(0x657404, int(g['players']))
w32(0x6573e8, int(g['mode']))
w32(0x5e67b0, int(g['demo']))
w32(0x606ae0, int(g['altgrip']))
w32(0x657408, 0)
r = [int(x) for x in g['rand'].split(',')]
wu32(0x5d1028, r[0]); wu32(0x5d102c, r[1]); wu32(0x655a0c, r[2])

calls = []
def hook_code(uc, addr, size, ud):
    if addr == 0x473290:
        calls.append('FindAbove')
        sp = uc.reg_read(UC_X86_REG_ESP)
        ret = struct.unpack('<I', uc.mem_read(sp, 4))[0]
        uc.reg_write(UC_X86_REG_EAX, 0)
        uc.reg_write(UC_X86_REG_ESP, sp + 4)
        uc.reg_write(UC_X86_REG_EIP, ret)
mu.hook_add(UC_HOOK_CODE, hook_code, begin=0x473290, end=0x473290)
def hook_mid(uc, addr, size, ud):
    vals = [struct.unpack('<f', uc.mem_read(car + o, 4))[0] for o in (0x860, 0x924, 0x9e8, 0xaac)]
    print('emu mid split', [v.hex() for v in vals], 'T', struct.unpack('<f', uc.mem_read(uc.reg_read(UC_X86_REG_EBP) - 4, 4))[0].hex())
mu.hook_add(UC_HOOK_CODE, hook_mid, begin=0x49f66f, end=0x49f66f)
def hook_bad(uc, access, addr, size, value, ud):
    print('bad access', hex(addr), 'at', hex(uc.reg_read(UC_X86_REG_EIP)))
    return False
mu.hook_add(UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED | UC_HOOK_MEM_FETCH_UNMAPPED, hook_bad)

# the FPU control word: a small stub (fldcw [esp]) run first
stub = 0x0dead100
mu.mem_write(stub, b'\xd9\x2c\x24\xc3')   # fldcw [esp]; ret
sp = STK - 0x1000
mu.mem_write(sp, struct.pack('<IHH', RET, cw, 0))
mu.reg_write(UC_X86_REG_ESP, sp - 4)
mu.mem_write(sp - 4, struct.pack('<I', RET - 0) )
# run fldcw: push a return to RET-? simpler: execute the stub with a return into RET
mu.mem_write(sp - 8, struct.pack('<IH', RET, cw))
mu.reg_write(UC_X86_REG_ESP, sp - 8)
mu.mem_write(stub, b'\xd9\x6c\x24\x04\xc3')   # fldcw [esp+4]; ret
mu.emu_start(stub, RET, count=100)

sp = STK - 0x2000
mu.mem_write(sp, struct.pack('<II', RET, car))
mu.reg_write(UC_X86_REG_ESP, sp)
mu.emu_start(0x49ef50, RET, count=20_000_000)
if mu.reg_read(UC_X86_REG_EIP) != RET:
    print('did not return', hex(mu.reg_read(UC_X86_REG_EIP))); sys.exit(2)

emu = bytes(mu.mem_read(car, len(dump[('postcar', 0)])))
port = dump[('postcar', 0)]
pre = dump[('precar', 0)]
skip = set(range(0x558, 0x55c)) | set(range(0x760, 0x764)) | set(range(0x768, 0x7b8))
diffs = []
for o in range(0, len(port) - 3, 4):
    if o in skip: continue
    a, b = emu[o:o + 4], port[o:o + 4]
    if a != b:
        fa, fb = struct.unpack('<f', a)[0], struct.unpack('<f', b)[0]
        ia, ib = struct.unpack('<i', a)[0], struct.unpack('<i', b)[0]
        changed = pre[o:o + 4] != a or pre[o:o + 4] != b
        diffs.append((o, fa, fb, ia, ib, changed))
print('calls', calls, 'cw', hex(cw))
print('differing words', len(diffs))
for o, fa, fb, ia, ib, ch in diffs[:80]:
    rel = abs(fa - fb) / max(abs(fa), abs(fb), 1e-30) if fa == fa and fb == fb else float('nan')
    print(f'+0x{o:04x} emu {fa:.9g} port {fb:.9g} (int {ia} {ib}) rel {rel:.2e}')
gl = {a: struct.unpack('<f', mu.mem_read(a, 4))[0] for a in (0x628fb0, 0x628fac, 0x628fb4, 0x628fa0, 0x628fa4, 0x628fa8)}
print('emu globals thr', gl[0x628fb0], 'brk', gl[0x628fac], 'steer', gl[0x628fb4], 'grav', gl[0x628fa0], gl[0x628fa4], gl[0x628fa8])
print('port', dump['postglob'])
print('rand emu', struct.unpack('<I', mu.mem_read(0x5d1028, 4))[0], struct.unpack('<I', mu.mem_read(0x655a0c, 4))[0])
