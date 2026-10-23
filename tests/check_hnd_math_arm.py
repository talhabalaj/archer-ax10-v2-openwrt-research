#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare original HND fixed-point helpers with the pinned hnd.ko instructions.

Executes the actual contiguous helper region (including its unnamed local
64-bit shift routines) under Unicorn 2.1.4. Internal calls and the referenced
bit-length table are resolved from the module's own relocations. The only
external calls, __aeabi_uidiv/__aeabi_uidivmod, are kernel compiler-runtime
routines bound to their ABI-defined unsigned semantics and counted; any other
relocation or call fails. No hardware, payload output or table printing.
"""
import argparse
import ctypes as ct
import hashlib
import json
import os
from pathlib import Path
import random
import shlex
import struct
import subprocess
import tempfile

HND_SHA256 = 'd05ff061b3a636630955a13fdd20a953f3dfb07ee92904a987dd4c77b648b45a'
FIRST, LAST = 'math_fp_calc_head_room_32', 'math_fp_ceil_64'
REGION_START = 0x28a9c  # local 64-bit shift routines precede the named helpers
RUNTIME = ('__aeabi_uidiv', '__aeabi_uidivmod')
CODE, DATA, TRAP, STACK = 0x100000, 0x400000, 0x600000, 0x810000
STOP = TRAP + 0x100
RESULT = STACK - 0x400


def require(ok, message):
    if not ok:
        raise ValueError(message)


def parse(data):
    require(hashlib.sha256(data).hexdigest() == HND_SHA256, 'Unknown HND module')
    h = struct.unpack_from('<HHIIIIIHHHHHH', data, 16)
    sections = [struct.unpack_from('<10I', data, h[5] + i * 40) for i in range(h[11])]
    symbols = []
    for s in sections:
        if s[1] != 2:
            continue
        st = sections[s[6]]
        for p in range(0, s[5], 16):
            n, v, z, info, _, sh = struct.unpack_from('<IIIBBH', data, s[4] + p)
            end = data.find(b'\0', st[4] + n)
            symbols.append((data[st[4] + n:end].decode('ascii'), v, z, info, sh))
    return sections, symbols


def region(data):
    """Return code bytes, functions and resolved patches for the helper region."""
    sections, symbols = parse(data)
    named = {s[0]: s for s in symbols if s[3] & 15 == 2 and s[4]}
    owner = named[FIRST][4]
    require(named[LAST][4] == owner, 'Helpers span sections')
    end = named[LAST][1] + named[LAST][2]
    text = sections[owner]
    code = bytearray(data[text[4] + REGION_START:text[4] + end])
    functions = {n: CODE + v - REGION_START for n, (_, v, z, i, sh) in named.items()
                 if sh == owner and REGION_START <= v < end}
    tables, calls = {}, {}
    for rel in sections:
        if rel[1] != 9 or rel[7] != owner:
            continue
        for p in range(0, rel[5], 8):
            at, info = struct.unpack_from('<II', data, rel[4] + p)
            if not REGION_START <= at < end:
                continue
            sym, kind, off = symbols[info >> 8], info & 255, at - REGION_START
            if kind in (28, 29):
                if sym[0] in RUNTIME:
                    calls[off] = TRAP + 4 * RUNTIME.index(sym[0])
                else:
                    require(sym[0] in functions, 'Call leaves the helper region: ' + sym[0])
                    calls[off] = functions[sym[0]]
            elif kind in (43, 44):
                tables.setdefault(sym, []).append((off, kind))
            else:
                raise ValueError('Unreviewed relocation kind %d' % kind)
    return code, functions, calls, tables, sections


def patched(data):
    code, functions, calls, tables, sections = region(data)
    for off, target in calls.items():
        word = struct.unpack_from('<I', code, off)[0]
        delta = (target - (CODE + off) - 8) >> 2
        struct.pack_into('<I', code, off, (word & 0xff000000) | (delta & 0xffffff))
    require(len(tables) == 1, 'Expected one referenced table')
    (sym, sites), = tables.items()
    table = sections[sym[4]]
    blob = data[table[4] + sym[1]:table[4] + sym[1] + 256]
    for off, kind in sites:
        word = struct.unpack_from('<I', code, off)[0]
        require(((word >> 4) & 0xf000) | (word & 0xfff) == 0, 'Unexpected REL addend')
        imm = DATA & 0xffff if kind == 43 else DATA >> 16
        struct.pack_into('<I', code, off, (word & 0xfff0f000) | ((imm >> 12) << 16) | (imm & 0xfff))
    return bytes(code), functions, blob


class Machine:
    def __init__(self, code, blob):
        import unicorn as uc
        from unicorn import arm_const as a
        self.uc, self.a = uc, a
        self.code, self.blob = code, blob
        self.instructions = self.runtime_calls = 0

    def call(self, address, registers, stack=(), out=False):
        uc, a = self.uc, self.a
        cpu = uc.Uc(uc.UC_ARCH_ARM, uc.UC_MODE_ARM)
        for base, size in ((CODE, 0x100000), (DATA, 0x1000), (TRAP, 0x1000), (STACK - 0x10000, 0x10000)):
            cpu.mem_map(base, size)
        cpu.mem_write(CODE, self.code)
        cpu.mem_write(DATA, self.blob)
        sp = STACK - 0x100
        cpu.mem_write(sp, b''.join(struct.pack('<I', v & 0xffffffff) for v in stack) + b'\0' * 64)
        result = RESULT
        cpu.mem_write(result, struct.pack('<I', 0xa5a5a5a5))
        regs = list(registers) + [0] * (4 - len(registers))
        if out:
            stack_list = list(stack) + [result]
            cpu.mem_write(sp, b''.join(struct.pack('<I', v & 0xffffffff) for v in stack_list) + b'\0' * 64)
        for reg, value in zip((a.UC_ARM_REG_R0, a.UC_ARM_REG_R1, a.UC_ARM_REG_R2, a.UC_ARM_REG_R3), regs):
            cpu.reg_write(reg, value & 0xffffffff)
        cpu.reg_write(a.UC_ARM_REG_SP, sp)
        cpu.reg_write(a.UC_ARM_REG_LR, STOP)

        def hook(engine, pc, size, _):
            if TRAP <= pc < TRAP + 8:
                n, d = engine.reg_read(a.UC_ARM_REG_R0), engine.reg_read(a.UC_ARM_REG_R1)
                require(d != 0, 'Runtime divide by zero reached')
                engine.reg_write(a.UC_ARM_REG_R0, n // d)
                if pc == TRAP + 4:
                    engine.reg_write(a.UC_ARM_REG_R1, n % d)
                engine.reg_write(a.UC_ARM_REG_PC, engine.reg_read(a.UC_ARM_REG_LR))
                self.runtime_calls += 1
                return
            require(CODE <= pc < CODE + len(self.code), 'Execution left the helper region')
            self.instructions += 1
        cpu.hook_add(uc.UC_HOOK_CODE, hook)
        cpu.mem_write(TRAP, struct.pack('<II', 0xe1a00000, 0xe1a00000))
        cpu.emu_start(address, STOP, count=200000)
        require(cpu.reg_read(a.UC_ARM_REG_PC) == STOP, 'No actual return')
        r0, r1 = cpu.reg_read(a.UC_ARM_REG_R0), cpu.reg_read(a.UC_ARM_REG_R1)
        stored = struct.unpack('<I', cpu.mem_read(result, 4))[0]
        return r0, r1, stored, result


def library(root, folder):
    out = folder / 'libaxd_hnd_math.so'
    subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + ['-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
                   '-shared', '-fPIC', str(root / 'driver/hnd_math_fp.c'), '-o', str(out)], check=True)
    lib = ct.CDLL(str(out))
    u8, u32, u64 = ct.c_uint8, ct.c_uint32, ct.c_uint64
    for name, res, args in (('head_room_32', u8, [u32]), ('head_room_64', u8, [u64]),
                            ('mult_64', u64, [u64, u64, u8, u8, u8]),
                            ('div_64', u8, [u64, u32, u8, u8, ct.POINTER(u32)]),
                            ('round_32', u32, [u32, u32]), ('floor_32', u32, [u32, u32]),
                            ('round_64', u32, [u64, u32]), ('ceil_64', u64, [u64, u32])):
        f = getattr(lib, 'axd_hnd_fp_' + name)
        f.restype, f.argtypes = res, args
    lib.axd_hnd_uint64_divide.restype = None
    lib.axd_hnd_uint64_divide.argtypes = [ct.POINTER(u32), u32, u32, u32]
    return lib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('hnd', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    code, functions, blob = patched(args.hnd.read_bytes())
    m = Machine(code, blob)
    rng = random.Random(0x20704)
    edge32 = [0, 1, 2, 3, 0x7f, 0x80, 0xff, 0x100, 0xffff, 0x10000, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff]
    words = edge32 + [rng.getrandbits(32) for _ in range(40)] + [rng.getrandbits(rng.randrange(1, 33)) for _ in range(40)]
    wides = [w | (rng.choice(words) << 32) for w in words[:30]] + [rng.getrandbits(rng.randrange(1, 65)) for _ in range(60)]
    shifts = list(range(0, 70)) + [127, 128, 255, 256, 257, 300]
    qs = [0, 1, 0x10, 0x13, 0x15, 0x18, 0x1f, 0x20, 0x3f, 0x40, 0xff]
    cases = 0
    f = functions

    def same(label, arm, c):
        require(arm == c, '%s mismatch: arm %#x c %#x' % (label, arm, c))

    for w in words:
        same('head_room_32', m.call(f['math_fp_calc_head_room_32'], [w])[0] & 0xff, m_lib.axd_hnd_fp_head_room_32(w)); cases += 1
    for v in wides + words:
        same('head_room_64', m.call(f['math_fp_calc_head_room_64'], [v & 0xffffffff, v >> 32])[0] & 0xff,
             m_lib.axd_hnd_fp_head_room_64(v)); cases += 1
    for v in words:
        for s in shifts:
            same('round_32', m.call(f['math_fp_round_32'], [v, s])[0], m_lib.axd_hnd_fp_round_32(v, s))
            same('floor_32', m.call(f['math_fp_floor_32'], [v, s])[0], m_lib.axd_hnd_fp_floor_32(v, s))
            cases += 2
    for v in wides:
        for s in shifts:
            same('round_64', m.call(f['math_fp_round_64'], [v & 0xffffffff, v >> 32, s])[0], m_lib.axd_hnd_fp_round_64(v, s))
            r0, r1, _, _ = m.call(f['math_fp_ceil_64'], [v & 0xffffffff, v >> 32, s])
            same('ceil_64', r0 | (r1 << 32), m_lib.axd_hnd_fp_ceil_64(v, s))
            cases += 2
    for _ in range(3000):
        a, b = rng.choice(wides), rng.choice(wides)
        qa, qb, qo = rng.choice(qs), rng.choice(qs), rng.choice(qs)
        r0, r1, _, _ = m.call(f['math_fp_mult_64'], [a & 0xffffffff, a >> 32, b & 0xffffffff, b >> 32], [qa, qb, qo])
        same('mult_64', r0 | (r1 << 32), m_lib.axd_hnd_fp_mult_64(a, b, qa, qb, qo)); cases += 1
    for _ in range(3000):
        n, d = rng.choice(wides), rng.choice(words)
        hi, lo = n >> 32, n & 0xffffffff
        q = ct.c_uint32(0xa5a5a5a5)
        m_lib.axd_hnd_uint64_divide(ct.byref(q), hi, lo, d)
        same('uint64_divide', m.call(f['math_uint64_divide'], [RESULT, hi, lo, d])[2], q.value)
        qn, qd = rng.choice(qs), rng.choice(qs)
        q = ct.c_uint32(0xa5a5a5a5)
        c_format = m_lib.axd_hnd_fp_div_64(n, d, qn, qd, ct.byref(q))
        r0, _, stored, _ = m.call(f['math_fp_div_64'], [lo, hi, d, qn], [qd], out=True)
        same('div_64 format', r0 & 0xff, c_format); same('div_64 quotient', stored, q.value); cases += 2
    print(json.dumps({'hnd_sha256': HND_SHA256, 'cases': cases, 'actual_arm_instructions': m.instructions,
                      'runtime_divide_calls': m.runtime_calls, 'region_functions': sorted(n for n in f if n.startswith('math_')),
                      'hardware_operations': 0}, sort_keys=True))


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='axd-hnd-math-') as folder:
        m_lib = library(Path(__file__).resolve().parents[1], Path(folder))
        main()
