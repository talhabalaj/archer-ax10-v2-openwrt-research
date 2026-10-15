#!/usr/bin/env python3
"""Compare three PHY primitives with caller-supplied stock ARM instructions.

Requires Unicorn 2.1.4 in the invoking Python environment. Executes selected
functions only in an isolated emulator with synthetic context/read replies.
No device, kernel, firmware payload export or real MMIO access is involved.
"""
import argparse
import ctypes as ct
import hashlib
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from radio_firmware_inventory import MODULE_SHA256, require

CODE, RAM, MMIO = 0x1000000, 0x2000000, 0x3000000
STOP, STACK = CODE + 0x10000, RAM + 0x10000
PI, DESC_PTR, DESC, ANA = RAM, RAM + 0x1000, RAM + 0x2000, RAM + 0x3000
ST, SY, WRITE16, READ16 = 1, 2, 7, 5


def functions(data, wanted):
    """Resolve selected ARM function bytes and relocation sites in ELF32 ET_REL."""
    require(len(data) >= 52 and data[:6] == b"\x7fELF\x01\x01", "Expected ELF32 LE")
    h = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
    require(h[0] == 1 and h[1] == 40 and h[10] == 40, "Expected ARM ET_REL")

    def region(offset, size):
        require(0 <= offset <= len(data) and 0 <= size <= len(data) - offset, "Invalid ELF bounds")
        return data[offset:offset + size]

    require(0 < h[11] < 65535 and h[12] < h[11], "Invalid ELF sections")
    region(h[5], h[11] * 40)
    sections = [struct.unpack_from("<10I", data, h[5] + i * 40) for i in range(h[11])]
    found = {}
    for section in sections:
        if section[1] != 2:
            continue
        require(section[9] == 16 and section[5] % 16 == 0 and section[6] < len(sections), "Invalid symbols")
        strings_section = sections[section[6]]
        strings = region(strings_section[4], strings_section[5])
        symbols = region(section[4], section[5])
        for off in range(0, len(symbols), 16):
            n, value, size, info, _, index = struct.unpack_from("<IIIBBH", symbols, off)
            require(n < len(strings), "Invalid symbol string offset")
            end = strings.find(b"\0", n)
            require(end >= 0, "Unterminated symbol")
            name = strings[n:end].decode("ascii")
            if name not in wanted:
                continue
            require(name not in found and info & 15 == 2 and 0 < index < len(sections), "Invalid function")
            owner = sections[index]
            require(owner[1] != 8 and value % 4 == 0 and 0 < size <= 0x1000 and size % 4 == 0,
                    "Unsupported function layout")
            require(value <= owner[5] and size <= owner[5] - value, "Function exceeds section")
            relocations = set()
            for rel in sections:
                if rel[1] not in (4, 9) or rel[7] != index:
                    continue
                stride = 12 if rel[1] == 4 else 8
                require(rel[9] == stride and rel[5] % stride == 0, "Invalid relocation table")
                for pos in range(0, rel[5], stride):
                    target = struct.unpack_from("<I", region(rel[4] + pos, stride))[0]
                    if value <= target < value + size:
                        relocations.add(target - value)
            found[name] = (region(owner[4] + value, size), relocations)
    require(set(found) == set(wanted), "Missing selected functions")
    return found


