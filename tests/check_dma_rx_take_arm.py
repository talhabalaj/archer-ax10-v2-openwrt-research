#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare the current closed RX packet-slot leaf with portable RX take.

Uses caller-supplied pinned stock ELF, Unicorn2.1.4 and a host C compiler.
Only synthetic context, strict typed status/slot read queues and real selected
instructions execute. No target driver, real MMIO, cache/unmap call or hardware
operation is supplied. Unknown calls, relocations or memory accesses fail.
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
FUNCTION_SHA256='e4c7021c6dd414b77c997303d6887f1acec45e4b50d0ab21d889ffee706daa26'
CODE,CTX,SLOTS,STACK,STATUS=0x10000000,0x20000000,0x21000000,0x30000000,0x31000000
STOP=CODE+0x1000

def require(ok,message):
    if not ok:raise ValueError(message)

def reads_for(count,inside,outside,cached,force,mode_a,mode_b,current,labels,base):
    """Supply explicit oracle inputs from the recovered branch contract."""
    result=[];i=inside;cache=cached;steps=0
    while i!=outside:
        steps+=1;require(steps<=count,'Oracle index loop exceeded ring')
        if i==cache:
            result.append((1,0x10,0xf0000000|((base&0xfffff)+current*16)));cache=current
            if cache==i and not force:break
        value=labels[i];result.append((3,i*4,value));i=(i+1)&(count-1)
        if not(value==0x80000000 and (mode_a or mode_b)):break
    return result

def emulate(code,count,inside,outside,cached,force,mode_a,mode_b,base,labels,reads,deny=None):
    import unicorn as uc
    from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC
    require(uc.__version__=='2.1.4','Use pinned Unicorn2.1.4')
    cpu=uc.Uc(uc.UC_ARCH_ARM,uc.UC_MODE_ARM|uc.UC_MODE_LITTLE_ENDIAN)
    for address in [CODE,CTX,STACK,STATUS]:cpu.mem_map(address,0x10000)
    cpu.mem_map(SLOTS,0x100000);cpu.mem_write(CODE,code)
    for index,label in enumerate(labels):cpu.mem_write(SLOTS+index*4,label.to_bytes(4,'little'))
    fields={CTX+0x80:(2,count),CTX+0x82:(2,inside),CTX+0x84:(2,outside),CTX+0x88:(4,SLOTS),
        CTX+0x4c:(4,STATUS),CTX+0xa4:(4,base),CTX+0xe4:(4,0xfffff),CTX+0xe8:(2,cached),
        CTX+0xf0:(4,99),CTX+0xf4:(1,mode_a),CTX+0x106:(1,mode_b)}
    if deny=='unknown-context':del fields[CTX+0x82]
    for address,(width,value) in fields.items():cpu.mem_write(address,value.to_bytes(width,'little'))
    sp=STACK+0x8000
    for register,value in [(UC_ARM_REG_R0,CTX),(UC_ARM_REG_R1,force),(UC_ARM_REG_SP,sp),(UC_ARM_REG_LR,STOP)]:cpu.reg_write(register,value)
    events=[];snapshots=[];remaining=list(reads);stack=set();instructions=[];cleared=[]
    def snapshot():
        return tuple(int.from_bytes(cpu.mem_read(address,width),'little') for address,width in
                     [(CTX+0x82,2),(CTX+0xf0,4),(CTX+0xe8,2)])
    def instruction(engine,address,width,_):
        require(CODE<=address<CODE+len(code) and width==4,'Unknown instruction')
        word=int.from_bytes(engine.mem_read(address,4),'little')
        require(not((word>>25)&7==5 and word&(1<<24)),'Unexpected call')
        if word==0xf57ff04f:events.append((2,0x1111,0,0));snapshots.append(snapshot())
        instructions.append(address)
    def supply(kind,offset,address):
        require(remaining and remaining[0][:2]==(kind,offset),'Missing/unexpected read input')
        value=remaining.pop(0)[2];cpu.mem_write(address,value.to_bytes(4,'little'))
    def memory(engine,access,address,width,value,_):
        if STATUS<=address<STATUS+0x10000:
            require(access==uc.UC_MEM_READ and width==4 and address==STATUS+0x10,'Unexpected status I/O')
            supply(1,0x10,address);events.append((1,0x1111,0x10,0));snapshots.append(snapshot())
        elif SLOTS<=address<SLOTS+count*4:
            require(width==4 and address%4==0,'Invalid packet-slot width')
            offset=address-SLOTS
            if access==uc.UC_MEM_READ:supply(3,offset,address);events.append((3,0x2222,offset,0))
            else:
                require(value==0,'Unexpected slot value store');events.append((4,0x2222,offset,0));cleared.append(offset)
            snapshots.append(snapshot())
        elif sp-0x100<=address<sp:
            touched=set(range(address,address+width))
            if access==uc.UC_MEM_WRITE:stack.update(touched)
            else:require(touched<=stack,'Unsupplied stack read')
        elif access==uc.UC_MEM_READ:require(address in fields and fields[address][0]==width,'Unsupplied context read')
        else:require((address==CTX+0x82 and width==2) or(address==CTX+0xe8 and width==2) or
                     (address==CTX+0xf0 and width==4),'Unexpected context write')
    cpu.hook_add(uc.UC_HOOK_CODE,instruction);cpu.hook_add(uc.UC_HOOK_MEM_READ|uc.UC_HOOK_MEM_WRITE,memory)
    cpu.emu_start(CODE,STOP,count=100000)
    require(cpu.reg_read(UC_ARM_REG_PC)==STOP and not remaining,'RX leaf/read queue did not complete')
    result=(*snapshot(),cpu.reg_read(UC_ARM_REG_R0))
    return events,snapshots,cleared,result,len(instructions)

