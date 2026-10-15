#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare composed PHY/TSSI C traces with selected actual ARM instructions.

Requires caller-supplied pinned stock ELF and Unicorn 2.1.4. Uses the companion
check_phy_arm.py ELF parser; that program's original primitive cases remain
separate. Selected REL CALL/JUMP24 bindings use actual symbol records/addends.
Only the reviewed table reader's bounded intra-function ABS32 dispatch is
resolved; other executed relocations, unknown calls and unsupplied reads fail.
MAC-inactive table cases execute no external PSM shim. No target/kernel loading,
real MMIO, mock-success external functions, or vendor payload output occurs.

Primary tool/format references:
https://github.com/unicorn-engine/unicorn/releases/tag/2.1.4
https://github.com/ARM-software/abi-aa/blob/main/aaelf32/aaelf32.rst
Behavior provenance: DRIVER_AC_TABLE_READ_AUDIT and DRIVER_RESOURCE_AUDIT.
"""
import argparse
import ctypes as ct
import copy
import hashlib
import importlib.util
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

CODE, RAM, MMIO = 0x1000000, 0x2000000, 0x3000000
STOP, STACK = CODE + 0x800000, RAM + 0x10000
PI, PUB, DP, DESC, AC, SAVE, SH, TABLEBUF, STF = [RAM + i * 0x1000 for i in range(9)]
ST, SY, W32, R32, R16, W16 = 1, 2, 3, 4, 5, 7
TSSI = "wlc_phy_ac_idletssi_force"
READ, MODIFY, WRITE = ["phy_utils_" + s + "_phyreg" for s in ("read", "mod", "write")]
TABLE = "wlc_phy_table_read_acphy"
TABLE_EXT = "wlc_phy_table_read_ext_acphy"
TABLE_READER = "phy_utils_read_phytable_ext"
TABLE_FUNCTIONS = {TABLE, TABLE_EXT, TABLE_READER, "phy_stf_get_data",
                   "wlc_phy_disable_hwaci_fcbs_trig", "wlc_phy_restore_hwaci_fcbs_trig"}
# Reviewed actual reader's width-dispatch entries only; no generic ABS32 linker.
DISPATCH_SITES = set(range(0xa4, 0x188, 4))
DISPATCH_TARGETS = {0x188, 0x210, 0x290, 0x2bc, 0x2e4, 0x300}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def load(module, repo):
    spec = importlib.util.spec_from_file_location("base_phy_arm", repo / "tests/check_phy_arm.py")
    base = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(base)
    require(module.stat().st_size <= 64 * 1024 * 1024, "Oversize input")
    data = module.read_bytes()
    require(hashlib.sha256(data).hexdigest() == base.MODULE_SHA256, "Unrecognized input")
    wanted = {TSSI, READ, MODIFY, WRITE} | TABLE_FUNCTIONS
    blobs = base.functions(data, wanted)
    h = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
    sections = [struct.unpack_from("<10I", data, h[5] + i * 40) for i in range(h[11])]

    def region(offset, size):
        require(0 <= offset <= len(data) and 0 <= size <= len(data) - offset, "Invalid ELF bounds")
        return data[offset:offset + size]

    symbols = {}
    selected = {}
    for index, section in enumerate(sections):
        if section[1] != 2:
            continue
        strings_section = sections[section[6]]
        strings = region(strings_section[4], strings_section[5])
        table = []
        for off in range(0, section[5], 16):
            n, value, size, info, _, owner = struct.unpack_from("<IIIBBH", region(section[4] + off, 16))
            require(n < len(strings), "Invalid ELF string")
            end = strings.find(b"\0", n)
            require(end >= 0, "Unterminated ELF string")
            name = strings[n:end].decode("ascii")
            table.append((name, value, size, info, owner))
            if name in wanted:
                require(name not in selected, "Duplicate selected symbol")
                selected[name] = (owner, value, size)
        symbols[index] = table
    require(set(selected) == wanted, "Missing selected symbol")
    result = {}
    for number, name in enumerate(sorted(wanted)):
        owner, value, size = selected[name]
        code, expected_sites = blobs[name]
        relocations = {}
        for section in sections:
            if section[1] not in (4, 9) or section[7] != owner:
                continue
            stride = 12 if section[1] == 4 else 8
            require(section[9] == stride and section[5] % stride == 0, "Invalid relocation stride")
            require(section[6] in symbols, "Invalid relocation symbol table")
            for pos in range(0, section[5], stride):
                record = region(section[4] + pos, stride)
                at, info = struct.unpack_from("<II", record)
                if not value <= at < value + size:
                    continue
                require(at % 4 == 0 and at + 4 <= value + size, "Unaligned relocation")
                require(info >> 8 < len(symbols[section[6]]), "Invalid relocation symbol")
                target = symbols[section[6]][info >> 8]
                require(at - value not in relocations, "Duplicate relocation")
                # Preserve unsupported entries: execution/data reads must reject
                # them. Only actual REL ARM CALL/JUMP24 entries are resolved.
                relocations[at - value] = (section[1], info & 255, target)
        require(set(relocations) == expected_sites, "Parser relocation disagreement")
        result[name] = {"code": code, "address": CODE + number * 0x10000,
                        "relocations": relocations, "symbol": selected[name]}
    return result


def emulate(functions, entry, count=1, snapshot=True, replies=(), saved=(0x111, 0x222, 0x333, 0x3ff),
            bookkeeping=0, selector=0, value=0, bus=0, threshold=1, deny=None):
    import unicorn as uc
    from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2
    from unicorn.arm_const import UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC
    cpu = uc.Uc(uc.UC_ARCH_ARM, uc.UC_MODE_ARM | uc.UC_MODE_LITTLE_ENDIAN)
    cpu.mem_map(RAM, 0x10000)
    cpu.mem_map(MMIO, 0x1000)
    loaded, resolved, dispatch = {}, {}, {}
    for name, item in functions.items():
        start = item["address"]
        code = bytearray(item["code"])
        if name == TABLE_READER:
            require({off for off, (_, kind, _) in item["relocations"].items() if kind == 2} ==
                    DISPATCH_SITES, "Unsupported dispatch entries")
        for off, (style, kind, target) in item["relocations"].items():
            if kind == 2 and name == TABLE_READER:
                require(style == 9 and off in DISPATCH_SITES, "Unsupported dispatch entry")
                require(target[0] == "" and target[1] == 0 and target[2] == 0 and
                        target[3] & 15 == 3 and target[4] == item["symbol"][0],
                        "Unsupported dispatch symbol")
                addend = struct.unpack_from("<I", code, off)[0]
                relative = target[1] + addend - item["symbol"][1]
                require(relative in DISPATCH_TARGETS and 0 <= relative < len(code) and
                        relative % 4 == 0, "Unsupported dispatch target")
                struct.pack_into("<I", code, off, start + relative)
                dispatch[start + off] = start + relative
                continue
            if style != 9 or kind not in (28, 29) or target[0] not in functions:
                continue
            destination = functions[target[0]]
            require((target[4], target[1], target[2]) == destination["symbol"], "Symbol binding mismatch")
            word = struct.unpack_from("<I", code, off)[0]
            require(word >> 28 != 15 and (word >> 25) & 7 == 5, "Non-ARM branch relocation")
            require(bool(word & (1 << 24)) == (kind == 28), "Call/branch relocation mismatch")
            addend = (word & 0xffffff) << 2
            if addend & (1 << 25):
                addend -= 1 << 26
            require(addend == -8, "Unexpected selected branch addend")
            delta = destination["address"] + addend - (start + off)
            require(delta % 4 == 0 and -(1 << 25) <= delta < 1 << 25, "Branch range overflow")
            struct.pack_into("<I", code, off, (word & 0xff000000) | ((delta >> 2) & 0xffffff))
            resolved[start + off] = destination["address"]
        cpu.mem_map(start, 0x1000)
        cpu.mem_write(start, bytes(code))
        loaded[name] = (start, start + len(code), item["relocations"])
    if deny == "unknown-call":
        resolved.clear()  # Loaded instructions stay patched; authorization does not.
        loaded = {name: (start, end, {}) for name, (start, end, _) in loaded.items()}
    fields = {
        PI + 4: (4, PUB), PUB: (4, 0xb), PUB + 4: (4, 51), PUB + 8: (1, count),
        PI + 8: (4, SH), SH + 0xc: (4, bus), SH + 0x48: (4, RAM + 0x9000), AC: (4, PI),
        PI + 0xc4: (4, MMIO), PI + 0xc8: (4, DP), DP: (4, DESC),
        DESC + 0xa8: (2, 0x3fc), DESC + 0xaa: (2, 0x3fe), DESC + 0x8e: (2, 0x3e0),
        DESC + 0x1a: (2, 0x120), PI + 0xb0: (4, STF), STF + 8: (4, STF + 0x100),
        PI + 0x104: (2, bookkeeping), PI + 0x106: (2, threshold),
        TABLEBUF: (4, 0xdeadbeef)
    }
    for index in range(4):
        fields[SAVE + 2 * index] = (2, saved[index])
    for address, (width, word) in fields.items():
        cpu.mem_write(address, word.to_bytes(width, "little"))
    arguments = (AC, SAVE, int(snapshot), 0) if entry == TSSI else (
        (PI, 0x29, 1, selector) if entry == TABLE else
        (PI, selector, value if entry == WRITE else 0, 0))
    for reg, word in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), arguments):
        cpu.reg_write(reg, word)
    entry_sp = STACK - 0x20
    cpu.reg_write(UC_ARM_REG_SP, entry_sp)
    cpu.reg_write(UC_ARM_REG_LR, STOP)
    events, remaining, initialized, executed, patched_calls = [], list(replies), set(), set(), set()
    if entry == TABLE:
        cpu.mem_write(entry_sp, struct.pack("<II", 32, TABLEBUF))
        initialized.update(range(entry_sp, entry_sp + 8))

    def owner_of(address, size=4):
        for name, (start, end, relocations) in loaded.items():
            if start <= address and address + size <= end:
                return name, start, end, relocations
        raise ValueError("Unexpected executed code/data")

    def instruction(engine, address, size, _):
        name, start, end, relocations = owner_of(address, size)
        require(size == 4 and address % 4 == 0, "Unexpected instruction width")
        executed.add(name)
        off = address - start
        if off in relocations:
            require(address in resolved and deny != "relocation", "Executed unresolved relocation")
            patched_calls.add(address)
        word = int.from_bytes(engine.mem_read(address, 4), "little")
        if (word & 0x0ffffff0) == 0x012fff30:
            raise ValueError("Unknown indirect call")
        if (word & 0x0ffffff0) == 0x012fff10:
            require(word & 15 == 14, "Unknown indirect branch")
        if word >> 28 == 15 and (word >> 25) & 7 == 5:
            raise ValueError("Unknown exchange call")
        if word >> 28 != 15 and (word >> 25) & 7 == 5:
            delta = (word & 0xffffff) << 2
            if delta & (1 << 25):
                delta -= 1 << 26
            target = address + 8 + delta
            # Reject unauthorized external branch sites even when an ARM
            # condition would currently skip them. No unknown call may hide.
            if not start <= target < end:
                require(address in resolved and resolved[address] == target, "Unknown external call/branch")
        if word in (0xf57ff04e, 0xf57ff04f):
            events.append((ST if word == 0xf57ff04e else SY, 0, 0))

    def memory(engine, access, address, size, word, _):
        if MMIO <= address < MMIO + 0x1000:
            off = address - MMIO
            if access == uc.UC_MEM_READ:
                require(((size == 2 and off in (0x3fe, 0x3e0)) or
                         (size == 4 and off == 0x120)) and remaining, "Unexpected MMIO read")
                engine.mem_write(address, remaining.pop(0).to_bytes(size, "little"))
                events.append((R16 if size == 2 else R32, off, 0))
            else:
                require((size == 2 and off in (0x3fc, 0x3fe)) or
                        (size == 4 and off == 0x3fc), "Unexpected MMIO write")
                events.append((W16 if size == 2 else W32, off, word))
        elif RAM <= address and address + size <= STACK:
            if address >= STACK - 0x1000:
                span = set(range(address, address + size))
                if access == uc.UC_MEM_WRITE:
                    initialized.update(span)
                else:
                    require(span <= initialized, "Unsupplied stack read")
            elif access == uc.UC_MEM_READ:
                require(address in fields and fields[address][0] == size, "Unsupplied context read")
            else:
                require(((address == PI + 0x104 or address in range(SAVE, SAVE + 8, 2)) and size == 2) or
                        (address == TABLEBUF and size == 4),
                        "Unexpected context write")
        else:
            name, start, _, relocations = owner_of(address, size)
            overlaps = [off for off in relocations if address - start <= off < address - start + size]
            require(access == uc.UC_MEM_READ and (not overlaps or
                    (size == 4 and len(overlaps) == 1 and address in dispatch)),
                    "Unresolved code-data relocation/write")

    cpu.hook_add(uc.UC_HOOK_CODE, instruction)
    cpu.hook_add(uc.UC_HOOK_MEM_READ | uc.UC_HOOK_MEM_WRITE, memory)
    cpu.emu_start(functions[entry]["address"], STOP, timeout=1000000, count=10000)
    require(cpu.reg_read(UC_ARM_REG_PC) == STOP and not remaining, "Bound/reply mismatch")
    result = lambda address: int.from_bytes(cpu.mem_read(address, 2), "little")
    return (events, result(PI + 0x104), tuple(result(SAVE + i * 2) for i in range(4)),
            executed, len(patched_calls), int.from_bytes(cpu.mem_read(TABLEBUF, 4), "little"),
            cpu.reg_read(UC_ARM_REG_R0) & 0xffff)


BRIDGE = r'''
#include "phy_tssi.h"
struct output { uint32_t bookkeeping, saved[4], valid, result; uint64_t attempted, completed; };
int compare_tssi(unsigned snapshot, uint8_t cores, uint16_t bookkeeping,
                 const uint16_t *saved, uint32_t bus, uint16_t threshold,
                 axd_d11_io_fn io, struct output *out) {
    struct axd_phy_tssi job;
    struct axd_tssi_context ctx = {0};
    unsigned steps=0, i;
    ctx.hardware.phy=(struct axd_phy_context){130,1,0xb,51,bookkeeping,{0,0},cores,0};
    ctx.hardware.shared_bus_word=bus; ctx.hardware.write_threshold=threshold;
    ctx.valid=15;
    for (i=0;i<4;i++) ctx.saved[i]=saved[i];
    if (axd_phy_tssi_begin(&job,snapshot ? AXD_TSSI_SNAPSHOT : AXD_TSSI_RESTORE,&ctx)) return -1;
    while (job.phase==AXD_TSSI_RUNNING && steps++<2000) axd_phy_tssi_step(&job,io,0);
    out->bookkeeping=job.context.hardware.phy.bookkeeping;
    for (i=0;i<4;i++) out->saved[i]=job.context.saved[i];
    out->valid=job.context.valid; out->result=0;
    out->attempted=job.attempted_ops; out->completed=job.completed_ops;
    return job.phase==AXD_TSSI_TRACE_COMPLETE ? 0 : -1;
}
int compare_table(uint32_t index, uint8_t cores, uint16_t bookkeeping,
                  uint32_t bus, uint16_t threshold, axd_d11_io_fn io, struct output *out) {
    struct axd_phy_table job;
    struct axd_phy_table_context ctx = {0};
    unsigned steps=0;
    ctx.phy=(struct axd_phy_context){130,1,0xb,51,bookkeeping,{0,0},cores,0};
    ctx.shared_bus_word=bus; ctx.write_threshold=threshold;
    if (axd_phy_table_begin(&job,index,&ctx)) return -1;
    while (job.phase==AXD_TABLE_RUNNING && steps++<2000) axd_phy_table_step(&job,io,0);
    out->bookkeeping=job.context.phy.bookkeeping; out->result=job.result;
    out->attempted=job.attempted_ops; out->completed=job.completed_ops;
    return job.phase==AXD_TABLE_TRACE_COMPLETE && job.word_valid ? 0 : -1;
}
'''


class Operation(ct.Structure):
    _fields_ = [("kind", ct.c_int), ("offset", ct.c_uint32), ("value", ct.c_uint32)]


class Output(ct.Structure):
    _fields_ = [("bookkeeping", ct.c_uint32), ("saved", ct.c_uint32 * 4),
               ("valid", ct.c_uint32), ("result", ct.c_uint32)] + [
        ("attempted", ct.c_uint64), ("completed", ct.c_uint64)]


CALLBACK = ct.CFUNCTYPE(ct.c_int, ct.c_void_p, ct.POINTER(Operation), ct.POINTER(ct.c_uint32))


def model(lib, snapshot, cores, bookkeeping, saved, replies, bus=0, threshold=1, index=None):
    events, remaining, errors = [], list(replies), []

    @CALLBACK
    def io(_, operation, reply):
        op = operation.contents
        events.append((op.kind, op.offset, op.value))
        if op.kind in (R16, R32):
            if not remaining:
                errors.append("Missing model read reply")
                return -1
            reply[0] = remaining.pop(0)
        return 0

    out = Output()
    if index is None:
        values = (ct.c_uint16 * 4)(*saved)
        status = lib.compare_tssi(snapshot, cores, bookkeeping, values, bus, threshold, io, ct.byref(out))
        require(out.valid == 15, "Model snapshot validity mismatch")
    else:
        status = lib.compare_table(index, cores, bookkeeping, bus, threshold, io, ct.byref(out))
    require(status == 0 and not errors and not remaining, "Model execution/reply mismatch")
    require(out.attempted == out.completed == len(events), "Model callback accounting")
    return events, out.bookkeeping, tuple(out.saved), out.result


def table_replies(rng, bus, threshold, guard):
    replies = [rng.randrange(1 << 32) & ~1, guard]
    if guard & 2 == 0:
        replies.append(rng.randrange(1 << 16))
    flushes = (2 if threshold <= 1 else 1 if threshold == 2 else 0) if bus == 1 else 0
    replies.extend(rng.randrange(1 << 16) for _ in range(flushes + 2))
    if guard & 2 == 0:
        replies.append(rng.randrange(1 << 16))
    return replies


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    import unicorn
    require(unicorn.__version__ == "2.1.4", "Use pinned Unicorn 2.1.4")
    functions = load(args.module, args.repo)
    sources = [args.repo / ("driver/" + source + ".c")
               for source in ("phy_register", "phy_table", "phy_tssi", "psm_control", "wake_policy")]
    source_digests = [hashlib.sha256(source.read_bytes()).hexdigest() for source in sources]
    rng = random.Random(0x1305102)
    cases = [(cores, snapshot) for cores in (1, 2, 3, 4) for snapshot in (False, True) for _ in range(64)]
    coverage, calls = set(), 0
    with tempfile.TemporaryDirectory(prefix="axd-phy-composed-") as folder:
        folder = Path(folder)
        bridge = folder / "bridge.c"
        bridge.write_text(BRIDGE)
        path = folder / ("model.dylib" if sys.platform == "darwin" else "model.so")
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-fPIC",
                        "-dynamiclib" if sys.platform == "darwin" else "-shared", "-I", str(args.repo / "driver"),
                        *[str(args.repo / ("driver/" + source + ".c"))
                      for source in ("phy_register", "phy_table", "phy_tssi", "psm_control", "wake_policy")],
                        str(bridge), "-o", str(path)], check=True)
        lib = ct.CDLL(str(path))
        lib.compare_tssi.argtypes = [ct.c_uint, ct.c_uint8, ct.c_uint16, ct.POINTER(ct.c_uint16),
                                    ct.c_uint32, ct.c_uint16, CALLBACK, ct.POINTER(Output)]
        lib.compare_tssi.restype = ct.c_int
        lib.compare_table.argtypes = [ct.c_uint32, ct.c_uint8, ct.c_uint16, ct.c_uint32,
                                    ct.c_uint16, CALLBACK, ct.POINTER(Output)]
        lib.compare_table.restype = ct.c_int
        for cores, snapshot in cases:
            saved = tuple(rng.randrange(0x10000) for _ in range(4))
            bookkeeping = rng.randrange(0x10000)
            bus = rng.choice([0, 1, 2]); threshold = rng.choice([0, 1, 2, 3, 0xffff])
            replies = []
            if snapshot:
                for _ in range(max(0, cores - 2)):
                    replies.extend(table_replies(rng, bus, threshold, rng.randrange(1 << 16)))
            replies.extend(rng.randrange(0x10000) for _ in range(min(cores, 2)))
            actual = emulate(functions, TSSI, cores, snapshot, replies, saved, bookkeeping,
                             bus=bus, threshold=threshold)
            expected = model(lib, snapshot, cores, bookkeeping, saved, replies, bus, threshold)
            require(actual[:3] == expected[:3], "TSSI instruction/model mismatch")
            coverage.update(actual[3]); calls += actual[4]
        table_cases = 0
        for entry in (TABLE, READ):
            for index in (0, 0xc000, 0xffff, 0x1234ffff, 0xffffffff):
                if entry == READ and index < 0xc000:
                    continue
                for bus in (0, 1, 2):
                    for threshold in (0, 1, 2, 3, 0xffff):
                        for guard in (0xace0, 0xace2):
                            replies = table_replies(rng, bus, threshold, guard)
                            bookkeeping = rng.randrange(1 << 16)
                            actual = emulate(functions, entry, replies=replies, bookkeeping=bookkeeping,
                                             selector=index, bus=bus, threshold=threshold)
                            expected = model(lib, True, 1, bookkeeping, (0, 0, 0, 0), replies,
                                             bus, threshold, index)
                            require(actual[:2] == expected[:2], "Table instruction/model mismatch")
                            result = actual[5] if entry == TABLE else actual[6]
                            require(result == (expected[3] if entry == TABLE else expected[3] & 0xffff),
                                    "Table/PHY result mismatch")
                            coverage.update(actual[3]); calls += actual[4]; table_cases += 1
    # Packed write fixtures are independent literal expectations, without a C
    # standalone packed primitive. Full table C comparisons above are separate.
    packed_cases = [(0, 7, 2, 7, False), (2, 7, 2, 7, False),
                    (1, 0, 2, 1, False), (1, 1, 2, 0, True),
                    (1, 0, 0, 0, True), (1, 0, 1, 0, True),
                    (1, 0xffff, 1, 0, False), (1, 0xffff, 0, 0, True)]
    require(len(packed_cases) == len(set(packed_cases)) == 8, "Packed fixture count/uniqueness")
    for bus, before, threshold, after, flush in packed_cases:
        for selector, value in [(0, 0), (0xbfff, 0x12345678)]:
            actual = emulate(functions, WRITE, bookkeeping=before, selector=selector,
                             value=value, bus=bus, threshold=threshold,
                             replies=[0xa5a5] if flush else [])
            events = ([(R16, 0x3e0, 0), (SY, 0, 0)] if flush else []) + [
                (ST, 0, 0), (W32, 0x3fc, ((value & 0xffff) << 16) | selector)]
            require(actual[0] == events and actual[1] == after, "Packed write fixture mismatch")
            coverage.update(actual[3])
    rejected = 0
    for entry, kwargs in [(WRITE, {"selector": 0xc000}),
                          (TABLE, {"replies": [1]}),
                          (TSSI, {"deny": "unknown-call"}), (TSSI, {"deny": "relocation"})]:
        try:
            emulate(functions, entry, **kwargs)
        except ValueError as error:
            require(str(error) in ("Executed unresolved relocation", "Unknown external call/branch"),
                    "Wrong negative-case rejection")
            rejected += 1
        else:
            raise ValueError("Unresolved caller unexpectedly succeeded")
    for change in ("entry", "symbol", "target"):
        altered = copy.deepcopy(functions)
        reader = altered[TABLE_READER]
        style, kind, target = reader["relocations"][0x104]
        if change == "entry":
            reader["relocations"][0] = reader["relocations"].pop(0x104)
        elif change == "symbol":
            reader["relocations"][0x104] = (style, kind, (*target[:3], 2, target[4]))
        else:
            code = bytearray(reader["code"])
            struct.pack_into("<I", code, 0x104, reader["symbol"][1] + len(code))
            reader["code"] = bytes(code)
        try:
            emulate(altered, TABLE)
        except ValueError as error:
            require(str(error) in ("Unsupported dispatch entries", "Unsupported dispatch symbol",
                                   "Unsupported dispatch target"), "Wrong dispatch rejection")
            rejected += 1
        else:
            raise ValueError("Unreviewed dispatch unexpectedly succeeded")
    require(source_digests == [hashlib.sha256(source.read_bytes()).hexdigest() for source in sources],
            "C sources changed during comparison")
    require(hashlib.sha256(args.module.read_bytes()).hexdigest() ==
            "8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c", "Input changed")
    print(f"Composed PHY ARM: {len(cases)} all-count TSSI and {table_cases} table/high-selector C comparisons; {len(packed_cases)*2} packed-write fixtures.")
    print(f"Actual selected functions {len(coverage)}; resolved branch-site visits {calls}; negative closures rejected {rejected}.")
    print("Synthetic replies/context only. No mock-success externals, kernel/device execution, or payload output.")


if __name__ == "__main__":
    main()
