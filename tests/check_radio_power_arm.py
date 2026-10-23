#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare real22-operation logen mode child with pinned ARM instructions.

Executes the actual mode/read/write/modify leaves with their exact reviewed
branch/MOVW/MOVT bindings, original private mode bytes, and strict synthetic
contexts/read queues. No helper stub, power-sequence emulation or hardware.
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
from check_radio_identity_arm import CALLBACK
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"scripts"))
from radio_power_inventory import inspect
from radio_firmware_inventory import MODULE_SHA256,require

CODE,RAM,MMIO,RO=0x1000000,0x2000000,0x3000000,0x4000000
PI,PUBLIC,SHARED,DPTR,DESC=RAM,RAM+0x1000,RAM+0x2000,RAM+0x3000,RAM+0x4000
STACK,STOP=RAM+0x10000,CODE+0x10000
NAMES=("phy_utils_read_radioreg","phy_utils_write_radioreg","phy_utils_mod_radioreg","wlc_phy_radio20704_sel_logen_mode")
MODE_CALLS=(0xcc,0xe8,0xfc,0x114,0x12c,0x144,0x160,0x17c,0x194,0x1b0,0x1c4,0x1dc,0x1f8,0x214,0x230,0x24c,0x264,0x280,0x294,0x2ac,0x2c8,0x2ec)


