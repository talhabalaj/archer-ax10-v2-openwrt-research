#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare pure wake-policy C decisions/load traces to pinned actual ARM code.

Requires Unicorn 2.1.4 and the caller-supplied pinned private ELF. Reuses the
primitive PHY harness's ELF layout/bounds parser; resolves only four actual
selected REL ARM CALL/JUMP24 sites with checked symbol bindings/addend -8.
No external stubs, MMIO, kernel/device execution or vendor payload export.

Primary references:
https://github.com/unicorn-engine/unicorn/releases/tag/2.1.4
https://github.com/ARM-software/abi-aa/blob/main/aaelf32/aaelf32.rst
Behavior provenance: docs/DRIVER_WAKE_POLICY_AUDIT.md.
"""
import argparse
import copy
import ctypes as ct
import hashlib
import importlib.util
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

CODE, RAM, STOP = 0x1000000, 0x2000000, 0x1800000
STACK = RAM + 0x10000
W, PUB, HW, ARRAY = [RAM + i * 0x1000 for i in range(4)]
BSS, BASSOC = RAM + 0x4000, RAM + 0x6000
OPT, NEST, INNER, RRM, RRM_INNER, ASSOC, ASSOC_INNER = [RAM + 0x8000 + i * 0x100 for i in range(7)]
NAMES = {"wlc_stay_awake", "wlc_rrm_inprog", "wlc_rrm_in_progress",
         "wlc_assoc_get_as_cfg", "wlc_hw_get_wake_override"}
ENTRY = "wlc_stay_awake"
FIELDS = ["NONE", "W19C", "W19B", "WB4", "B4_C", "W2FC", "2FC_4", "2FC_4_C",
          "PUBLIC", "PUBLIC58", "W530", "RRM14", "RRM_C", "W22C", "ASSOC4", "ASSOC_WORD",
          "W199", "W19A", "W198", "W300", "W336", "W568", "W188", "W18", "OVERRIDE",
          "W1A0", "W1E0", "BSS_POINTER", "BSS6", "BSS4", "BSS8", "PRIMARY", "BSS10",
          "BSS98", "BSS_STATE8"]
F = {name: i for i, name in enumerate(FIELDS)}


def require(ok, text):
    if not ok:
        raise ValueError(text)


class U8(ct.Structure):
    _fields_ = [("known", ct.c_uint8), ("value", ct.c_uint8)]


class U32(ct.Structure):
    _fields_ = [("value", ct.c_uint32), ("known", ct.c_uint8)]


class Bss(ct.Structure):
    _fields_ = [(n, U32) for n in ("pointer", "assoc98", "state8")] + [
        (n, U8) for n in ("byte4", "byte6", "byte8", "byte10")]


class Inputs(ct.Structure):
    _fields_ = [(n, U8) for n in ("w19c", "w19b", "b4_c", "public58", "w199", "w19a",
                                "w198", "w300", "w336")] + [
        (n, U32) for n in ("wb4", "w2fc", "object2fc_4", "object2fc_4_c", "public_pointer",
                          "w530", "rrm14", "rrm_c", "w22c", "assoc4", "assoc_word", "w568",
                          "w188", "w18", "w1a0", "w1e0", "primary")] + [("bss", Bss * 16)]


class Access(ct.Structure):
    _fields_ = [("field", ct.c_int), ("slot", ct.c_uint8), ("pass_", ct.c_uint8)]


class Result(ct.Structure):
    _fields_ = [("decision", ct.c_int), ("reason", ct.c_int), ("slot", ct.c_uint8),
               ("pass_", ct.c_uint8), ("accesses", ct.c_uint), ("trace", Access * 256)]


def case():
    s = Inputs()
    for name, typ in Inputs._fields_:
        if typ in (U8, U32):
            getattr(s, name).known = 1
    for b in s.bss:
        for name, _ in Bss._fields_:
            getattr(b, name).known = 1
    for name, value in [("object2fc_4", INNER), ("public_pointer", PUB), ("w530", RRM),
                        ("rrm14", RRM_INNER), ("w22c", ASSOC), ("assoc4", ASSOC_INNER),
                        ("w18", HW), ("w1e0", ARRAY)]:
        getattr(s, name).value = value
    return s, U32(0, 1)


def clone(c):
    s, override = c
    return Inputs.from_buffer_copy(s), U32.from_buffer_copy(override)


def selected(data, repo):
    spec = importlib.util.spec_from_file_location("base_phy_arm", repo / "tests/check_phy_arm.py")
    base = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(base)
    require(hashlib.sha256(data).hexdigest() == base.MODULE_SHA256, "Unrecognized input")
    blobs = base.functions(data, NAMES)
    h = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
    sections = [struct.unpack_from("<10I", data, h[5] + i * 40) for i in range(h[11])]

    def region(offset, size):
        require(0 <= offset <= len(data) and 0 <= size <= len(data) - offset, "Invalid ELF bounds")
        return data[offset:offset + size]

    symbols, found = {}, {}
    for index, section in enumerate(sections):
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        strings = region(strings[4], strings[5])
        table = []
        for pos in range(0, section[5], 16):
            n, value, size, info, _, owner = struct.unpack("<IIIBBH", region(section[4] + pos, 16))
            require(n < len(strings) and strings.find(b"\0", n) >= 0, "Invalid ELF string")
            name = strings[n:strings.find(b"\0", n)].decode("ascii")
            table.append((name, value, size, info, owner))
            if name in NAMES:
                require(name not in found, "Duplicate function")
                found[name] = (owner, value, size)
        symbols[index] = table
    result = {}
    for index, name in enumerate(sorted(NAMES)):
        owner, value, size = found[name]
        code, expected_sites = blobs[name]
        records = {}
        for section in sections:
            if section[1] not in (4, 9) or section[7] != owner:
                continue
            stride = 12 if section[1] == 4 else 8
            require(section[9] == stride and section[5] % stride == 0, "Invalid relocations")
            require(section[6] in symbols, "Invalid relocation symbol table")
            for pos in range(0, section[5], stride):
                at, info = struct.unpack_from("<II", region(section[4] + pos, stride))
                if value <= at < value + size:
                    require(at % 4 == 0 and at + 4 <= value + size, "Invalid relocation alignment")
                    require(info >> 8 < len(symbols[section[6]]), "Invalid relocation symbol")
                    require(at - value not in records, "Duplicate relocation")
                    records[at - value] = (section[1], info & 255, symbols[section[6]][info >> 8])
        require(set(records) == expected_sites, "Parser relocation disagreement")
        result[name] = {"address": CODE + index * 0x10000, "code": code,
                        "symbol": found[name], "relocations": records}
    return result


def graph(s, override):
    fields = {}

    def add(address, item, name, size=4):
        if not RAM <= address < STACK - 0x1000:
            return  # Known null/invalid pointers are not mapped to fake objects.
        v = (size, item.value, item.known, F[name])
        if address in fields:
            require(fields[address] == v, "Inconsistent repeated pointer identity")
        fields[address] = v

    for name, offset, label in [("w19c", 0x19c, "W19C"), ("w19b", 0x19b, "W19B"),
                                ("w199", 0x199, "W199"), ("w19a", 0x19a, "W19A"),
                                ("w198", 0x198, "W198"), ("w300", 0x300, "W300"),
                                ("w336", 0x336, "W336")]:
        add(W + offset, getattr(s, name), label, 1)
    for name, offset, label in [("wb4", 0xb4, "WB4"), ("w2fc", 0x2fc, "W2FC"),
                                ("public_pointer", 0, "PUBLIC"), ("w530", 0x530, "W530"),
                                ("w22c", 0x22c, "W22C"), ("w568", 0x568, "W568"),
                                ("w188", 0x188, "W188"), ("w18", 0x18, "W18"),
                                ("w1a0", 0x1a0, "W1A0"), ("w1e0", 0x1e0, "W1E0"),
                                ("primary", 0x1e4, "PRIMARY")]:
        add(W + offset, getattr(s, name), label)
    if s.wb4.value:
        add(s.wb4.value + 0xc, s.b4_c, "B4_C", 1)
    if s.w2fc.value:
        add(s.w2fc.value + 4, s.object2fc_4, "2FC_4")
    if s.object2fc_4.value:
        add(s.object2fc_4.value + 0xc, s.object2fc_4_c, "2FC_4_C")
    if s.public_pointer.value:
        add(s.public_pointer.value + 0x58, s.public58, "PUBLIC58", 1)
    if s.w530.value:
        add(s.w530.value + 0x14, s.rrm14, "RRM14")
    if s.rrm14.value:
        add(s.rrm14.value + 0xc, s.rrm_c, "RRM_C")
    if s.w22c.value:
        add(s.w22c.value + 4, s.assoc4, "ASSOC4")
    if s.assoc4.value:
        add(s.assoc4.value, s.assoc_word, "ASSOC_WORD")
    if s.w18.value:
        add(s.w18.value + 0x1f8, override, "OVERRIDE")
    for slot, b in enumerate(s.bss):
        if s.w1e0.value:
            add(s.w1e0.value + slot * 4, b.pointer, "BSS_POINTER")
        if b.pointer.value:
            for name, offset, label in [("byte6", 6, "BSS6"), ("byte4", 4, "BSS4"),
                                        ("byte8", 8, "BSS8"), ("byte10", 0x10, "BSS10")]:
                add(b.pointer.value + offset, getattr(b, name), label, 1)
            add(b.pointer.value + 0x98, b.assoc98, "BSS98")
        if b.assoc98.value:
            add(b.assoc98.value + 8, b.state8, "BSS_STATE8")
    return fields


class Missing(ValueError):
    def __init__(self, trace):
        super().__init__("Unsupplied reached context")
        self.trace = trace


def emulate(functions, c, deny=None):
    import unicorn as uc
    from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC
    cpu = uc.Uc(uc.UC_ARCH_ARM, uc.UC_MODE_ARM | uc.UC_MODE_LITTLE_ENDIAN)
    cpu.mem_map(RAM, 0x10000)
    loaded, bindings = {}, {}
    for name, item in functions.items():
        start, code = item["address"], bytearray(item["code"])
        for off, (style, kind, symbol) in item["relocations"].items():
            if style != 9 or kind not in (28, 29) or symbol[0] not in functions:
                continue
            target = functions[symbol[0]]
            require((symbol[4], symbol[1], symbol[2]) == target["symbol"], "Wrong symbol binding")
            word = struct.unpack_from("<I", code, off)[0]
            require(word >> 28 != 15 and (word >> 25) & 7 == 5 and
                    bool(word & (1 << 24)) == (kind == 28), "Unsupported branch encoding")
            addend = (word & 0xffffff) << 2
            if addend & (1 << 25):
                addend -= 1 << 26
            require(addend == -8, "Unsupported branch addend")
            delta = target["address"] + addend - (start + off)
            require(delta % 4 == 0 and -(1 << 25) <= delta < 1 << 25, "Branch out of range")
            struct.pack_into("<I", code, off, (word & 0xff000000) | ((delta >> 2) & 0xffffff))
            bindings[start + off] = target["address"]
        cpu.mem_map(start, 0x1000)
        cpu.mem_write(start, bytes(code))
        loaded[name] = (start, start + len(code), item["relocations"])
    if deny == "unknown-call":
        bindings.clear()
        loaded = {name: (start, end, {}) for name, (start, end, _) in loaded.items()}
    fields = graph(*c)
    for address, (size, value, known, _) in fields.items():
        if known == 1:
            cpu.mem_write(address, value.to_bytes(size, "little"))
    cpu.reg_write(UC_ARM_REG_R0, W)
    cpu.reg_write(UC_ARM_REG_SP, STACK)
    cpu.reg_write(UC_ARM_REG_LR, STOP)
    trace, initialized, coverage = [], set(), set()
    state = {"pc": 0, "slot": 255, "pass": 0}

    def owner(address, size=4):
        for name, (start, end, relocations) in loaded.items():
            if start <= address and address + size <= end:
                return name, start, end, relocations
        raise ValueError("Unknown executed code/data")

    def instruction(engine, address, size, _):
        name, start, end, rel = owner(address, size)
        require(size == 4 and address % 4 == 0, "Unexpected instruction width")
        coverage.add(name); state["pc"] = address
        if address - start in rel:
            require(address in bindings and deny != "relocation", "Executed unresolved relocation")
        word = int.from_bytes(engine.mem_read(address, 4), "little")
        require((word & 0x0ffffff0) != 0x012fff30, "Unknown indirect call")
        if (word & 0x0ffffff0) == 0x012fff10:
            require(word & 15 == 14, "Unknown indirect branch")
        if (word >> 25) & 7 == 5:
            require(word >> 28 != 15, "Unknown exchange call")
            delta = (word & 0xffffff) << 2
            if delta & (1 << 25):
                delta -= 1 << 26
            target = address + 8 + delta
            if not start <= target < end:
                require(address in bindings and bindings[address] == target, "Unknown external call")
        require(word not in (0xf57ff04e, 0xf57ff04f), "Unexpected device barrier")

    def memory(engine, access, address, size, value, _):
        require(RAM <= address and address + size <= STACK, "Unknown context address")
        if address >= STACK - 0x1000:
            span = set(range(address, address + size))
            if access == uc.UC_MEM_WRITE:
                initialized.update(span)
            else:
                require(span <= initialized, "Uninitialized stack read")
            return
        require(access == uc.UC_MEM_READ and address in fields, "Unknown context read/write")
        width, _, known, field = fields[address]
        require(size == width, "Unexpected context width")
        offset = state["pc"] - functions[ENTRY]["address"]
        if field == F["BSS_POINTER"]:
            require(offset in (0x104, 0x130, 0x168), "Unknown BSS pass load")
            state["pass"] = {0x104: 1, 0x130: 2, 0x168: 3}[offset]
            state["slot"] = (address - c[0].w1e0.value) // 4
        is_bss = field >= F["BSS_POINTER"]
        trace.append((field, state["slot"] if is_bss else 255, state["pass"] if is_bss else 0))
        if known != 1:
            raise Missing(list(trace))

    cpu.hook_add(uc.UC_HOOK_CODE, instruction)
    cpu.hook_add(uc.UC_HOOK_MEM_READ | uc.UC_HOOK_MEM_WRITE, memory)
    def unmapped(*_):
        state["unmapped"] = True
        return False
    cpu.hook_add(uc.UC_HOOK_MEM_READ_UNMAPPED, unmapped)
    try:
        cpu.emu_start(functions[ENTRY]["address"], STOP, count=5000)
    except uc.UcError:
        require(state.get("unmapped", False), "Unexpected emulator fault")
        raise ValueError("Unmapped policy pointer") from None
    require(cpu.reg_read(UC_ARM_REG_PC) == STOP, "Instruction bound exceeded")
    result = cpu.reg_read(UC_ARM_REG_R0)
    require(result in (0, 1), "Non-Boolean actual policy result")
    return result, trace, coverage


def bridge_source():
    enums = ",".join("AXD_WAKE_" + name for name in FIELDS)
    return '#include "wake_policy.h"\n#include <stddef.h>\n' + f'''
unsigned field_id(unsigned i) {{ static const unsigned ids[]={{{enums}}}; return ids[i]; }}
void layout(unsigned *out) {{
    out[0]=sizeof(struct axd_wake_u8); out[1]=sizeof(struct axd_wake_u32);
    out[2]=sizeof(struct axd_wake_bss); out[3]=sizeof(struct axd_wake_inputs);
    out[4]=sizeof(struct axd_wake_access); out[5]=sizeof(struct axd_wake_result);
}}
'''


def evaluate(lib, c):
    result = Result()
    decision = lib.axd_wake_policy_evaluate(ct.byref(c[0]), c[1], ct.byref(result))
    require(decision == result.decision and result.accesses <= 256, "Invalid C result")
    trace = [(a.field, a.slot, a.pass_) for a in result.trace[:result.accesses]]
    return decision, trace, result


def deep(slot=0, optional=True):
    c = case(); s, _ = c; b = s.bss[slot]
    if optional:
        s.wb4.value = OPT; s.w2fc.value = NEST; s.object2fc_4_c.value = 3
        s.public58.value = 1; s.rrm_c.value = 3
    b.pointer.value = BSS + slot * 0x100; b.byte8.value = b.byte10.value = 1
    b.assoc98.value = BASSOC + slot * 0x100; s.primary.value = b.pointer.value
    return c


def cases():
    out = []
    out.append(("all-zero-optional-absent", case()))
    for name in ("w19c", "w19b", "w199", "w19a", "w198", "w300", "w336", "w568", "w188", "w1a0"):
        for value in (1, 2, 0x80):
            c = case(); getattr(c[0], name).value = value; out.append((name, c))
    for value in (1, 0x80000000, 0xffffffff):
        c = case(); c[1].value = value; out.append(("override", c))
        c = case(); c[0].assoc_word.value = value; out.append(("assoc-word", c))
    for name in ("w568", "w188", "w1a0"):
        for value in (0x80000000, 0xffffffff):
            c = case(); getattr(c[0], name).value = value; out.append(("full-word-flag", c))
    for value in (0, 1, 3, 4, 0x80000000, 0xffffffff):
        c = case(); c[0].w2fc.value = NEST; c[0].object2fc_4_c.value = value
        out.append(("optional-threshold", c))
        c = case(); c[0].public58.value = 0x80; c[0].rrm_c.value = value
        out.append(("rrm-threshold", c))
    for value in (0, 1, 0x80):
        c = case(); c[0].wb4.value = OPT; c[0].b4_c.value = value; out.append(("optional-byte", c))
    for slot in range(16):
        for pass_ in (1, 2, 3):
            c = case(); b = c[0].bss[slot]; b.pointer.value = BSS + slot * 0x100
            if pass_ == 1:
                b.byte6.value = 0x80; b.byte4.value = 2
            else:
                b.byte8.value = 0x80
                if pass_ == 3:
                    c[0].primary.value = b.pointer.value; b.byte10.value = 2
                    b.assoc98.value = BASSOC + slot * 0x100; b.state8.value = 0x80000000
            out.append((f"pass-{pass_}-slot-{slot}", c))
    for state in (0, 0x1c, 1, 0x80000000, 0xffffffff):
        c = case(); b = c[0].bss[15]; b.pointer.value = BSS + 15 * 0x100
        b.byte8.value = b.byte10.value = 1; c[0].primary.value = b.pointer.value
        b.assoc98.value = BASSOC + 15 * 0x100; b.state8.value = state
        out.append(("primary-third-state", c))
    # Full arrays whose entries fail each pass retain separate 16-entry scans.
    c = case()
    for slot, b in enumerate(c[0].bss):
        b.pointer.value = BSS + slot * 0x100
    out.append(("all-present-no-match", c))
    # Repeated pointer identities must carry one coherent object's fields.
    # All16 primary aliases avoid pass2's positive non-primary predicate.
    c = case(); c[0].primary.value = BSS
    c[0].wb4.value = OPT; c[0].w2fc.value = NEST; c[0].object2fc_4_c.value = 3
    c[0].public58.value = 1; c[0].rrm_c.value = 3
    for b in c[0].bss:
        b.pointer.value = BSS; b.byte8.value = b.byte10.value = 1
        b.assoc98.value = BASSOC
    out.append(("all-primary-aliases-218-loads", c))
    for slot in (0, 15):
        c = deep(slot); c[0].bss[slot].assoc98.value = 0; c[0].bss[slot].state8.known = 0
        out.append(("absent-association-skips-state", c))
    # Every later input is unavailable, but the first positive load suffices.
    c = case(); c[0].w19c.value = 0x80
    for name, typ in Inputs._fields_:
        if typ in (U8, U32) and name != "w19c":
            getattr(c[0], name).known = 0
    for b in c[0].bss:
        for name, _ in Bss._fields_: getattr(b, name).known = 0
    c[1].known = 0
    out.append(("first-positive-skips-missing-rest", c))
    c = case(); c[0].b4_c.known = c[0].object2fc_4.known = c[0].object2fc_4_c.known = 0
    c[0].w530.known = c[0].rrm14.known = c[0].rrm_c.known = 0
    out.append(("guarded-absences-skip-missing", c))
    rng = random.Random(0x13051a7)
    for _ in range(256):
        c = case(); s, override = c
        # Mostly false globals permit randomized arrays and deep short circuits.
        for name in ("w19c", "w19b", "w199", "w19a", "w198", "w300", "w336", "w568", "w188", "w1a0"):
            getattr(s, name).value = rng.choice([0] * 20 + [1, 0x80])
        s.wb4.value = rng.choice([0, OPT]); s.b4_c.value = rng.choice([0] * 10 + [0x80])
        s.w2fc.value = rng.choice([0, NEST]); s.object2fc_4_c.value = rng.choice([0, 3, 4])
        s.public58.value = rng.choice([0, 1]); s.rrm_c.value = rng.choice([0, 3, 4])
        s.assoc_word.value = rng.choice([0] * 20 + [0xffffffff])
        override.value = rng.choice([0] * 20 + [0x80000000])
        s.primary.value = BSS + rng.randrange(16) * 0x100
        for slot, b in enumerate(s.bss):
            b.pointer.value = rng.choice([0, BSS + slot * 0x100])
            for name in ("byte4", "byte6", "byte8", "byte10"):
                getattr(b, name).value = rng.choice([0, 0, 1, 0x80])
            b.assoc98.value = rng.choice([0, BASSOC + slot * 0x100])
            b.state8.value = rng.choice([0, 0x1c, 1, 0x80000000])
        out.append(("random-combination", c))
    return out


def missing_cases():
    out = []
    for name, typ in Inputs._fields_:
        if typ in (U8, U32):
            c = deep(); getattr(c[0], name).known = 0; out.append((name, c))
    c = deep(); c[1].known = 0; out.append(("current-override", c))
    for slot in range(16):
        for name, _ in Bss._fields_:
            c = deep(slot)
            if name == "byte4":
                c[0].bss[slot].byte6.value = 1
            getattr(c[0].bss[slot], name).known = 0
            out.append((f"slot{slot}-{name}", c))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    import unicorn
    require(unicorn.__version__ == "2.1.4", "Use pinned Unicorn 2.1.4")
    require(args.module.stat().st_size <= 64 * 1024 * 1024, "Oversize module")
    data = args.module.read_bytes(); functions = selected(data, args.repo)
    source_files = [args.repo / ("driver/wake_policy." + suffix) for suffix in ("c", "h")]
    source_digests = [hashlib.sha256(f.read_bytes()).hexdigest() for f in source_files]
    testcases = cases(); coverage = set(); longest = 0
    with tempfile.TemporaryDirectory(prefix="axd-wake-arm-") as folder:
        folder = Path(folder); bridge = folder / "bridge.c"; bridge.write_text(bridge_source())
        path = folder / ("model.dylib" if sys.platform == "darwin" else "model.so")
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-fPIC",
                        "-dynamiclib" if sys.platform == "darwin" else "-shared", "-I", str(args.repo / "driver"),
                        str(args.repo / "driver/wake_policy.c"), str(bridge), "-o", str(path)], check=True)
        lib = ct.CDLL(str(path))
        lib.axd_wake_policy_evaluate.argtypes = [ct.POINTER(Inputs), U32, ct.POINTER(Result)]
        lib.axd_wake_policy_evaluate.restype = ct.c_int
        lib.layout.argtypes = [ct.POINTER(ct.c_uint)]; lib.layout.restype = None
        lib.field_id.argtypes = [ct.c_uint]; lib.field_id.restype = ct.c_uint
        sizes = (ct.c_uint * 6)(); lib.layout(sizes)
        require(list(sizes) == [ct.sizeof(t) for t in (U8, U32, Bss, Inputs, Access, Result)], "C layout mismatch")
        for i in range(len(FIELDS)):
            require(lib.field_id(i) == i, "C field enum changed")
        for label, c in testcases:
            actual, trace, reached = emulate(functions, c)
            decision, expected, result = evaluate(lib, c)
            require(decision in (1, 2) and actual == (decision == 1), f"Decision mismatch: {label}")
            require(trace == expected, f"Load-order mismatch: {label}")
            require((not actual and result.reason == 0) or
                    (actual and result.reason == trace[-1][0]), f"Reason mismatch: {label}")
            require((result.slot, result.pass_) == (trace[-1][1:] if actual else (255, 0)),
                    f"Reason location mismatch: {label}")
            if label == "all-primary-aliases-218-loads":
                require(len(trace) == 218, "Longest trace changed")
            longest = max(longest, len(trace))
            coverage.update(reached)
        dependencies = missing_cases()
        for label, c in dependencies:
            decision, expected, result = evaluate(lib, c)
            require(decision == 3 and result.reason == expected[-1][0], f"Expected C dependency: {label}")
            try:
                emulate(functions, c)
            except Missing as error:
                require(error.trace == expected, f"Missing-context load-prefix mismatch: {label}")
            else:
                raise ValueError("Missing reached context unexpectedly succeeded")
        invalid_pointers = 0
        for name in ("public_pointer", "object2fc_4", "w530", "rrm14", "w22c", "assoc4", "w18", "w1e0"):
            c = deep(); getattr(c[0], name).value = 0
            require(evaluate(lib, c)[0] == 0, "Expected C invalid required pointer")
            try:
                emulate(functions, c)
            except ValueError as error:
                require(str(error) in ("Unknown context address", "Unmapped policy pointer"),
                        "Wrong null-pointer rejection")
                invalid_pointers += 1
            else:
                raise ValueError("Known null required pointer unexpectedly succeeded")
    rejected = 0
    for deny in ("unknown-call", "relocation"):
        try:
            emulate(functions, case(), deny)
        except ValueError as error:
            require(str(error) in ("Executed unresolved relocation", "Unknown external call"),
                    "Wrong call rejection")
            rejected += 1
        else:
            raise ValueError("Unauthorized call unexpectedly succeeded")
    altered = copy.deepcopy(functions)
    style, _, symbol = altered[ENTRY]["relocations"][0x5c]
    altered[ENTRY]["relocations"][0x5c] = (style, 2, symbol)
    try:
        emulate(altered, case())
    except ValueError as error:
        require(str(error) == "Executed unresolved relocation", "Wrong unsupported-reloc rejection")
        rejected += 1
    else:
        raise ValueError("Unsupported executed relocation unexpectedly succeeded")
    require(source_digests == [hashlib.sha256(f.read_bytes()).hexdigest() for f in source_files],
            "C sources changed during comparison")
    require(hashlib.sha256(args.module.read_bytes()).hexdigest() == hashlib.sha256(data).hexdigest(),
            "Input changed during comparison")
    print(f"Wake policy ARM: {len(testcases)} decision/load-trace comparisons; {len(dependencies)} missing-context prefixes; longest {longest} loads.")
    print(f"Actual functions {len(coverage)}; rejected calls/relocations {rejected}; rejected null required pointers {invalid_pointers}.")
    print("Explicit synthetic context only; no MMIO, external stubs, or kernel/device execution.")


if __name__ == "__main__":
    main()
