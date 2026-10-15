#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check the actual closed RX-idle child used on initial RX-fill allocation failure.

The full fill body calls imported OS allocation/account/pull/trim/address services;
this checker does not replace them in ARM or claim whole-fill ARM execution. Only
the selected 84-byte zero-call child executes with strict synthetic memory and
fresh channel reads. Its actual observations/return compare with the portable
fill failure path. No real MMIO, router, driver loading or external ARM stub.
"""
import argparse
import ctypes as ct
import hashlib
import importlib.util
import itertools
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
MODULE_SHA256='8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c'
FUNCTION_SHA256='7d506ff07c05267279dc4402613f1bafe64818a3f5c660bfbf03e49a28d10e56'
CODE,DI,CHANNEL,STACK=0x10000000,0x20000000,0x21000000,0x30008000
STOP=CODE+0x1000

def require(ok,message):
    if not ok:raise ValueError(message)

def emulate(code,mask,replies,deny=None):
    import unicorn as uc
    from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC
    require(uc.__version__=='2.1.4','Use pinned Unicorn2.1.4')
    require(len(code)==84 and hashlib.sha256(code).hexdigest()==FUNCTION_SHA256,'Modified selected child')
    cpu=uc.Uc(uc.UC_ARCH_ARM,uc.UC_MODE_ARM|uc.UC_MODE_LITTLE_ENDIAN)
    for address in [CODE,DI,CHANNEL,STACK-0x8000]:cpu.mem_map(address,0x10000)
    cpu.mem_write(CODE,code);fields={DI+0x4c:CHANNEL,DI+0xe4:mask}
    if deny=='unknown-mask':del fields[DI+0xe4]
    for address,value in fields.items():cpu.mem_write(address,value.to_bytes(4,'little'))
    cpu.reg_write(UC_ARM_REG_R0,DI);cpu.reg_write(UC_ARM_REG_SP,STACK);cpu.reg_write(UC_ARM_REG_LR,STOP)
    remaining=list(replies);stack=set();events=[];instructions=[]
    def instruction(engine,address,width,_):
        require(CODE<=address<CODE+len(code) and width==4,'Unknown instruction')
        word=int.from_bytes(engine.mem_read(address,4),'little')
        require(not((word>>25)&7==5 and word&(1<<24)),'Unexpected CALL')
        if word==0xf57ff04f:events.append((9,8,0x1111,0,0,0))
        instructions.append(address)
    def memory(engine,access,address,width,value,_):
        if CHANNEL<=address<CHANNEL+0x10000:
            require(access==uc.UC_MEM_READ and width==4 and remaining and address==CHANNEL+remaining[0][0],'Missing/unexpected channel read')
            off,reply=remaining.pop(0);engine.mem_write(address,reply.to_bytes(4,'little'));events.append((6,8,0x1111,off,0,0))
        elif STACK-8<=address<STACK:
            touched=set(range(address,address+width))
            if access==uc.UC_MEM_WRITE:stack.update(touched)
            else:require(touched<=stack,'Uninitialized stack read')
        else:require(access==uc.UC_MEM_READ and width==4 and address in fields,'Unknown context read/write')
    cpu.hook_add(uc.UC_HOOK_CODE,instruction);cpu.hook_add(uc.UC_HOOK_MEM_READ|uc.UC_HOOK_MEM_WRITE,memory)
    cpu.emu_start(CODE,STOP,timeout=1000000,count=1000)
    require(cpu.reg_read(UC_ARM_REG_PC)==STOP and not remaining,'Idle child/read queue incomplete')
    return events,cpu.reg_read(UC_ARM_REG_R0),len(instructions)

BRIDGE=r'''
#include "dma_rx_fill.h"
int compare_idle(uint32_t mask,axd_rx_fill_io_fn io,uint32_t *out) {
 struct axd_rx_fill_context c={0};struct axd_dma_rx_fill j;unsigned steps=0;
 c.rx.ring=(struct axd_dma64_ring){8,mask,0x100,6,7};c.rx.core=130;c.rx.status_owned=1;c.rx.status_identity=0x1111;
 c.receive_posts=2;c.buffer_size=64;c.os_owned=1;c.allocator=(struct axd_wake_u32){0x5555,1};
 if(axd_dma_rx_fill_begin(&j,&c))return -1;
 while(j.phase==AXD_RXF_RUNNING && steps++<1000)axd_dma_rx_fill_step(&j,io,0);
 out[0]=j.return_value;out[1]=j.status_sample;out[2]=j.producer_sample;out[3]=j.context.no_buffer_count;
 out[4]=(uint32_t)j.attempted_ops;out[5]=(uint32_t)j.completed_ops;
 return j.phase==AXD_RXF_TRACE_COMPLETE && j.return_valid?0:-1;
}
'''
class Operation(ct.Structure):_fields_=[('kind',ct.c_int),('resource',ct.c_int),('identity',ct.c_uint32),('offset',ct.c_uint32),('value',ct.c_uint32),('argument',ct.c_uint32)]
CALLBACK=ct.CFUNCTYPE(ct.c_int,ct.c_void_p,ct.POINTER(Operation),ct.POINTER(ct.c_uint32))
def model(lib,mask,replies):
    queue=list(replies);events=[];errors=[];out=(ct.c_uint32*6)();allocated=[False];published=[0]
    @CALLBACK
    def io(_,pointer,reply):
        op=pointer.contents
        if op.kind==1:
            if allocated[0] or(op.resource,op.identity,op.value,op.argument)!=(1,0x5555,64,0):errors.append('Unexpected allocation request');return -1
            allocated[0]=True;reply[0]=0
        elif op.kind in(6,9):
            events.append((op.kind,op.resource,op.identity,op.offset,op.value,op.argument))
            if op.kind==6:
                if not queue or op.offset!=queue[0][0]:errors.append('Missing fresh C read');return -1
                reply[0]=queue.pop(0)[1]
        elif op.kind==8:
            if queue or published[0]:errors.append('Invalid publication barrier');return -1
            published[0]=1
        elif op.kind==7:
            if published[0]!=1 or(op.resource,op.identity,op.offset,op.value)!=(8,0x1111,4,0x170):errors.append('Invalid publication store');return -1
            published[0]=2
        else:errors.append('Unexpected C operation');return -1
        return 0
    result=lib.compare_idle(mask,io,out)
    require(result==0 and not errors and not queue and published[0]==2,'Portable fill failure path failed')
    require(tuple(out[1:])==(replies[0][1],replies[1][1],1,7,7),'Portable samples/counter/accounting differs')
    return events,1-out[0]

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('module',type=Path);args=parser.parse_args()
    root=Path(__file__).resolve().parents[1];data=args.module.read_bytes();require(hashlib.sha256(data).hexdigest()==MODULE_SHA256,'Unrecognized module')
    spec=importlib.util.spec_from_file_location('fill_idle_elf',root/'tests/check_phy_arm.py');helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
    code,relocations=helper.functions(data,{'dma_rxidle.part.0'})['dma_rxidle.part.0'];require(not relocations,'Unexpected child relocation')
    cases=0;instructions=0
    with tempfile.TemporaryDirectory(prefix='axd-rxfill-idle-arm-') as tmp:
        bridge=Path(tmp)/'bridge.c';library=Path(tmp)/'bridge.so';bridge.write_text(BRIDGE)
        subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11','-Wall','-Wextra','-Werror','-pedantic','-shared','-fPIC','-I'+str(root/'driver'),str(bridge),str(root/'driver/dma_rx_fill.c'),str(root/'driver/dma_descriptor.c'),'-o',str(library)],check=True)
        lib=ct.CDLL(str(library));lib.compare_idle.argtypes=[ct.c_uint32,CALLBACK,ct.POINTER(ct.c_uint32)];lib.compare_idle.restype=ct.c_int
        for mask,status,producer in itertools.product([0x1fff,0xffff,0xfffff,0xffffffff],[0,0x100,0x170,0xa0000170,0xf0000180,0xffffffff],[0,0x100,0x170,0xf0000170,0xa0000180,0xffffffff]):
            replies=[(0x10,status),(4,producer)];arm=emulate(code,mask,replies);c=model(lib,mask,replies)
            require(arm[:2]==c,'ARM/C idle child mismatch');cases+=1;instructions+=arm[2]
        rejected=0
        for reads,deny in [([(0x10,0x170),(4,0x170)],'unknown-mask'),([(0x10,0x170)],None),([(0x10,0x170),(4,0x170),(4,0)],None)]:
            try:emulate(code,0x1fff,reads,deny)
            except ValueError:rejected+=1
            else:raise ValueError('Invalid read/context accepted')
        try:emulate(code[:-1]+bytes([code[-1]^1]),0x1fff,[(0x10,0x170),(4,0x170)])
        except ValueError:rejected+=1
        else:raise ValueError('Changed child bytes accepted')
    print(json.dumps({'module_sha256':MODULE_SHA256,'function_sha256':FUNCTION_SHA256,'cases':cases,'actual_arm_instructions':instructions,
        'functions':1,'calls':0,'relocations':0,'rejection_probes':rejected,'whole_fill_arm_execution':False,
        'external_arm_helper_stubs':0,'hardware_operations':0},sort_keys=True))
if __name__=='__main__':main()
