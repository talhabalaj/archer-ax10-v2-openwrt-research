#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare current closed dma_txdesc ARM leaf with portable descriptor posting.

Requires caller-supplied pinned stock ELF, Unicorn2.1.4 and a host C compiler.
All descriptor parity reads have explicit independent replies. The emulator
rejects calls, relocations, unknown memory/instructions and unsupported ports;
it executes no hardware, target module or successful external-function stub.
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
import struct
import subprocess
import tempfile

MODULE_SHA256='8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c'
FUNCTION_SHA256='60b22ebeb1baaf5cdc675f99713c471d7e2eacabd3d9150c64f7cd57a1f693d0'
CODE,CTX,INPUT,RING,STACK,CHANNEL=0x10000000,0x20000000,0x20100000,0x21000000,0x30000000,0x31000000
STOP=CODE+0x1000

def require(ok,message):
    if not ok:raise ValueError(message)

def emulate(code,count,inside,outside,base,flags,burst,commit,descriptor,replies,deny=None):
    import unicorn as uc
    from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC
    require(uc.__version__=='2.1.4','Use pinned Unicorn2.1.4')
    cpu=uc.Uc(uc.UC_ARCH_ARM,uc.UC_MODE_ARM|uc.UC_MODE_LITTLE_ENDIAN)
    for address in [CODE,CTX,INPUT,STACK,CHANNEL]:cpu.mem_map(address,0x10000)
    cpu.mem_map(RING,0x100000);cpu.mem_write(CODE,code);cpu.mem_write(INPUT,struct.pack('<4I',*descriptor))
    fields={CTX:(4,99),CTX+4:(4,flags),CTX+0x10:(4,0xffffffff),CTX+0x48:(4,CHANNEL),
        CTX+0x50:(4,RING),CTX+0x5a:(2,count),CTX+0x5c:(2,inside),CTX+0x5e:(2,outside),
        CTX+0x7c:(4,base),CTX+0xee:(1,burst)}
    if deny=='unknown-context':del fields[CTX+0x5a]
    for address,(width,value) in fields.items():cpu.mem_write(address,value.to_bytes(width,'little'))
    sp=STACK+0x8000
    for register,value in [(UC_ARM_REG_R0,CTX),(UC_ARM_REG_R1,INPUT),(UC_ARM_REG_R2,commit),
                           (UC_ARM_REG_SP,sp),(UC_ARM_REG_LR,STOP)]:cpu.reg_write(register,value)
    events=[];snapshots=[];remaining=list(replies);stack=set();instructions=[];memory={}
    def snapshot():
        return (int.from_bytes(cpu.mem_read(CTX+0x5e,2),'little'),int.from_bytes(cpu.mem_read(CTX,4),'little'),
                int.from_bytes(cpu.mem_read(INPUT,4),'little'))
    def instruction(engine,address,width,_):
        require(CODE<=address<CODE+len(code) and width==4,'Unknown instruction')
        word=int.from_bytes(engine.mem_read(address,4),'little')
        require(not((word>>25)&7==5 and word&(1<<24)),'Unexpected call')
        if word==0xf57ff04e:
            events.append((3,2,0x2222,0,0));snapshots.append(snapshot())
        instructions.append(address)
    def access(engine,kind,address,width,value,_):
        if RING<=address<RING+0x100000:
            require(width==4 and address-RING in [outside*16+x for x in [8,12,0,4]],'Wrong descriptor access')
            offset=address-RING
            if kind==uc.UC_MEM_READ:
                require(remaining,'Missing parity reply');reply=remaining.pop(0)
                engine.mem_write(address,reply.to_bytes(4,'little'));memory[offset]=reply
                events.append((2,1,0x1111,offset,0))
            else:memory[offset]=value;events.append((1,1,0x1111,offset,value))
            snapshots.append(snapshot())
        elif CHANNEL<=address<CHANNEL+0x10000:
            require(kind==uc.UC_MEM_WRITE and width==4 and address==CHANNEL+4,'Wrong channel I/O')
            events.append((4,2,0x2222,4,value));snapshots.append(snapshot())
        elif sp-0x100<=address<sp:
            touched=set(range(address,address+width))
            if kind==uc.UC_MEM_WRITE:stack.update(touched)
            else:require(touched<=stack,'Unsupplied stack read')
        elif INPUT<=address<INPUT+16:
            require(width==4 and address%4==0,'Wrong input access')
            if kind==uc.UC_MEM_WRITE:require(address==INPUT,'Unexpected input mutation')
        elif kind==uc.UC_MEM_READ:
            require(address in fields and fields[address][0]==width,'Unsupplied context read')
        else:
            require((address==CTX and width==4) or (address==CTX+0x10 and width==4) or
                    (address==CTX+0x5e and width==2),'Unexpected context write')
    cpu.hook_add(uc.UC_HOOK_CODE,instruction)
    cpu.hook_add(uc.UC_HOOK_MEM_READ|uc.UC_HOOK_MEM_WRITE,access)
    cpu.emu_start(CODE,STOP,timeout=1000000,count=1000)
    require(cpu.reg_read(UC_ARM_REG_PC)==STOP and not remaining,'Leaf/read queue did not complete')
    output=(int.from_bytes(cpu.mem_read(CTX,4),'little'),int.from_bytes(cpu.mem_read(CTX+0x10,4),'little'),
            int.from_bytes(cpu.mem_read(CTX+0x5e,2),'little'),*struct.unpack('<4I',cpu.mem_read(INPUT,16)),
            cpu.reg_read(UC_ARM_REG_R0))
    return events,snapshots,memory,output,len(instructions)

