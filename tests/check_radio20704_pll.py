#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Double-entry check of the radio20704 PLL value computation.

A second, separately written Python transcription (including its own HND
fixed-point helpers) is compared with driver/radio20704_pll.c over a grid of
frequencies, references, shifts and modes. Both derive from the same recovered
instruction order, so this catches transcription and wiring slips, not shared
misreadings; the HND helpers themselves are separately matched to the pinned
instructions by check_hnd_math_arm.py. No private input or hardware.
"""
import ctypes as ct
import itertools
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

M32, M64 = (1 << 32) - 1, (1 << 64) - 1


def lsl32(v, n):
    n &= 0xff
    return 0 if n >= 32 else (v << n) & M32


def lsr32(v, n):
    n &= 0xff
    return 0 if n >= 32 else v >> n


def shl64(v, n):
    if n > 31:
        return lsl32(v & M32, (n - 32) & 0xff) << 32
    return (v << n) & M64


def shr64(v, n):
    if n > 31:
        return lsr32(v >> 32, (n - 32) & 0xff)
    return v >> n


def nbits(x):
    return x.bit_length()


def head32(x):
    return (32 - nbits(x)) & 0xff


def head64(x):
    hi = x >> 32
    return ((32 if hi else 64) - nbits(hi if hi else x & M32)) & 0xff


def mult(a, b, qa, qb, qo):
    s = (qa + qb - qo) & 0xff
    half = (s - 1) & 0xff
    r = shl64(1, half) if half else 1
    p = (a * b) & M64
    return p if s == 0 else shr64((r + p) & M64, s)


def udiv(q, hi, lo, d):
    if d <= 1:
        return q
    acc = 0
    if hi:
        step, carry = M32 // d, (M32 % d + 1) % d
        while hi:
            acc = (acc + hi * step) & M32
            r = carry * hi + lo
            hi, lo = r >> 32, r & M32
    return (lo // d + acc) & M32


def div(n, d, qn, qd):
    room = (head64(n) - 1 - head32(d)) & 0xff
    s = min((qd - qn + 31) & 0xff, room)
    if s:
        n = shl64(n, s)
    return (s + (qn - qd)) & 0xff, udiv(0, n >> 32, n & M32, d)


def round32(v, n):
    return lsr32(((lsl32(1, n - 1) if n else 0) + v) & M32, n)


def round64(v, n):
    half = (n - 1) & 0xff
    r = shl64(1, half) if half else 1
    return v & M32 if n == 0 else shr64((r + v) & M64, n) & M32


def ceil64(v, n):
    return v if n == 0 else shr64((shl64(1, n) + (v - 1)) & M64, n)


def s32(v):
    v &= M32
    return v - (1 << 32) if v >> 31 else v


def clamp(diff, d, limit):
    if diff < 1:
        return 0
    f, q = div(diff & M64, d, 16, 16)
    v = s32(round32(q, f))
    return 0 if v < 0 else min(v, limit)


def scale(f):
    k = (f - 0x97b) & M32
    a = ((0x40100021 >> k) & 1) ^ 1 if k < 0x1f else 1
    b = 0 if (f & ~0x20) == 0x994 else int(f not in (0x9a3, 0x99e))
    if not (a & b):
        return 0xaaaaaaaa
    if f < 0xbb9:
        return 0xc0000000
    if f in (0x143c, 0x1351):
        return 0x60000000
    return 0x55555555 if f < 0x1892 else 0x40000000


def model(freq, ref, shift, mode, before):
    v = list(before)
    put = lambda off, x: v.__setitem__(off // 2, x & 0xffff)
    prod = mult(freq, scale(freq), 0, 31, 19)
    if mode > 2:
        return 1, v
    refsh = lsl32(ref, shift)
    if refsh <= 1 or prod & M32 <= 1:
        return -1, v
    f, q = div(prod & M32, refsh, 19, 24); x44 = round32(q, (f - 21) & 0xff)
    f, q = div(freq, prod & M32, 0, 19); x3c = lsr32(q, (f - 16) & 0xff)
    lo1 = mult((x3c << 1) & M32, ref, 16, 24, 16) & M32
    if lo1 <= 1:
        return -1, v
    c16 = ceil64(lo1, 16) & 0xffff; put(0x16, c16)
    f, q = div(c16, lo1, 0, 16)
    uv5 = round64(((round32(q, (f - 16) & 0xff) - 0x10000) << 13) & M32, 16) & 0xffff; put(0x1a, uv5)
    p = prod & M32
    for limit, hi, lo in ((0x63ffffff, 0xde4, 0xce4), (0x6a3fffff, 0xbf0, 0xaf0), (0x707fffff, 0x89e, 0x79e),
                          (0x76bfffff, 0x614, 0x514), (0x7cffffff, 0x3ee, 0x2ee), (M32, 0x290, 0x190)):
        if p <= limit:
            put(0x1e, lo); put(0x20, hi); break
    if freq <= 0xbb8:
        base = 0x98a; put(0x1c, 3); put(0x22, 0xffd9); put(0x24, 0x27); put(0x82, 0x19)
    else:
        for top, b in ((0x1482, 0x13ce), (0x157c, 0x14fa), (6000, 0x1644), (0x1964, 0x17d4), (7000, 0x19c8),
                       (0x1d4c, 0x1bbc), (8000, 0x1db0), (1 << 20, 0x1fa4)):
            if freq < top:
                base = b; break
        put(0x1c, 5 if freq < 0x157c else 8); put(0x22, 0xffce); put(0x24, 0x32); put(0x82, 0x28)
    put(0x28, 1); put(0x38, base + round64((base * uv5 * 8) & M32, 16)); put(0x46, freq)
    consts = {2: (1, 1, 1, 0, 0, 0, 0), 0: (0, 1, 0, 0, 0, 1, 0xfff), 1: (0, 0, 1, 1, 0xfff, 0, 0)}[mode]
    v74, u4, u21, u14, u10, u22, u18 = consts
    for off, x in ((0x74, v74), (0x7a, 3), (0x6e, 0), (0x70, u4), (0x72, u21), (0x48, u14), (0x4a, u10),
                   (0x86, u22), (0x88, u18), (0x4c, x44 >> 16), (0x4e, x44), (0x50, 9), (0x52, 10),
                   (0x54, 1), (0x56, 3), (0x58, 5)):
        put(off, x)
    A = mult(0xa1fd8938, 700, 30, 0, 20); alo = A & M32
    C = (mult(0x225247cb, 9, 32, 0, 26) + mult(0x45ec80c7, 1, 32, 0, 26)) & M32
    D = mult(0x2f4b6b, prod, 32, 19, 26) & M32
    G = mult(mult(D, mult(D, D, 26, 26, 26) & M32, 26, 26, 26) & M32, C, 26, 26, 26) & M32
    def dr(n, d, qn, qd):
        if d <= 1:
            raise ZeroDivisionError
        f, q = div(n, d, qn, qd); return round32(q, (f - 16) & 0xff)
    try:
        l40 = dr(x44, G, 21, 26)
        ic = mult(mult(l40, 700, 16, 0, 16) & M32, 0x9d3e9d, 16, 32, 16) & M32
        ic = min(ic, 0x28000)
        K = mult(dr((alo << 1) & M32, ic, 20, 16), l40, 16, 16, 16) & M32
        M = mult(l40, mult(alo, alo, 20, 20, 8) & M32, 16, 8, 8) & M32
        l38 = dr(mult(ic, 1000000000, 16, 0, 0) & M32, M, 0, 8)
        l34 = dr(l38, 9, 16, 0)
    except ZeroDivisionError:
        return -1, v
    lv = mult(K, 0x12000, 16, 16, 16)
    sx = lambda x: x - (1 << 64) if x >> 63 else x
    put(0x5a, clamp(K - 0x1012e14, 0x11935c3, 255)); put(0x5c, v[0x5a // 2])
    put(0x5e, clamp(sx((lv << 3) & M64) - 0x1012e14, 0x11935c3, 255))
    put(0x60, clamp(sx(lv) - 0x1012e14, 0x11935c3, 255))
    put(0x62, clamp(l34 - 0x3430c, 0x810d, 255))
    put(0x64, clamp(dr(l38, 0x14, 16, 0) - 0x7d1f9, 0x37b3, 255))
    l2c = dr(l38, 0x28, 16, 0)
    put(0x66, clamp(l2c - 0x3e34d, 0x1c50, 255)); put(0x68, clamp((l2c >> 1) - 0x1c3d0, 0xe2d, 255))
    x = s32(round32((mult(0x1cfd2f, mult(ic, ic, 16, 16, 16) & M32, 16, 16, 16) - 0xd26b1 +
                     mult(0x6477333, ic, 16, 16, 16)) & M32, 22))
    put(0x6c, 0 if x < 0 else min(x, 127))
    x = s32(round32(mult(mult(0x10000, refsh, 16, 24, 16) & M32, 0x147ae, 16, 16, 16) & M32, 16))
    put(0x6a, 0 if x < 0 else min(x, 255))
    return 0, v


def main():
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='axd-pll-') as folder:
        lib_path = Path(folder) / 'libpll.so'
        subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + ['-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
                       '-shared', '-fPIC', str(root / 'driver/radio20704_pll.c'), str(root / 'driver/hnd_math_fp.c'),
                       '-o', str(lib_path)], check=True)
        lib = ct.CDLL(str(lib_path))

        class Config(ct.Structure):
            _fields_ = [('reference', ct.c_uint32), ('shift', ct.c_uint8), ('mode', ct.c_uint8)]
        lib.axd_radio20704_pll_compute.restype = ct.c_int
        lib.axd_radio20704_pll_compute.argtypes = [ct.c_uint16, ct.POINTER(Config), ct.POINTER(ct.c_uint16 * 73)]
        freqs = list(range(2400, 2500, 1)) + list(range(4900, 5925, 5)) + list(range(5925, 7200, 20)) + \
            [0x97b, 0x994, 0x9b4, 0x99e, 0x9a3, 0x143c, 0x1351, 0x1891, 0x1892, 6000, 6100, 6500, 7000, 7500, 8000]
        # Crystal-scale references (Hz, with shift) reach every field; small and
        # degenerate ones exercise the refusal paths.
        refs = [(54000000, 0), (50000000, 0), (40000000, 0), (26000000, 1), (25000000, 1), (27000000, 1),
                (19200000, 1), (0x9c4, 12), (1, 0), (40000, 6), (7, 40)]
        cases = mismatches = full = 0
        for f, (ref, sh), mode in itertools.product(freqs, refs, range(4)):
            before = [(0x1111 * (k + 3)) & 0xffff for k in range(73)]
            arr = (ct.c_uint16 * 73)(*before)
            rc = lib.axd_radio20704_pll_compute(f, ct.byref(Config(ref, sh, mode)), ct.byref(arr))
            prc, pv = model(f, ref, sh, mode, before)
            cases += 1
            if rc != prc or list(arr) != pv:
                mismatches += 1
                if mismatches <= 5:
                    diff = [(hex(k * 2), arr[k], pv[k]) for k in range(73) if arr[k] != pv[k]][:6]
                    print('mismatch f=%d ref=%d shift=%d mode=%d rc=%d/%d %s' % (f, ref, sh, mode, rc, prc, diff))
            full += rc == 0
        print('radio20704 PLL double-entry: %d cases (%d full computations), %d mismatches' % (cases, full, mismatches))
        return 1 if mismatches or full < cases // 3 else 0


if __name__ == '__main__':
    sys.exit(main())
