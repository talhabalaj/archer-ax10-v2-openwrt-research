#!/usr/bin/env python3
"""Compare three closed radio identity leaves with pinned stock ARM instructions.

Requires caller-supplied pinned wl.ko and Unicorn2.1.4. Only synthetic contexts
and strict read replies are used. No target process, hardware, firmware export
or RF operation occurs. The registered thunk is covered by the C suite; its
unresolved branch relocation is deliberately outside this ARM leaf comparison.
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

from check_phy_arm import functions

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from radio_firmware_inventory import MODULE_SHA256, require

CODE, RAM, MMIO = 0x1000000, 0x2000000, 0x3000000
PI, PUBLIC, DESC_PTR, DESC = RAM, RAM + 0x1000, RAM + 0x2000, RAM + 0x3000
STACK, STOP = RAM + 0x10000, CODE + 0x10000
ST, SY, W16, R16 = 1, 2, 7, 5


def emulate(function, value, replies, query):
    import unicorn as uc
    from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC
    code, relocations = function
    require(not relocations, "Selected radio leaf has unresolved relocations")
    cpu = uc.Uc(uc.UC_ARCH_ARM, uc.UC_MODE_ARM | uc.UC_MODE_LITTLE_ENDIAN)
    cpu.mem_map(CODE, 0x1000)
    cpu.mem_map(RAM, 0x10000)
    cpu.mem_map(MMIO, 0x1000)
    cpu.mem_write(CODE, code)
    fields = {PI + 4: (4, PUBLIC), PI + 0xc4: (4, MMIO), PI + 0xc8: (4, DESC_PTR),
              DESC_PTR: (4, DESC), DESC + 0x88: (2, 0x3d8), DESC + 0x8a: (2, 0x3da)}
    for address, (width, word) in fields.items():
        cpu.mem_write(address, word.to_bytes(width, "little"))
    cpu.mem_write(PUBLIC + 0xa, bytes.fromhex("11112233"))
    for reg, word in ((UC_ARM_REG_R0, PI), (UC_ARM_REG_R1, value),
                      (UC_ARM_REG_SP, STACK), (UC_ARM_REG_LR, STOP)):
        cpu.reg_write(reg, word)
    events, remaining, initialized_stack = [], list(replies), set()
    decoded = []

    def instruction(engine, address, size, _):
        require(CODE <= address < CODE + len(code) and size == 4, "Unexpected executed radio code")
        word = int.from_bytes(engine.mem_read(address, 4), "little")
        if word in (0xf57ff04e, 0xf57ff04f):
            events.append((ST if word == 0xf57ff04e else SY, 0, 0))

    def memory(engine, access, address, size, word, _):
        if MMIO <= address < MMIO + 0x1000:
            require(query and size == 2, "Unexpected radio MMIO width/path")
            offset = address - MMIO
            if access == uc.UC_MEM_READ:
                require(offset == 0x3da and remaining, "Missing or unexpected radio read reply")
                engine.mem_write(address, remaining.pop(0).to_bytes(2, "little"))
                events.append((R16, offset, 0))
            else:
                require(offset == 0x3d8 and word in (0, 1), "Unexpected radio address write")
                events.append((W16, offset, word))
        elif RAM <= address < STACK:
            if address >= STACK - 0x1000:
                region = set(range(address, address + size))
                if access == uc.UC_MEM_WRITE:
                    initialized_stack.update(region)
                else:
                    require(region <= initialized_stack, "Read of unsupplied radio stack bytes")
            elif access == uc.UC_MEM_READ:
                require(address in fields and fields[address][0] == size, "Read of unsupplied radio context")
            else:
                require(not query and (address - PUBLIC, size) in ((0xa, 2), (0xc, 1), (0xd, 1)),
                        "Unexpected radio context write")
                decoded.append((address - PUBLIC, size, word))
        else:
            raise ValueError("Unexpected radio memory access")

    cpu.hook_add(uc.UC_HOOK_CODE, instruction)
    cpu.hook_add(uc.UC_HOOK_MEM_READ | uc.UC_HOOK_MEM_WRITE, memory)
    cpu.emu_start(CODE, STOP, timeout=1000000, count=1000)
    require(cpu.reg_read(UC_ARM_REG_PC) == STOP, "Radio leaf did not return within bound")
    require(not remaining, "Unconsumed stock radio replies")
    fields_after = struct.unpack("<HBB", cpu.mem_read(PUBLIC + 0xa, 4))
    return events, cpu.reg_read(UC_ARM_REG_R0), decoded, fields_after


BRIDGE = r'''
#include "radio_identity.h"
struct output {uint32_t code,id,revision,version,stores,phase,error;uint64_t attempted,completed;};
int compare_identity(unsigned command,uint32_t code,axd_d11_io_fn io,struct output *out) {
    struct axd_radio_identity job;
    struct axd_radio_identity_context c={0};
    unsigned steps=0;
    c.core=130;c.minor=1;c.phy_pointer=(struct axd_wake_u32){0x1000,1};
    c.public_pointer=(struct axd_wake_u32){0x2000,1};
    c.radio_id=0x1111;c.radio_revision=0x22;c.radio_version=0x33;
    if(axd_radio_identity_begin(&job,(enum axd_radio_identity_command)command,code,&c))return -1;
    while(job.phase==AXD_RID_RUNNING && steps++<100)axd_radio_identity_step(&job,io,0);
    out->code=job.result_code;out->id=job.context.radio_id;
    out->revision=job.context.radio_revision;out->version=job.context.radio_version;
    out->stores=job.decoded_stores;out->phase=job.phase;out->error=job.error;
    out->attempted=job.attempted_ops;out->completed=job.completed_ops;
    return job.phase==AXD_RID_TRACE_COMPLETE?0:-1;
}
'''


class Operation(ct.Structure):
    _fields_ = [("kind", ct.c_int), ("offset", ct.c_uint32), ("value", ct.c_uint32)]


class Output(ct.Structure):
    _fields_ = [(name, ct.c_uint32) for name in ("code", "id", "revision", "version", "stores", "phase", "error")]
    _fields_ += [("attempted", ct.c_uint64), ("completed", ct.c_uint64)]


CALLBACK = ct.CFUNCTYPE(ct.c_int, ct.c_void_p, ct.POINTER(Operation), ct.POINTER(ct.c_uint32))


def model(library, command, value, replies, expected):
    events, remaining, errors = [], list(replies), []

    @CALLBACK
    def io(_, pointer, reply):
        op = pointer.contents
        event = (op.kind, op.offset, op.value)
        if len(events) >= len(expected) or event != expected[len(events)]:
            errors.append("Radio trace mismatch")
            return -1
        events.append(event)
        if op.kind == R16:
            if not remaining:
                errors.append("Missing C radio read reply")
                return -1
            reply[0] = remaining.pop(0)
        return 0

    result = Output()
    require(library.compare_identity(command, value, io, ct.byref(result)) == 0,
            "Radio C model failed")
    require(not errors and not remaining and events == expected, "Radio model/read queue mismatch")
    require(result.attempted == result.completed == len(expected) and result.phase == 2 and result.error == 0,
            "Radio operation accounting differs")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    args = parser.parse_args()
    require(args.module.stat().st_size <= 64 * 1024 * 1024, "Input exceeds size bound")
    data = args.module.read_bytes()
    require(hashlib.sha256(data).hexdigest() == MODULE_SHA256, "Unrecognized stock module")
    import unicorn
    require(unicorn.__version__ == "2.1.4", "Use pinned Unicorn2.1.4")
    leaves = functions(data, {"phy_ac_radio_query_idcode", "phy_ac_radio_parse_idcode", "phy_utils_parse_idcode"})
    root = Path(__file__).resolve().parents[1]
    rng = random.Random(0x13051a)
    samples = [(0, 0), (0xffff, 0xffff), (0xabcd, 0x5678), (0, 0xffff)]
    samples += [(rng.randrange(0x10000), rng.randrange(0x10000)) for _ in range(64)]
    codes = [0, 0xffffffff, 0xabcd5678, 0x00010000] + [rng.randrange(0x100000000) for _ in range(128)]
    with tempfile.TemporaryDirectory(prefix="axd-radio-arm-") as folder:
        folder = Path(folder)
        bridge = folder / "bridge.c"
        bridge.write_text(BRIDGE)
        library_path = folder / ("model.dylib" if sys.platform == "darwin" else "model.so")
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-fPIC",
                        "-dynamiclib" if sys.platform == "darwin" else "-shared", "-I", str(root / "driver"),
                        str(root / "driver/radio_identity.c"), str(bridge), "-o", str(library_path)], check=True)
        library = ct.CDLL(str(library_path))
        library.compare_identity.argtypes = [ct.c_uint, ct.c_uint32, CALLBACK, ct.POINTER(Output)]
        library.compare_identity.restype = ct.c_int
        for high, low in samples:
            expected, returned, writes, fields = emulate(leaves["phy_ac_radio_query_idcode"], 0, [high, low], True)
            require(not writes and fields == (0x1111, 0x22, 0x33), "Query changed public identity")
            result = model(library, 1, 0, [high, low], expected)
            require(result.code == returned, "High-first stock radio result differs")
            require((result.id, result.revision, result.version) == fields and result.stores == 0,
                    "Query model changed public identity")
        for name, command in (("phy_ac_radio_parse_idcode", 4), ("phy_utils_parse_idcode", 5)):
            for value in codes:
                expected, _, writes, fields = emulate(leaves[name], value, [], False)
                require(not expected and [item[:2] for item in writes] == [(0xa, 2), (0xc, 1), (0xd, 1)],
                        "Stock parser store width/order differs")
                result = model(library, command, value, [], [])
                require((result.id, result.revision, result.version) == fields and result.stores == 7,
                        "Stock parser fields differ")
    print(f"Radio identity ARM: {len(samples)} query traces and {len(codes) * 2} AC/common decodes matched.")
    print("Actual selected stock instructions only; synthetic contexts/replies, no registered-thunk relocation or hardware execution.")


if __name__ == "__main__":
    main()