BRIDGE=r'''
#include "dma_tx_post.h"
int compare_post(uint32_t count,uint32_t in,uint32_t out,uint32_t base,uint32_t flags,
                 unsigned burst,unsigned commit,const uint32_t *words,axd_dma_tx_post_io_fn io,
                 void *opaque,uint64_t *result) {
    struct axd_dma_tx_post j;
    const struct axd_dma_tx_post_request r={{words[0],words[1],words[2],words[3]},(uint8_t)commit};
    const struct axd_dma_tx_post_context c={{count,0xfffff,base,in,out},flags,99,UINT32_MAX,
        0x1111,count*16,0x2222,(uint8_t)burst,1,1};unsigned steps=0;
    if(axd_dma_tx_post_begin(&j,&r,&c))return -1;
    result[8]=(uint64_t)(uintptr_t)&j;
    while(j.phase==AXD_DTP_RUNNING && steps++<1000)axd_dma_tx_post_step(&j,io,opaque);
    result[0]=j.context.available;result[1]=j.context.no_descriptor_count;result[2]=j.context.ring.out;
    result[3]=j.request.descriptor.control1;result[4]=j.request.descriptor.control2;
    result[5]=j.request.descriptor.address_low;result[6]=j.request.descriptor.address_high;
    result[7]=j.return_value;result[8]=j.attempted_ops;result[9]=j.completed_ops;
    return j.phase==AXD_DTP_DONE && j.return_valid ? 0 : -1;
}
void snapshot_post(const void *opaque,uint32_t *out) {
    const struct axd_dma_tx_post *j=opaque;
    out[0]=j->context.ring.out;out[1]=j->context.available;out[2]=j->request.descriptor.control1;
}
'''
class Operation(ct.Structure):_fields_=[('kind',ct.c_int),('resource',ct.c_int),('identity',ct.c_uint32),('offset',ct.c_uint32),('value',ct.c_uint32)]
CALLBACK=ct.CFUNCTYPE(ct.c_int,ct.c_void_p,ct.POINTER(Operation),ct.POINTER(ct.c_uint32))
def model(lib,count,inside,outside,base,flags,burst,commit,descriptor,replies):
    events=[];snapshots=[];memory={};remaining=list(replies);failures=[];out=(ct.c_uint64*10)()
    @CALLBACK
    def io(_,pointer,reply):
        op=pointer.contents;events.append((op.kind,op.resource,op.identity,op.offset,op.value))
        s=(ct.c_uint32*3)();lib.snapshot_post(ct.c_void_p(out[8]),s);snapshots.append(tuple(s))
        if op.kind==2:
            if not remaining:failures.append('Missing C parity reply');return -1
            reply[0]=remaining.pop(0);memory[op.offset]=reply[0]
        elif op.kind==1:memory[op.offset]=op.value
        return 0
    words=(ct.c_uint32*4)(*descriptor)
    status=lib.compare_post(count,inside,outside,base,flags,burst,commit,words,io,None,out)
    require(status==0 and not remaining and not failures,'C trace failed')
    require(out[8]==out[9]==len(events),'C operation accounting differs')
    return events,snapshots,memory,tuple(out[:8])

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('module',type=Path);args=parser.parse_args()
    root=Path(__file__).resolve().parents[1];data=args.module.read_bytes()
    require(hashlib.sha256(data).hexdigest()==MODULE_SHA256,'Unrecognized module')
    spec=importlib.util.spec_from_file_location('post_elf_helpers',root/'tests/check_phy_arm.py')
    base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
    code,relocations=base.functions(data,{'dma_txdesc'})['dma_txdesc']
    require(len(code)==324 and hashlib.sha256(code).hexdigest()==FUNCTION_SHA256 and not relocations,'Unreviewed leaf')
    cases=0;full=0;instructions=0
    with tempfile.TemporaryDirectory(prefix='axd-dma-post-arm-') as tmp:
        bridge=Path(tmp)/'bridge.c';library=Path(tmp)/'bridge.so';bridge.write_text(BRIDGE)
        subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11','-Wall','-Wextra','-Werror','-pedantic',
            '-shared','-fPIC','-I'+str(root/'driver'),str(bridge),str(root/'driver/dma_tx_post.c'),
            str(root/'driver/dma_descriptor.c'),'-o',str(library)],check=True)
        lib=ct.CDLL(str(library));lib.compare_post.argtypes=[ct.c_uint32]*5+[ct.c_uint,ct.c_uint,
            ct.POINTER(ct.c_uint32),CALLBACK,ct.c_void_p,ct.POINTER(ct.c_uint64)]
        lib.compare_post.restype=ct.c_int;lib.snapshot_post.argtypes=[ct.c_void_p,ct.POINTER(ct.c_uint32)]
        for count in [2,4,8,512]:
            pairs=itertools.product(range(count),repeat=2) if count<=8 else [(0,511),(3,511),(511,0),(1,2)]
            for inside,outside in pairs:
                for flags,burst,commit,odd in itertools.product([0,0x8001],[0,0x80],[0,0x80],[0,1]):
                    descriptor=(0x80000001,0x89abcdef,0x11223344+outside,0x55667788+inside)
                    is_full=((outside+1)&(count-1))==inside
                    replies=[0x11111111,0x22222222,0x33333333,odd] if flags&1 and not is_full else []
                    arguments=(count,inside,outside,0x12300000,flags,burst,commit,descriptor,replies)
                    arm=emulate(code,*arguments);c=model(lib,*arguments)
                    require(arm[:4]==c,f'ARM/C posting mismatch for{arguments[:7]}')
                    cases+=1;full+=is_full;instructions+=arm[4]
        rejected=0;probe=(8,4,7,0x12300000,1,1,1,(0x80000001,0x89abcdef,1,2),[1,2,3,1])
        for denial in ['unknown-context']:
            try:emulate(code,*probe,deny=denial)
            except ValueError:rejected+=1
            else:raise ValueError('Context rejection accepted')
        for replies in [[],[1,2,3,4,5]]:
            try:emulate(code,*probe[:-1],replies)
            except ValueError:rejected+=1
            else:raise ValueError('Read-queue rejection accepted')
    print(json.dumps({'module_sha256':MODULE_SHA256,'function_sha256':FUNCTION_SHA256,'cases':cases,
        'full_ring_cases':full,'actual_arm_instructions':instructions,'functions':1,'calls':0,'relocations':0,
        'rejection_probes':rejected,'strict_memory_and_read_queues':True,'hardware_operations':0},sort_keys=True))
if __name__=='__main__':main()