def emulate(leaves,mode_data,mode,bus,revision,counter,threshold,read_offset,replies):
    import unicorn as uc
    from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC
    cpu=uc.Uc(uc.UC_ARCH_ARM,uc.UC_MODE_ARM|uc.UC_MODE_LITTLE_ENDIAN)
    cpu.mem_map(CODE,0x4000);cpu.mem_map(RAM,0x10000);cpu.mem_map(MMIO,0x1000);cpu.mem_map(RO,0x1000)
    ranges=[]
    for number,name in enumerate(NAMES):
        raw,relocations=leaves[name];raw=bytearray(raw);base=CODE+number*0x1000
        expected={0x14,0x34} if number==2 else {0,4,*MODE_CALLS} if number==3 else set()
        require(relocations==expected,"Unreviewed logen relocation set")
        bindings={0x14:CODE,0x34:CODE+0x1000} if number==2 else {off:CODE+0x2000 for off in MODE_CALLS} if number==3 else {}
        for off,target in bindings.items():
            word=struct.unpack_from("<I",raw,off)[0];opcode=0xea if off==(0x34 if number==2 else 0x2ec) else 0xeb
            require(word>>24==opcode and word&0xffffff==0xfffffe,"Unreviewed branch opcode/addend")
            struct.pack_into("<I",raw,off,(opcode<<24)|(((target-base-off-8)//4)&0xffffff))
        if number==3:
            for off,value,opcode in ((0,RO&0xffff,0xe3000000),(4,RO>>16,0xe3400000)):
                word=struct.unpack_from("<I",raw,off)[0]
                require(word&0xfff00000==opcode and not(((word>>4)&0xf000)|(word&0xfff)),"Unreviewed MOVW/MOVT")
                struct.pack_into("<I",raw,off,(word&~0xf0fff)|((value&0xf000)<<4)|(value&0xfff))
        cpu.mem_write(base,bytes(raw));ranges.append((base,base+len(raw)))
    fields={PI+4:(4,PUBLIC),PI+8:(4,SHARED),PI+0xc4:(4,MMIO),PI+0xc8:(4,DPTR),PI+0x104:(2,counter),PI+0x106:(2,threshold),
        PUBLIC:(4,0xb),PUBLIC+0xe:(2,read_offset),SHARED+0xc:(4,bus),SHARED+0x10:(4,revision),DPTR:(4,DESC),
        DESC+0x1a:(2,0x120),DESC+0x88:(2,0x3d8),DESC+0x8a:(2,0x3da),DESC+0x8e:(2,0x3e0)}
    for offset,width in ((0,2),(2,1),(4,4),(8,2),(12,4),(16,2)):
        fields[RO+0x64+offset]=(width,int.from_bytes(mode_data[offset:offset+width],"little"))
    for address,(width,value) in fields.items():cpu.mem_write(address,value.to_bytes(width,"little"))
    for register,value in ((UC_ARM_REG_R0,PI),(UC_ARM_REG_R1,mode),(UC_ARM_REG_SP,STACK),(UC_ARM_REG_LR,STOP)):cpu.reg_write(register,value)
    events,remaining,stack=[],list(replies),set()
    def instruction(engine,address,size,_):
        require(size==4 and any(lo<=address<hi for lo,hi in ranges),"Unbound mode instruction")
        word=int.from_bytes(engine.mem_read(address,4),"little")
        if word in (0xf57ff04e,0xf57ff04f):events.append((1 if word==0xf57ff04e else 2,0,0))
    def memory(engine,access,address,width,value,_):
        if MMIO<=address<MMIO+0x1000:
            offset=address-MMIO
            if access==uc.UC_MEM_READ:
                kind=4 if width==4 else 5
                require(width in (2,4) and remaining and remaining[0][:2]==(kind,offset),"Missing/unexpected mode read")
                engine.mem_write(address,remaining.pop(0)[2].to_bytes(width,"little"));events.append((kind,offset,0))
            else:
                require(width==2 and offset in (0x3d8,0x3da),"Unexpected mode write");events.append((7,offset,value))
        elif STACK-0x1000<=address<STACK:
            touched=set(range(address,address+width))
            if access==uc.UC_MEM_WRITE:stack.update(touched)
            else:require(touched<=stack,"Uninitialized mode stack read")
        elif access==uc.UC_MEM_READ:require(address in fields and fields[address][0]==width,"Uninitialized mode/context read")
        else:require(address==PI+0x104 and width==2,"Unexpected mode context write")
    cpu.hook_add(uc.UC_HOOK_CODE,instruction);cpu.hook_add(uc.UC_HOOK_MEM_READ|uc.UC_HOOK_MEM_WRITE,memory)
    cpu.emu_start(CODE+0x3000,STOP,count=20000)
    require(cpu.reg_read(UC_ARM_REG_PC)==STOP and not remaining,"Mode child/queue did not return")
    return events,int.from_bytes(cpu.mem_read(PI+0x104,2),"little")


BRIDGE=r'''
#include "radio_power.h"
struct output {uint32_t counter,done,phase,error;uint64_t attempted,completed;};
int compare_mode(unsigned mode,uint32_t bus,uint32_t revision,uint16_t counter,uint16_t threshold,
                 uint16_t read_offset,const uint8_t *data,axd_d11_io_fn io,struct output *out) {
 struct axd_radio_power j;struct axd_radio_power_context c={0};unsigned steps=0;
 c.radio.hardware.phy=(struct axd_phy_context){.core=130,.minor=1,.phy_type=0xb,.phy_revision=51,.phy_core_count=2,.bookkeeping=counter};
 c.radio.hardware.shared_bus_word=bus;c.radio.hardware.write_threshold=threshold;
 c.radio.phy_pointer=(struct axd_wake_u32){0x1000,1};c.radio.public_pointer=(struct axd_wake_u32){0x2000,1};c.radio.shared_pointer=(struct axd_wake_u32){0x3000,1};
 c.radio.radio_read_offset=(struct axd_wake_u32){read_offset,1};c.radio.shared_bus_revision=(struct axd_wake_u32){revision,1};c.mode_data=data;c.mode_length=18;
 if(axd_radio_power_begin(&j,AXD_RPOWER_LOGEN,mode,&c))return -1;
 while(j.phase==AXD_RPOWER_RUNNING && steps++<10000)axd_radio_power_step(&j,io,0);
 out->counter=j.context.radio.hardware.phy.bookkeeping;out->done=j.logen_completed;out->phase=j.phase;out->error=j.error;out->attempted=j.attempted_ops;out->completed=j.completed_ops;
 return j.phase==AXD_RPOWER_TRACE_COMPLETE?0:-1;
}
'''


class Output(ct.Structure):
    _fields_=[(n,ct.c_uint32) for n in ("counter","done","phase","error")]+[("attempted",ct.c_uint64),("completed",ct.c_uint64)]


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("module",type=Path);args=parser.parse_args()
    require(args.module.stat().st_size<=64*1024*1024,"Oversized module")
    data=args.module.read_bytes();require(hashlib.sha256(data).hexdigest()==MODULE_SHA256,"Unrecognized module")
    import unicorn
    require(unicorn.__version__=="2.1.4","Use pinned Unicorn2.1.4")
    _,spans=inspect(data);private=spans["logen-mode"];leaves=functions(data,set(NAMES));rng=random.Random(0x2070451)
    cases=[(mode,bus,revision,0xffff,threshold,0x8000) for mode in range(3) for bus,revision,threshold in ((0,0,0),(1,4,0),(1,4,1),(1,4,2),(2,3,0),(2,4,0))]
    cases += [(rng.randrange(3),rng.randrange(4),rng.randrange(6),rng.randrange(0x10000),rng.randrange(0x10000),rng.randrange(0x10000)) for _ in range(128)]
    root=Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="axd-radio-power-arm-") as folder:
        folder=Path(folder);bridge=folder/"bridge.c";bridge.write_text(BRIDGE);libpath=folder/("model.dylib" if sys.platform=="darwin" else "model.so")
        subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror","-pedantic","-fPIC","-dynamiclib" if sys.platform=="darwin" else "-shared","-I",str(root/"driver"),*map(str,sorted((root/"driver").glob("*.c"))),str(bridge),"-o",str(libpath)],check=True)
        library=ct.CDLL(str(libpath));library.compare_mode.restype=ct.c_int
        library.compare_mode.argtypes=[ct.c_uint,ct.c_uint32,ct.c_uint32,ct.c_uint16,ct.c_uint16,ct.c_uint16,ct.POINTER(ct.c_uint8),CALLBACK,ct.POINTER(Output)]
        mode_bytes=(ct.c_uint8*18).from_buffer_copy(private)
        for case in cases:
            mode,bus,revision,counter,threshold,read_offset=case;replies=[]
            for _ in range(22):
                replies.append((5,0x3da,rng.randrange(0x10000)))
                if bus==1 and threshold<=1:replies.append((4,0x120,rng.randrange(0x100000000)))
                if bus==2 and revision<=3:replies.append((5,0x3e0,rng.randrange(0x10000)))
            expected,final_counter=emulate(leaves,private,*case,replies);events,remaining,errors=[],list(replies),[]
            @CALLBACK
            def io(_,pointer,reply):
                op=pointer.contents;event=(op.kind,op.offset,op.value)
                if len(events)>=len(expected) or event!=expected[len(events)]:errors.append("Mode trace mismatch");return -1
                events.append(event)
                if op.kind in (4,5):
                    if not remaining or remaining[0][:2]!=(op.kind,op.offset):errors.append("Mode read mismatch");return -1
                    reply[0]=remaining.pop(0)[2]
                return 0
            out=Output();require(library.compare_mode(*case,mode_bytes,io,ct.byref(out))==0,"C mode child failed")
            require(not errors and not remaining and events==expected,"Mode event/read mismatch")
            require(out.counter==final_counter and out.done==1 and out.attempted==out.completed==len(expected),"Mode state/accounting mismatch")
    print(f"Radio power ARM: {len(cases)} complete real22-modification mode traces matched; private payload not emitted.")
    print("Actual mode/read/write/modify instructions, reviewed bindings and synthetic replies only; no power-sequence or hardware execution.")


if __name__=="__main__":main()