def emulate(function, command, selector, mask, value, bookkeeping, replies):
    import unicorn as uc
    from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3
    from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC

    code, relocations = function
    cpu = uc.Uc(uc.UC_ARCH_ARM, uc.UC_MODE_ARM | uc.UC_MODE_LITTLE_ENDIAN)
    cpu.mem_map(CODE, 0x1000)
    cpu.mem_map(RAM, 0x10000)
    cpu.mem_map(MMIO, 0x1000)
    cpu.mem_write(CODE, code)
    fields = {PI + 0xc4: (4, MMIO), PI + 0xc8: (4, DESC_PTR), DESC_PTR: (4, DESC),
              DESC + 0xa8: (2, 0x3fc), DESC + 0xaa: (2, 0x3fe),
              DESC + 0x94: (2, 0x3e6), ANA: (4, PI), PI + 0x104: (2, bookkeeping)}
    for address, (width, word) in fields.items():
        cpu.mem_write(address, word.to_bytes(width, "little"))
    for reg, word in ((UC_ARM_REG_R0, ANA if command == 3 else PI),
                      (UC_ARM_REG_R1, selector), (UC_ARM_REG_R2, mask),
                      (UC_ARM_REG_R3, value), (UC_ARM_REG_SP, STACK), (UC_ARM_REG_LR, STOP)):
        cpu.reg_write(reg, word)
    events, remaining, initialized_stack = [], list(replies), set()

    def instruction(engine, address, size, _):
        require(CODE <= address < CODE + len(code) and size == 4, "Unexpected executed code")
        require(address - CODE not in relocations, "Executed unresolved relocation")
        word = int.from_bytes(engine.mem_read(address, 4), "little")
        if word in (0xf57ff04e, 0xf57ff04f):
            events.append((ST if word == 0xf57ff04e else SY, 0, 0))

    def memory(engine, access, address, size, word, _):
        if MMIO <= address < MMIO + 0x1000:
            offset = address - MMIO
            require(size == 2, "Unexpected MMIO width")
            if access == uc.UC_MEM_READ:
                require(offset == 0x3fe and remaining, "Missing or unexpected MMIO read")
                engine.mem_write(address, remaining.pop(0).to_bytes(2, "little"))
                events.append((READ16, offset, 0))
            else:
                require(offset in (0x3fc, 0x3fe, 0x3e6), "Unexpected MMIO write")
                events.append((WRITE16, offset, word))
        elif RAM <= address < STACK:
            if address >= STACK - 0x1000:
                addresses = set(range(address, address + size))
                if access == uc.UC_MEM_WRITE:
                    initialized_stack.update(addresses)
                else:
                    require(addresses <= initialized_stack, "Read of unsupplied stack bytes")
            elif access == uc.UC_MEM_READ:
                require(address in fields and fields[address][0] == size, "Read of unsupplied context")
            else:
                require(address == PI + 0x104 and size == 2 and word == 0, "Unexpected context write")
        else:
            raise ValueError("Unexpected memory access")

    cpu.hook_add(uc.UC_HOOK_CODE, instruction)
    cpu.hook_add(uc.UC_HOOK_MEM_READ | uc.UC_HOOK_MEM_WRITE, memory)
    cpu.emu_start(CODE, STOP, timeout=1000000, count=1000)
    require(cpu.reg_read(UC_ARM_REG_PC) == STOP, "Function did not return within bound")
    require(not remaining, "Unconsumed ARM read replies")
    return events, cpu.reg_read(UC_ARM_REG_R0) & 0xffff, int.from_bytes(cpu.mem_read(PI + 0x104, 2), "little")


BRIDGE = r'''
#include "phy_register.h"
struct output { uint32_t result, bookkeeping, phase, error; uint64_t attempted, completed; };
int compare_phy(unsigned command, uint16_t selector, uint16_t mask, uint16_t value,
                uint16_t bookkeeping, axd_d11_io_fn io, struct output *out) {
    struct axd_phy_register job;
    const struct axd_phy_context ctx = {130,1,0xb,51,bookkeeping,{0,0},1,0};
    unsigned steps=0;
    if (axd_phy_register_begin(&job,(enum axd_phy_command)command,selector,mask,value,&ctx)) return -1;
    while (job.phase==AXD_PHY_RUNNING && steps++<200) axd_phy_register_step(&job,io,0);
    out->result=job.result; out->bookkeeping=job.context.bookkeeping;
    out->phase=job.phase; out->error=job.error;
    out->attempted=job.attempted_ops; out->completed=job.completed_ops;
    return job.phase==AXD_PHY_TRACE_COMPLETE ? 0 : -1;
}
'''


class Operation(ct.Structure):
    _fields_ = [("kind", ct.c_int), ("offset", ct.c_uint32), ("value", ct.c_uint32)]


class Output(ct.Structure):
    _fields_ = [(n, ct.c_uint32) for n in ("result", "bookkeeping", "phase", "error")] + [
        ("attempted", ct.c_uint64), ("completed", ct.c_uint64)]