BRIDGE=r'''
#include "dma_rx_take.h"
int compare_rx(uint32_t count,uint32_t in,uint32_t out,uint32_t cached,uint32_t force,
               unsigned a,unsigned b,uint32_t base,axd_rx_take_io_fn io,void *opaque,uint64_t *result) {
    struct axd_dma_rx_take j;
    const struct axd_rx_take_context c={{count,0xfffff,base,in,out},130,1,(uint16_t)cached,
        (uint8_t)a,(uint8_t)b,1,1,0x1111,0x2222,99,count};unsigned steps=0;
    if(axd_dma_rx_take_begin(&j,force,&c))return -1;
    result[4]=(uint64_t)(uintptr_t)&j;
    while(j.phase==AXD_RX_TAKE_RUNNING && steps++<10000)axd_dma_rx_take_step(&j,io,opaque);
    result[0]=j.context.ring.in;result[1]=j.context.available;result[2]=j.context.cached_current;
    result[3]=j.return_value;result[4]=j.attempted_ops;result[5]=j.completed_ops;
    result[6]=j.removed_slots;result[7]=j.skipped_sentinels;
    return j.phase==AXD_RX_TAKE_DONE && j.return_valid ? 0 : -1;
}
void snapshot_rx(const void *opaque,uint32_t *out) {
    const struct axd_dma_rx_take *j=opaque;
    out[0]=j->context.ring.in;out[1]=j->context.available;out[2]=j->context.cached_current;
}
'''
class Operation(ct.Structure):_fields_=[('kind',ct.c_int),('identity',ct.c_uint32),('offset',ct.c_uint32),('value',ct.c_uint32)]
CALLBACK=ct.CFUNCTYPE(ct.c_int,ct.c_void_p,ct.POINTER(Operation),ct.POINTER(ct.c_uint32))
def model(lib,count,inside,outside,cached,force,mode_a,mode_b,base,labels,reads):
    del labels
    events=[];snapshots=[];remaining=list(reads);failures=[];cleared=[];out=(ct.c_uint64*8)()
    @CALLBACK
    def io(_,pointer,reply):
        op=pointer.contents;events.append((op.kind,op.identity,op.offset,op.value))
        snapshot=(ct.c_uint32*3)();lib.snapshot_rx(ct.c_void_p(out[4]),snapshot);snapshots.append(tuple(snapshot))
        if op.kind in(1,3):
            if not remaining or remaining[0][:2]!=(op.kind,op.offset):failures.append('Missing C read');return -1
            reply[0]=remaining.pop(0)[2]
        if op.kind==4:cleared.append(op.offset)
        return 0
    status=lib.compare_rx(count,inside,outside,cached,force,mode_a,mode_b,base,io,None,out)
    require(status==0 and not remaining and not failures,'C RX trace failed')
    require(out[4]==out[5]==len(events) and out[6]==len(cleared),'C operation/clear accounting differs')
    return events,snapshots,cleared,tuple(out[:4])

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('module',type=Path);args=parser.parse_args()
    root=Path(__file__).resolve().parents[1];data=args.module.read_bytes()
    require(hashlib.sha256(data).hexdigest()==MODULE_SHA256,'Unrecognized module')
    spec=importlib.util.spec_from_file_location('rx_elf_helpers',root/'tests/check_phy_arm.py')
    helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
    code,relocations=helper.functions(data,{'dma_getnextrxp.part.1'})['dma_getnextrxp.part.1']
    require(len(code)==296 and hashlib.sha256(code).hexdigest()==FUNCTION_SHA256 and not relocations,'Unreviewed RX leaf')
    cases=0;instructions=0;empty=0
    with tempfile.TemporaryDirectory(prefix='axd-rx-take-arm-') as tmp:
        bridge=Path(tmp)/'bridge.c';library=Path(tmp)/'bridge.so';bridge.write_text(BRIDGE)
        subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11','-Wall','-Wextra','-Werror','-pedantic',
            '-shared','-fPIC','-I'+str(root/'driver'),str(bridge),str(root/'driver/dma_rx_take.c'),
            str(root/'driver/dma_descriptor.c'),'-o',str(library)],check=True)
        lib=ct.CDLL(str(library));lib.compare_rx.argtypes=[ct.c_uint32]*5+[ct.c_uint,ct.c_uint,ct.c_uint32,
            CALLBACK,ct.c_void_p,ct.POINTER(ct.c_uint64)];lib.compare_rx.restype=ct.c_int
        lib.snapshot_rx.argtypes=[ct.c_void_p,ct.POINTER(ct.c_uint32)]
        for count in [2,4,8,512]:
            pairs=list(itertools.product(range(count),repeat=2)) if count==2 else [(0,count-1),(count-2,2),(count-1,0),(1,1)]
            for inside,outside in pairs:
                for cache_same,force,modes,pattern,current_same in itertools.product(
                        [0,1],[0,1,0xffffffff],[(0,0),(0x80,0),(0,0xff)],[0,1,2,3],[0,1]):
                    cached=inside if cache_same else(inside+1)&(count-1)
                    current=inside if current_same else outside
                    labels=[0xdead0000+i if pattern==0 else 0 if pattern==1 else
                            0x80000000 if pattern==2 or i%2==0 else 0xabcdef00+i for i in range(count)]
                    base=0x12301000;reads=reads_for(count,inside,outside,cached,force,*modes,current,labels,base)
                    arguments=(count,inside,outside,cached,force,*modes,base,labels,reads)
                    arm=emulate(code,*arguments);c=model(lib,*arguments)
                    require(arm[:4]==c,f'ARM/C RX mismatch for{arguments[:8]}')
                    cases+=1;empty+=inside==outside;instructions+=arm[4]
        rejected=0;probe=(8,6,2,6,1,0,0,0x12301000,[0x1234]*8,[(1,0x10,0xf0001060),(3,24,0x1234)])
        for denial in ['unknown-context']:
            try:emulate(code,*probe,deny=denial)
            except ValueError:rejected+=1
            else:raise ValueError('Unknown-context probe accepted')
        for reads in [[],probe[-1]+[(3,28,0)]]:
            try:emulate(code,*probe[:-1],reads)
            except ValueError:rejected+=1
            else:raise ValueError('Read-queue rejection accepted')
    print(json.dumps({'module_sha256':MODULE_SHA256,'function_sha256':FUNCTION_SHA256,'cases':cases,
        'empty_ring_cases':empty,'actual_arm_instructions':instructions,'functions':1,'calls':0,'relocations':0,
        'rejection_probes':rejected,'strict_memory_and_read_queues':True,'hardware_operations':0},sort_keys=True))
if __name__=='__main__':main()
