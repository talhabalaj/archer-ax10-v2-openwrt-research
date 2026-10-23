#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare four current object-memory writers with actual selected ARM code.

Requires caller-supplied pinned stock ELF, Unicorn2.1.4 and a host C compiler.
Only the two reviewed COPY->WRITE16 ARM CALL relocations are bound. Unknown
calls, instructions, context/source/stack reads, MMIO widths/ports and missing
or unused read replies fail closed. No target driver/MMIO/hardware executes.
"""
import argparse
import ctypes as ct
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile

MODULE_SHA256='8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c'
COPY='wlc_bmac_copyto_objmem'
COPY32='wlc_bmac_copyto_objmem32'
SET32='wlc_bmac_set_objmem32'
WRITE16='wlc_bmac_write_objmem16'
IDENTITIES={COPY:(356,'0f6e4804dd0e3ac40e2d6fc91607ade35adabf34b8c310a20ed302c652e5dae0'),
 COPY32:(188,'88992713b2b4af24ee7a109162d52be579cd3e8ff5f19a256447fcb9603e1473'),
 SET32:(148,'c13b06f7800834f1cd51633725afaf6f70392d25b66f553656ef9e5b67cd535b'),
 WRITE16:(148,'f6153a9cbd6d4292bc0bc81bbff6600387b1668a2fa2088ce858129f58730632')}
CODE,RAM,MMIO=0x1000000,0x2000000,0x3000000
STOP=CODE+0x80000
HW,ROOT,DESC,SOURCE,STACK=[RAM+x for x in (0,0x1000,0x2000,0x3000,0xf000)]
ST,SY,W32,R32,W16=1,2,3,4,7

def require(ok,message):
    if not ok:raise ValueError(message)

def load(data,repo):
    require(hashlib.sha256(data).hexdigest()==MODULE_SHA256,'Unrecognized stock module')
    spec=importlib.util.spec_from_file_location('object_elf_helpers',repo/'tests/check_phy_arm.py')
    base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
    blobs=base.functions(data,set(IDENTITIES))
    h=struct.unpack_from('<HHIIIIIHHHHHH',data,16)
    sections=[struct.unpack_from('<10I',data,h[5]+i*40) for i in range(h[11])]
    symbols={};selected={}
    for number,s in enumerate(sections):
        if s[1]!=2:continue
        st=sections[s[6]];strings=data[st[4]:st[4]+st[5]];items=[]
        for pos in range(0,s[5],16):
            n,v,z,info,_,owner=struct.unpack_from('<IIIBBH',data,s[4]+pos)
            name=strings[n:strings.find(b'\0',n)].decode('ascii');items.append((name,v,z,info,owner))
            if name in IDENTITIES:selected[name]=(owner,v,z)
        symbols[number]=items
    functions={}
    for number,name in enumerate(sorted(IDENTITIES)):
        code,sites=blobs[name];length,digest=IDENTITIES[name]
        require(len(code)==length and hashlib.sha256(code).hexdigest()==digest,'Unrecognized selected function')
        require(sites==({0x11c,0x150} if name==COPY else set()),'Unreviewed relocation sites')
        owner,value,size=selected[name];relocations={}
        for section in sections:
            if section[1] not in(4,9) or section[7]!=owner:continue
            for pos in range(0,section[5],section[9]):
                at,info=struct.unpack_from('<II',data,section[4]+pos)
                if not value<=at<value+size:continue
                target=symbols[section[6]][info>>8]
                require(section[1]==9 and info&255==28 and name==COPY and target[0]==WRITE16,
                        'Unreviewed relocation binding')
                require((target[4],target[1],target[2])==selected[WRITE16],'Wrong actual target identity')
                relocations[at-value]=WRITE16
        require(set(relocations)==sites,'Relocation disagreement')
        functions[name]={'code':code,'address':CODE+number*0x10000,'relocations':relocations}
    return functions

def emulate(functions,command,address,selector,value,count,data,replies,deny=None):
    import unicorn as uc
    from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3
    from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC
    require(uc.__version__=='2.1.4','Use pinned Unicorn2.1.4')
    cpu=uc.Uc(uc.UC_ARCH_ARM,uc.UC_MODE_ARM|uc.UC_MODE_LITTLE_ENDIAN)
    cpu.mem_map(RAM,0x10000);cpu.mem_map(MMIO,0x1000)
    resolved={};loaded=[]
    for name,item in functions.items():
        code=bytearray(item['code']);start=item['address']
        for off,target in item['relocations'].items():
            word=struct.unpack_from('<I',code,off)[0]
            require(word>>28!=15 and (word>>25)&7==5 and word&(1<<24),'Non-ARM CALL')
            addend=(word&0xffffff)<<2
            if addend&(1<<25):addend-=1<<26
            require(addend==-8,'Unexpected actual ARM CALL addend')
            destination=functions[target]['address'];delta=destination+addend-(start+off)
            require(delta%4==0 and -(1<<25)<=delta<1<<25,'CALL binding overflow')
            struct.pack_into('<I',code,off,(word&0xff000000)|((delta>>2)&0xffffff))
            resolved[start+off]=destination
        cpu.mem_map(start,0x1000);cpu.mem_write(start,bytes(code));loaded.append((start,start+len(code)))
    if deny=='unbound-call':resolved.clear()
    fields={HW+0x198:(4,MMIO),HW+0x19c:(4,ROOT),ROOT:(4,DESC),DESC+0x34:(2,0x160),DESC+0x36:(2,0x164)}
    if deny=='unknown-context':del fields[HW+0x198]
    for at,(width,word) in fields.items():cpu.mem_write(at,word.to_bytes(width,'little'))
    cpu.mem_write(SOURCE,data)
    cpu.mem_write(STACK,selector.to_bytes(4,'little'))
    initialized_stack=set(range(STACK,STACK+4))
    entry={1:COPY,2:COPY32,3:SET32,4:WRITE16}[command]
    args=(HW,address,value,selector) if command==4 else (HW,address,value if command==3 else SOURCE,count&0xffffffff)
    for reg,word in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),args):cpu.reg_write(reg,word)
    cpu.reg_write(UC_ARM_REG_SP,STACK);cpu.reg_write(UC_ARM_REG_LR,STOP)
    events=[];remaining=list(replies);visited=[]
    def instruction(engine,at,width,_):
        require(width==4 and any(lo<=at<hi for lo,hi in loaded),'Unknown instruction target')
        owner=next(name for name,item in functions.items() if item['address']<=at<item['address']+len(item['code']))
        off=at-functions[owner]['address']
        if off in functions[owner]['relocations']:require(at in resolved,'Unbound selected call')
        word=int.from_bytes(engine.mem_read(at,4),'little')
        if word in (0xf57ff04e,0xf57ff04f):events.append((ST if word==0xf57ff04e else SY,0,0))
        # Every branch-with-link must be one of the two explicitly bound calls.
        if (word>>25)&7==5 and word&(1<<24):require(at in resolved,'Unknown call instruction')
        visited.append(at)
    def memory(engine,access,at,width,word,_):
        if MMIO<=at<MMIO+0x1000:
            off=at-MMIO
            if access==uc.UC_MEM_READ:
                require(off==0x160 and width==4 and remaining,'Missing/unexpected object read')
                engine.mem_write(at,remaining.pop(0).to_bytes(4,'little'));events.append((R32,off,0))
            else:
                require((width==4 and off in(0x160,0x164)) or (width==2 and off in(0x164,0x166)),
                        'Unexpected object write port/width')
                events.append((W32 if width==4 else W16,off,word&((1<<(8*width))-1)))
        elif STACK-0x1000<=at<STACK+4:
            addresses=set(range(at,at+width))
            if access==uc.UC_MEM_WRITE:initialized_stack.update(addresses)
            else:require(addresses<=initialized_stack,'Unsupplied stack read')
        elif access==uc.UC_MEM_READ:
            if SOURCE<=at<SOURCE+len(data):require(width==1 and at+width<=SOURCE+len(data),'Source overread')
            else:require(at in fields and fields[at][0]==width,'Unsupplied context read')
        else:raise ValueError('Unexpected context/source write')
    cpu.hook_add(uc.UC_HOOK_CODE,instruction)
    cpu.hook_add(uc.UC_HOOK_MEM_READ|uc.UC_HOOK_MEM_WRITE,memory)
    cpu.emu_start(functions[entry]['address'],STOP,count=20000)
    require(cpu.reg_read(UC_ARM_REG_PC)==STOP,'Function did not return')
    require(not remaining,'Unconsumed ARM read replies')
    return events,len(visited)

BRIDGE=r'''
#include "object_memory.h"
int compare_object(unsigned command,uint32_t address,uint32_t selector,uint32_t value,
                   int32_t count,const uint8_t *data,size_t size,axd_d11_io_fn io,uint64_t *out) {
    struct axd_object_memory j;
    struct axd_object_request r={(enum axd_object_command)command,address,selector,value,count,data,size};
    const struct axd_object_context c={130,1,1};unsigned steps=0;
    if(axd_object_memory_begin(&j,&r,&c)) return -1;
    while(j.phase==AXD_OBJECT_RUNNING && steps++<20000) axd_object_memory_step(&j,io,0);
    out[0]=j.completed_bytes;out[1]=j.last_read;out[2]=j.address_writes;
    out[3]=j.attempted_ops;out[4]=j.completed_ops;
    return j.phase==AXD_OBJECT_DONE && j.error==AXD_OBJECT_NO_ERROR ? 0 : -1;
}
'''
class Operation(ct.Structure):_fields_=[('kind',ct.c_int),('offset',ct.c_uint32),('value',ct.c_uint32)]
CALLBACK=ct.CFUNCTYPE(ct.c_int,ct.c_void_p,ct.POINTER(Operation),ct.POINTER(ct.c_uint32))
def model(lib,command,address,selector,value,count,data,replies):
    events=[];remaining=list(replies);failures=[]
    @CALLBACK
    def io(_,pointer,reply):
        op=pointer.contents;events.append((op.kind,op.offset,op.value))
        if op.kind==R32:
            if not remaining:failures.append('Missing C reply');return -1
            reply[0]=remaining.pop(0)
        return 0
    source=(ct.c_uint8*len(data))(*data);out=(ct.c_uint64*5)()
    status=lib.compare_object(command,address,selector,value,count,source,len(data),io,out)
    require(status==0 and not failures and not remaining,'C trace or read queue failed')
    expected_bytes=2 if command==4 else max(0,count)
    require(out[0]==expected_bytes and out[1]==(replies[-1] if replies else 0) and out[2]==len(replies),
            'C partial-result accounting differs')
    require(out[3]==out[4]==len(events),'C operation accounting differs')
    return events

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('module',type=Path);args=parser.parse_args()
    root=Path(__file__).resolve().parents[1];data=args.module.read_bytes();functions=load(data,root)
    cases=[]
    for selector in [0,0x10000,0x20000,0x30000,0x40000,0x50000,0x60000,0x70000,0x90000]:
        half=(selector&0x50000)==0x10000 or (selector&0x70000)==0x20000
        for count in [-2147483648,-1,0,2,4,6,8,10]:
            for address in [0x200,0x202 if half or count==2 else 0x204,0x80000200]:
                cases.append((1,address,selector,0,count))
    for command in [2,3]:
        for selector in [0,0x10000,0x20000,0x70000,0xdead0000]:
            for count in [-2147483648,-1,0,4,8,12]:
                for address in [0,0x200,0xfffffff0]:cases.append((command,address,selector,0x89abcdef,count))
    for address in [0,2,0x200,0x202,0xfffffffe]:
        for selector in [0,0x10000,0x70000,0xdeadbeef]:
            for value in [0,0x10000,0xabcdef12]:cases.append((4,address,selector,value,0))
    instructions=0
    with tempfile.TemporaryDirectory(prefix='axd-object-arm-') as tmp:
        bridge=Path(tmp)/'bridge.c';library=Path(tmp)/'bridge.so';bridge.write_text(BRIDGE)
        subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11','-Wall','-Wextra','-Werror','-pedantic',
            '-shared','-fPIC','-I'+str(root/'driver'),str(bridge),str(root/'driver/object_memory.c'),'-o',str(library)],check=True)
        lib=ct.CDLL(str(library));lib.compare_object.argtypes=[ct.c_uint,ct.c_uint32,ct.c_uint32,ct.c_uint32,
            ct.c_int32,ct.POINTER(ct.c_uint8),ct.c_size_t,CALLBACK,ct.POINTER(ct.c_uint64)]
        lib.compare_object.restype=ct.c_int
        source=bytes([0x12,0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0,0x13,0x57,0x9b,0xdf])
        for command,address,selector,value,count in cases:
            half=(selector&0x50000)==0x10000 or (selector&0x70000)==0x20000
            reads=1 if command!=1 else 0 if count<=0 else count//2 if half else (count+3)//4
            replies=[(0xa5a51234+i*0x1234567)&0xffffffff for i in range(reads)]
            arguments=(command,address,selector,value,count,source,replies)
            arm,steps=emulate(functions,*arguments);c=model(lib,*arguments)
            require(arm==c,f'Actual ARM and C object traces differ: case{arguments[:5]}, ARM{arm}, C{c}');instructions+=steps
        # Closed-call/read-binding rejection probes: no successful shim fallback.
        probe=(1,0x200,0x10000,0,2,source,[0x12345678])
        rejected=0
        for denial in ['unbound-call','unknown-context']:
            try:emulate(functions,*probe,deny=denial)
            except ValueError:rejected+=1
            else:raise ValueError('Rejection probe accepted')
        for replies in [[],[1,2]]:
            try:emulate(functions,*probe[:-1],replies)
            except ValueError:rejected+=1
            else:raise ValueError('Read-queue rejection accepted')
    print(json.dumps({'module_sha256':MODULE_SHA256,'functions':len(functions),'cases':len(cases),
        'actual_arm_instructions':instructions,'resolved_internal_call_sites':2,'external_calls':0,
        'rejection_probes':rejected,'strict_memory_and_read_queues':True,'hardware_operations':0},sort_keys=True))
if __name__=='__main__':main()