CALLBACK = ct.CFUNCTYPE(ct.c_int, ct.c_void_p, ct.POINTER(Operation), ct.POINTER(ct.c_uint32))


def model(library, command, selector, mask, value, bookkeeping, replies):
    events, remaining, failures = [], list(replies), []

    @CALLBACK
    def io(_, operation, reply):
        op = operation.contents
        events.append((op.kind, op.offset, op.value))
        if op.kind == READ16:
            if not remaining:
                failures.append("Missing C read reply")
                return -1
            reply[0] = remaining.pop(0)
        return 0

    out = Output()
    result = library.compare_phy(command, selector, mask, value, bookkeeping, io, ct.byref(out))
    require(result == 0 and not failures and not remaining, "C model failed or read queue differs")
    require(out.attempted == out.completed == len(events), "C operation accounting differs")
    return events, out.result, out.bookkeeping


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    args = parser.parse_args()
    require(args.module.stat().st_size <= 64 * 1024 * 1024, "Input exceeds bound")
    data = args.module.read_bytes()
    require(hashlib.sha256(data).hexdigest() == MODULE_SHA256, "Unrecognized stock module")
    import unicorn
    require(unicorn.__version__ == "2.1.4", "Use pinned Unicorn 2.1.4")
    blobs = functions(data, {"phy_utils_read_phyreg", "phy_utils_mod_phyreg", "phy_ac_ana_reset"})
    rng = random.Random(0x13051)
    cases = [(1, s, 0, 0, [r]) for s, r in ((0, 0), (0xbfff, 0xffff), (0x645, 0xa53c))]
    cases += [(2, s, m, v, [] if s == 0xffff else [r]) for s, m, v, r in (
        (0xffff, 0xffff, 0xffff, 0), (0xc000, 0x3ff, 0xffff, 0xa000),
        (0xfffe, 0, 0xffff, 0x5555), (0x845, 0xffff, 0, 0xffff))]
    for _ in range(128):
        cases.append((1, rng.randrange(0xc000), 0, 0, [rng.randrange(0x10000)]))
        cases.append((2, rng.randrange(0x10000), rng.randrange(0x10000), rng.randrange(0x10000), []))
        if cases[-1][1] != 0xffff:
            cases[-1][-1].append(rng.randrange(0x10000))
    cases.append((3, 0, 0, 0, []))
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="axd-phy-arm-") as folder:
        folder = Path(folder)
        bridge = folder / "bridge.c"
        bridge.write_text(BRIDGE)
        path = folder / ("model.dylib" if sys.platform == "darwin" else "model.so")
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-fPIC",
                        "-dynamiclib" if sys.platform == "darwin" else "-shared", "-I", str(root / "driver"),
                        str(root / "driver/phy_register.c"), str(bridge), "-o", str(path)], check=True)
        library = ct.CDLL(str(path))
        library.compare_phy.argtypes = [ct.c_uint, ct.c_uint16, ct.c_uint16, ct.c_uint16, ct.c_uint16,
                                        CALLBACK, ct.POINTER(Output)]
        library.compare_phy.restype = ct.c_int
        for index, (cmd, selector, mask, value, replies) in enumerate(cases):
            name = {1: "phy_utils_read_phyreg", 2: "phy_utils_mod_phyreg", 3: "phy_ac_ana_reset"}[cmd]
            bookkeeping = rng.randrange(0x10000)
            actual = emulate(blobs[name], cmd, selector, mask, value, bookkeeping, replies)
            observed = model(library, cmd, selector, mask, value, bookkeeping, replies)
            require(actual[0] == observed[0] and actual[2] == observed[2], f"Trace/context mismatch in case {index}")
            if cmd == 1:
                require(actual[1] == observed[1], f"Read result mismatch in case {index}")
    print(f"PHY ARM differential: {len(cases)} cases match stock instructions, barriers, widths and bookkeeping.")
    print("Synthetic contexts/read replies only; no real MMIO, elapsed timing, PHY reset or RF execution.")


if __name__ == "__main__":
    main()
