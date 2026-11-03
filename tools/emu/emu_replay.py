# Plays a replay file's packets with Porsche.exe's reader (0x4144d0, the replay's view 2) under unicorn,
# in the order the port asked for them, and compares each packet (0x5e9664..0x5e9667) and the assists
# it sets in the car's description (+0x14, +0x18, +0x1c) with the port's log (PK lines: tick, player,
# packet bytes, assists).
# emu_replay.py <exe> <replay.rpl> <port log>
import struct, sys
from unicorn import *
from unicorn.x86_const import *

exe = open(sys.argv[1], 'rb').read()
rpl = open(sys.argv[2], 'rb').read()
want = []
for line in open(sys.argv[3]):
    p = line.split()
    if p and p[0] == 'PK': want.append((int(p[1]), int(p[2]), p[3], p[4]))

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
mu.mem_map(HEAP, 0x100000)
mu.mem_map(STK - 0x100000, 0x100000)
mu.mem_map(RET, 0x1000)
mu.mem_write(RET, b'\xf4' * 0x10)

# the file: the race's block (0x3e24 bytes), the final tick, the replay buffer (0x19000 bytes)
mu.mem_write(0x5ed848, rpl[0x3e28:0x3e28 + 0x19000])
mu.mem_write(0x5e99f4, struct.pack('<i', 2))        # playing
mu.mem_write(0x5e99fc, struct.pack('<i', 0))        # the read offset
# the humans (0x5e8c00): stand-in cars with a description each (+0x558)
for p in range(4):
    desc = HEAP + 0x1000 * (p + 1)
    car = HEAP + 0x10000 + 0x1000 * p
    mu.mem_write(car + 0x558, struct.pack('<I', desc))
    mu.mem_write(0x5e8c00 + 4 * p, struct.pack('<I', car))

def ret0(uc, addr, size, ud):
    sp = uc.reg_read(UC_X86_REG_ESP)
    uc.reg_write(UC_X86_REG_EAX, 0)
    uc.reg_write(UC_X86_REG_EIP, struct.unpack('<I', uc.mem_read(sp, 4))[0])
    uc.reg_write(UC_X86_REG_ESP, sp + 4)
for a in (0x413890, 0x46d8b0):   # the live input (overwritten), the cameras' count (0: no cuts)
    mu.hook_add(UC_HOOK_CODE, ret0, begin=a, end=a)

bad = 0
for n, (tick, p, pk, assists) in enumerate(want):
    sp = STK - 0x1000
    mu.mem_write(sp, struct.pack('<II', RET, p))
    mu.reg_write(UC_X86_REG_ESP, sp)
    mu.emu_start(0x4144d0, RET, count=1_000_000)
    got = bytes(mu.mem_read(0x5e9664, 4)).hex()
    d = HEAP + 0x1000 * (p + 1)
    ga = ''.join(str(struct.unpack('<I', mu.mem_read(d + o, 4))[0]) for o in (0x14, 0x18, 0x1c))
    if got != pk or ga != assists:
        bad += 1
        if bad <= 20: print(f'tick {tick} player {p}: emu {got} {ga} port {pk} {assists}')
print('packets', len(want), 'differing', bad, 'mode', struct.unpack('<i', mu.mem_read(0x5e99f4, 4))[0])
