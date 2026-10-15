#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare the actual status-release bitmap/cursor path with original C.

Caller supplies pinned wl.ko and Unicorn2.1.4. Bus0 takes the real source guard
that omits both HND calls; any reached call is rejected, never stubbed. This
does not claim ARM coverage of bus1 SI children (separate literal C tests),
physical bitmap ownership, hardware register effects or a complete RX driver.
"""
import argparse
import ctypes as ct
import hashlib
import importlib.util
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
MODULE_SHA256='8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c'
FUNCTION_SHA256='ff47f8de972e9b6d27906a3306109724b50caf83f5d5a2d12394a45ac4707509'
CODE,OFFLOAD,SI,BITMAP,REGISTERS,STACK=0x10000000,0x20000000,0x20001000,0x21000000,0x22000000,0x30008000
STOP=CODE+0x1000

def require(ok,message):
    if not ok:raise ValueError(message)

def load(data,root):
    require(hashlib.sha256(data).hexdigest()==MODULE_SHA256,'Unrecognized module')
    spec=importlib.util.spec_from_file_location('offload_elf',root/'tests/check_phy_arm.py')
    helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
    code,sites=helper.functions(data,{'wlc_offload_release_rxstatus'})['wlc_offload_release_rxstatus']
    require(len(code)==284 and hashlib.sha256(code).hexdigest()==FUNCTION_SHA256,'Modified selected body')
    require(sites=={0xfc,0x114},'Unknown relocation sites')
    # Verify the two native REL CALLs by exact symbol and instruction type.
    h=struct.unpack_from('<HHIIIIIHHHHHH',data,16)
    sections=[struct.unpack_from('<10I',data,h[5]+i*40) for i in range(h[11])]
    tables={};selected=[]
    for number,s in enumerate(sections):
        if s[1]!=2:continue
        strings_section=sections[s[6]];strings=data[strings_section[4]:strings_section[4]+strings_section[5]];items=[]
        for pos in range(0,s[5],16):
            n,v,z,info,_,owner=struct.unpack_from('<IIIBBH',data,s[4]+pos)
            name=strings[n:strings.find(b'\0',n)].decode('ascii');items.append(name)
            if name=='wlc_offload_release_rxstatus':selected.append((owner,v,z))
        tables[number]=items
    require(len(selected)==1,'Duplicate selected body');owner,start,length=selected[0];seen={}
    for s in sections:
        if s[1] not in(4,9) or s[7]!=owner:continue
        require(s[9]==(8 if s[1]==9 else 12),'Unknown relocation stride')
        for pos in range(0,s[5],s[9]):
            at,info=struct.unpack_from('<II',data,s[4]+pos)
            if start<=at<start+length:
                require(s[1]==9 and info&255==28,'Unknown call relocation')
                seen[at-start]=tables[s[6]][info>>8]
    require(seen=={0xfc:'si_restore_core',0x114:'si_switch_core'},'Unknown call binding')
    return code

def emulate(code,count,consumer,producer,index,initial,deny=None):
    import unicorn as uc
    from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_SP
    cpu=uc.Uc(uc.UC_ARCH_ARM,uc.UC_MODE_ARM|uc.UC_MODE_LITTLE_ENDIAN)
    for base in(CODE,OFFLOAD,BITMAP,REGISTERS,STACK-0x8000):cpu.mem_map(base,0x10000)
    cpu.mem_write(CODE,code);cpu.mem_write(BITMAP,initial)
    fields={OFFLOAD+4:SI,OFFLOAD+0x4c:REGISTERS,OFFLOAD+0x58:count,
        OFFLOAD+0x60:producer,OFFLOAD+0x68:consumer,OFFLOAD+0x6c:BITMAP,SI+4:0}
    if deny=='unknown-count':del fields[OFFLOAD+0x58]
    if deny=='reached-call':fields[SI+4]=1
    for address,value in fields.items():cpu.mem_write(address,value.to_bytes(4,'little'))
    for reg,value in((UC_ARM_REG_R0,OFFLOAD),(UC_ARM_REG_R1,index),(UC_ARM_REG_LR,STOP),(UC_ARM_REG_SP,STACK)):cpu.reg_write(reg,value)
    events=[];stack=set();instructions=[];stores=[]
    def instruction(engine,address,width,_):
        require(CODE<=address<CODE+len(code) and width==4,'Unknown execution')
        word=int.from_bytes(engine.mem_read(address,4),'little')
        require(not((word>>25)&7==5 and word&(1<<24)),'Reached external CALL')
        if word==0xf57ff04e:events.append((3,0,0,0,int.from_bytes(engine.mem_read(OFFLOAD+0x68,4),'little')))
        instructions.append(address)
    def memory(engine,access,address,width,value,_):
        current=int.from_bytes(engine.mem_read(OFFLOAD+0x68,4),'little')
        if BITMAP<=address<BITMAP+len(initial):
            require(width==1,'Wrong bitmap width')
            events.append((1 if access==uc.UC_MEM_READ else 2,BITMAP,address-BITMAP,0 if access==uc.UC_MEM_READ else value,current))
        elif REGISTERS<=address<REGISTERS+0x10000:
            require(access==uc.UC_MEM_WRITE and address==REGISTERS+0x20 and width==4,'Unknown register access')
            events.append((4,REGISTERS,0x20,value,current))
        elif STACK-0x1000<=address<STACK:
            touched=set(range(address,address+width))
            if access==uc.UC_MEM_WRITE:stack.update(touched)
            else:require(touched<=stack,'Uninitialized stack read')
        elif access==uc.UC_MEM_WRITE:
            require(address==OFFLOAD+0x68 and width==4,'Unknown software store');stores.append(value)
        else:require(width==4 and address in fields,'Unsupplied context field')
    cpu.hook_add(uc.UC_HOOK_CODE,instruction);cpu.hook_add(uc.UC_HOOK_MEM_READ|uc.UC_HOOK_MEM_WRITE,memory)
    cpu.emu_start(CODE,STOP,timeout=2000000,count=1500000)
    require(cpu.reg_read(UC_ARM_REG_PC)==STOP,'No actual return')
    return events,int.from_bytes(cpu.mem_read(OFFLOAD+0x68,4),'little'),bytes(cpu.mem_read(BITMAP,len(initial))),len(stores),len(instructions)

BRIDGE=r'''
#include "offload_status.h"
typedef int (*normalized_io)(void *,unsigned,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t *);
struct relay {struct axd_offload_status *j;normalized_io io;void *context;};
static int io(void *opaque,const struct axd_offload_status_operation *op,struct axd_offload_status_reply *reply){
 struct relay *r=opaque;
 return r->io(r->context,op->kind,op->identity,op->offset,op->value,r->j->context.consumer,&reply->value);
}
int compare_release(uint32_t count,uint32_t consumer,uint32_t producer,uint32_t index,normalized_io fn,uint32_t *out){
 struct axd_offload_status_context c={0};struct axd_offload_status j;struct relay r={&j,fn,0};unsigned steps=0;
 c.count=count;c.consumer=consumer;c.producer=producer;c.bitmap=0x21000000;c.bitmap_bytes=(count+7)/8;c.registers=0x22000000;
 c.bitmap_owned=c.registers_owned=c.os_owned=c.si_known=1;
 if(axd_offload_status_begin(&j,(int32_t)index,&c))return -1;
 while(j.phase==AXD_OS_RUNNING && steps++<600000)axd_offload_status_step(&j,io,&r);
 out[0]=j.context.consumer;out[1]=j.consumer_stores;out[2]=(uint32_t)j.attempted_ops;out[3]=(uint32_t)j.completed_ops;
 return j.phase==AXD_OS_TRACE_COMPLETE?0:-1;
}
'''
CALLBACK=ct.CFUNCTYPE(ct.c_int,ct.c_void_p,ct.c_uint,ct.c_uint32,ct.c_uint32,ct.c_uint32,ct.c_uint32,ct.POINTER(ct.c_uint32))

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('module',type=Path);args=parser.parse_args()
    import unicorn
    require(unicorn.__version__=='2.1.4','Use pinned Unicorn2.1.4')
    require(args.module.stat().st_size<64*1024*1024,'Oversized input')
    root=Path(__file__).resolve().parents[1];code=load(args.module.read_bytes(),root);rng=random.Random(0x63178130)
    cases=[]
    for count in(2,8,16,256,65536):
        for consumer,producer,index in((0,0,0),(count-1,1,count-1),(0,count-1,0),(count-1,0,0)):
            for mode in(0,1,2):
                initial=bytearray((count+7)//8)
                if mode==1:initial[:]=bytes([255])*len(initial)
                if mode==2:initial[(count//2)>>3]|=1<<((count//2)&7)
                cases.append((count,consumer,producer,index,bytes(initial)))
    for _ in range(160):
        count=rng.choice((8,16,32,64,256));cases.append((count,rng.randrange(count),rng.randrange(count),rng.randrange(count),rng.randbytes((count+7)//8)))
    instructions=0
    with tempfile.TemporaryDirectory(prefix='axd-offload-arm-') as folder:
        folder=Path(folder);bridge=folder/'bridge.c';bridge.write_text(BRIDGE);library=folder/('model.dylib' if sys.platform=='darwin' else 'model.so')
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-pedantic','-fPIC','-dynamiclib' if sys.platform=='darwin' else '-shared','-I',str(root/'driver'),*[str(root/'driver'/s) for s in('offload_status.c','si_switch.c','si_select.c')],str(bridge),'-o',str(library)],check=True)
        lib=ct.CDLL(str(library));lib.compare_release.argtypes=[ct.c_uint32]*4+[CALLBACK,ct.POINTER(ct.c_uint32)];lib.compare_release.restype=ct.c_int
        for case in cases:
            expected,consumer,bitmap,stores,executed=emulate(code,*case);instructions+=executed
            memory=bytearray(case[4]);events=[];errors=[]
            @CALLBACK
            def io(_,kind,identity,offset,value,current,reply):
                event=(kind,identity,offset,value,current)
                if len(events)>=len(expected) or event!=expected[len(events)]:errors.append('C event differs');return -1
                events.append(event)
                if kind==1:reply[0]=memory[offset]
                elif kind==2:memory[offset]=value
                return 0
            out=(ct.c_uint32*4)()
            require(lib.compare_release(*case[:4],io,out)==0 and not errors,'C release failed/differed')
            require(events==expected and bytes(memory)==bitmap,'Bitmap/trace mismatch')
            require(list(out)==[consumer,stores,len(events),len(events)],'Consumer/store/accounting mismatch')
    rejected=0
    # Two reached guards and one modified code identity must fail closed.
    probe=(8,0,2,0,bytes([1]))
    for deny in('unknown-count','reached-call'):
        try:emulate(code,*probe,deny=deny)
        except (ValueError,unicorn.UcError):rejected+=1
        else:raise ValueError('Rejection probe passed')
    try:load(b'not the pinned module',root)
    except ValueError:rejected+=1
    else:raise ValueError('Unpinned input passed')
    print(f'Offload status ARM: {len(cases)} cases / {instructions} actual instructions / {rejected} rejection probes; zero external stubs')

if __name__=='__main__':main()
