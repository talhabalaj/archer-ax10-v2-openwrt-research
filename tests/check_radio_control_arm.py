#!/usr/bin/env python3
"""Compare real radio register leaves with pinned stock ARM instructions.

Caller supplies wl.ko and Unicorn2.1.4. Only selected instruction bytes and
synthetic contexts/read queues execute in an isolated emulator, never hardware.
The two MODIFY branch relocations bind only the exact selected read/write
symbols. This check does not execute radio power sequences or private tables.
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
from check_radio_identity_arm import CALLBACK, Operation
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from radio_firmware_inventory import MODULE_SHA256, require

CODE, RAM, MMIO = 0x1000000, 0x2000000, 0x3000000
PI, PUBLIC, SHARED, DPTR, DESC = RAM, RAM+0x1000, RAM+0x2000, RAM+0x3000, RAM+0x4000
STACK, STOP = RAM+0x10000, CODE+0x10000
NAMES = ("phy_utils_read_radioreg", "phy_utils_write_radioreg", "phy_utils_mod_radioreg")


def emulate(leaves, command, selector, mask, value, bus, revision, counter, threshold, read_offset, replies):
    import unicorn as uc
    from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3
    from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_SP, UC_ARM_REG_PC
    cpu = uc.Uc(uc.UC_ARCH_ARM, uc.UC_MODE_ARM | uc.UC_MODE_LITTLE_ENDIAN)
    cpu.mem_map(CODE, 0x3000);cpu.mem_map(RAM, 0x10000);cpu.mem_map(MMIO, 0x1000)
    ranges = []
    for i, name in enumerate(NAMES):
        raw, relocations = leaves[name];raw = bytearray(raw);base=CODE+i*0x1000
        require(relocations == ({0x14, 0x34} if i == 2 else set()), "Unexpected radio relocation set")
        if i == 2:
            for off, target, opcode in ((0x14, CODE, 0xeb), (0x34, CODE+0x1000, 0xea)):
                word=struct.unpack_from("<I",raw,off)[0]
                require(word>>24 == opcode, "Unexpected radio call relocation opcode")
                struct.pack_into("<I",raw,off,(opcode<<24)|(((target-base-off-8)//4)&0xffffff))
        cpu.mem_write(base, bytes(raw));ranges.append((base,base+len(raw)))
    fields = {PI+4:(4,PUBLIC),PI+8:(4,SHARED),PI+0xc4:(4,MMIO),PI+0xc8:(4,DPTR),
        PI+0x104:(2,counter),PI+0x106:(2,threshold),PUBLIC:(4,0xb),PUBLIC+0xe:(2,read_offset),
        SHARED+0xc:(4,bus),SHARED+0x10:(4,revision),DPTR:(4,DESC),
        DESC+0x1a:(2,0x120),DESC+0x88:(2,0x3d8),DESC+0x8a:(2,0x3da),DESC+0x8e:(2,0x3e0)}
    for address,(width,word) in fields.items():cpu.mem_write(address,word.to_bytes(width,"little"))
    entry=CODE+(command-5)*0x1000
    for reg,word in ((UC_ARM_REG_R0,PI),(UC_ARM_REG_R1,selector),
        (UC_ARM_REG_R2,value if command==6 else mask),(UC_ARM_REG_R3,value),
        (UC_ARM_REG_SP,STACK),(UC_ARM_REG_LR,STOP)):cpu.reg_write(reg,word)
    events,remaining,stack=[],list(replies),set()

    def instruction(engine,address,size,_):
        require(size==4 and any(lo<=address<hi for lo,hi in ranges),"Unexpected radio execution")
        word=int.from_bytes(engine.mem_read(address,4),"little")
        if word in (0xf57ff04e,0xf57ff04f):events.append((1 if word==0xf57ff04e else 2,0,0))

    def memory(engine,access,address,size,word,_):
        if MMIO<=address<MMIO+0x1000:
            offset=address-MMIO
            if access==uc.UC_MEM_READ:
                kind=4 if size==4 else 5
                require(size in (2,4) and remaining and remaining[0][:2]==(kind,offset),"Unexpected ARM radio read")
                supplied=remaining.pop(0)[2]&((1<<(size*8))-1)
                engine.mem_write(address,supplied.to_bytes(size,"little"));events.append((kind,offset,0))
            else:
                require(size==2 and offset in (0x3d8,0x3da),"Unexpected ARM radio write")
                events.append((7,offset,word))
        elif STACK-0x1000<=address<STACK:
            region=set(range(address,address+size))
            if access==uc.UC_MEM_WRITE:stack.update(region)
            else:require(region<=stack,"Uninitialized radio stack read")
        elif RAM<=address<STACK-0x1000:
            if access==uc.UC_MEM_READ:require(address in fields and fields[address][0]==size,"Unsupplied radio context read")
            else:require(address==PI+0x104 and size==2,"Unexpected radio context write")
        else:raise ValueError("Unexpected radio memory region")
    cpu.hook_add(uc.UC_HOOK_CODE,instruction);cpu.hook_add(uc.UC_HOOK_MEM_READ|uc.UC_HOOK_MEM_WRITE,memory)
    cpu.emu_start(entry,STOP,timeout=1000000,count=1500)
    require(cpu.reg_read(UC_ARM_REG_PC)==STOP and not remaining,"Radio leaf did not consume queue and return")
    return events,cpu.reg_read(UC_ARM_REG_R0)&0xffff,int.from_bytes(cpu.mem_read(PI+0x104,2),"little")


BRIDGE = r'''
#include "radio_control.h"
struct output {uint32_t result,counter,phase,error;uint64_t attempted,completed;};
int compare_radio(unsigned command,uint16_t selector,uint16_t mask,uint16_t value,
                  uint32_t bus,uint32_t revision,uint16_t counter,uint16_t threshold,
                  uint16_t read_offset,axd_d11_io_fn io,struct output *out) {
    struct axd_radio_control j;struct axd_radio_control_context c={0};unsigned steps=0;
    c.hardware.phy=(struct axd_phy_context){.core=130,.minor=1,.phy_type=0xb,.phy_revision=51,.phy_core_count=2,.bookkeeping=counter};
    c.hardware.shared_bus_word=bus;c.hardware.write_threshold=threshold;
    c.phy_pointer=(struct axd_wake_u32){0x1000,1};c.public_pointer=(struct axd_wake_u32){0x2000,1};
    c.shared_pointer=(struct axd_wake_u32){0x3000,1};c.radio_read_offset=(struct axd_wake_u32){read_offset,1};
    c.shared_bus_revision=(struct axd_wake_u32){revision,1};
    if(axd_radio_register_begin(&j,(enum axd_radio_control_command)command,selector,mask,value,&c))return -1;
    while(j.phase==AXD_RADIO_CTL_RUNNING && steps++<1000)axd_radio_control_step(&j,io,0);
    out->result=j.result;out->counter=j.context.hardware.phy.bookkeeping;out->phase=j.phase;out->error=j.error;
    out->attempted=j.attempted_ops;out->completed=j.completed_ops;
    return j.phase==AXD_RADIO_CTL_TRACE_COMPLETE?0:-1;
}
'''


class Output(ct.Structure):
    _fields_=[(n,ct.c_uint32) for n in ("result","counter","phase","error")]+[("attempted",ct.c_uint64),("completed",ct.c_uint64)]


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("module",type=Path);args=parser.parse_args()
    require(args.module.stat().st_size<=64*1024*1024,"Oversized module")
    data=args.module.read_bytes();require(hashlib.sha256(data).hexdigest()==MODULE_SHA256,"Unrecognized module digest")
    import unicorn
    require(unicorn.__version__=="2.1.4","Use pinned Unicorn2.1.4")
    leaves=functions(data,set(NAMES));root=Path(__file__).resolve().parents[1];rng=random.Random(0x6317851)
    cases=[]
    for command in (5,6,7):
        for selector in (0,1,0x88b,0xffff):
            for bus,revision,counter,threshold in ((0,0,42,0),(1,4,42,43),(1,4,0xffff,1),(1,4,0,0),(2,3,42,0),(2,4,42,0)):
                cases.append((command,selector,0x3f,1,bus,revision,counter,threshold,0x8000))
    for _ in range(128):cases.append((rng.choice((5,6,7)),rng.randrange(0x10000),rng.randrange(0x10000),rng.randrange(0x10000),rng.randrange(4),rng.randrange(6),rng.randrange(0x10000),rng.randrange(0x10000),rng.randrange(0x10000)))
    with tempfile.TemporaryDirectory(prefix="axd-radio-control-arm-") as folder:
        folder=Path(folder);bridge=folder/"bridge.c";bridge.write_text(BRIDGE);libpath=folder/("model.dylib" if sys.platform=="darwin" else "model.so")
        subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror","-pedantic","-fPIC","-dynamiclib" if sys.platform=="darwin" else "-shared","-I",str(root/"driver"),*map(str,sorted((root/"driver").glob("*.c"))),str(bridge),"-o",str(libpath)],check=True)
        library=ct.CDLL(str(libpath));library.compare_radio.restype=ct.c_int
        library.compare_radio.argtypes=[ct.c_uint,ct.c_uint16,ct.c_uint16,ct.c_uint16,ct.c_uint32,ct.c_uint32,ct.c_uint16,ct.c_uint16,ct.c_uint16,CALLBACK,ct.POINTER(Output)]
        for case in cases:
            command,selector,mask,value,bus,revision,counter,threshold,read_offset=case
            replies=[]
            if command!=6 and selector!=0xffff:replies.append((5,0x3da,rng.randrange(0x10000)))
            after_read=0 if command!=6 and selector!=0xffff else counter
            if command!=5 and bus==1 and ((after_read+1)&0xffff)>=threshold:replies.append((4,0x120,rng.randrange(0x100000000)))
            if command!=5 and selector!=0xffff and bus==2 and revision<=3:replies.append((5,0x3e0,rng.randrange(0x10000)))
            expected,result,final_counter=emulate(leaves,*case,replies)
            events,remaining,errors=[],list(replies),[]
            @CALLBACK
            def io(_,pointer,reply):
                op=pointer.contents;event=(op.kind,op.offset,op.value)
                if len(events)>=len(expected) or event!=expected[len(events)]:errors.append("C trace mismatch");return -1
                events.append(event)
                if op.kind in (4,5):
                    if not remaining or remaining[0][:2]!=(op.kind,op.offset):errors.append("C read mismatch");return -1
                    reply[0]=remaining.pop(0)[2]
                return 0
            out=Output();c_mask=0 if command in (5,6) else mask;c_value=0 if command==5 else value
            require(library.compare_radio(command,selector,c_mask,c_value,bus,revision,counter,threshold,read_offset,io,ct.byref(out))==0,"Radio C leaf failed")
            require(not errors and not remaining and events==expected,"Radio trace/read queue mismatch")
            require(out.counter==final_counter and out.attempted==out.completed==len(expected),"Radio software state/accounting mismatch")
            if command==5:require(out.result==result,"Radio read result mismatch")
    print(f"Radio control ARM: {len(cases)} real read/write/modify traces and software counters matched.")
    print("Selected stock leaves, exact two MODIFY branch bindings, synthetic contexts/replies; no power sequence or hardware execution.")


if __name__=="__main__":main()
