#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare both current closed RX list leaves with portable C.

Caller supplies the pinned immutable module. Executes only selected instructions
with typed synthetic contexts, linked nodes, descriptor/status observations, and
strict initialized-stack tracking. Rejects unknown memory/calls/relocations and
missing/unused read inputs. No target driver, hardware, cache or OS helper runs.
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
FUNCTIONS={'_dma_getnext_rxlist':(324,'74e18628df25fdbb4a62f289a0ff855a595796878342556825d21319dfc38450'),
 '_dma_getnext_sts_rxlist':(280,'cd8eb6ca8fde6cdb9e229f70984867a8118d9a74779b55041f436f7236c78ce3')}
CODE,CTX,LINKED,RING,NODES,STACK,STATUS=0x10000000,0x20000000,0x20100000,0x21000000,0x22000000,0x30000000,0x31000000
STOP=CODE+0x1000

def require(ok,message):
    if not ok:raise ValueError(message)

def inputs(status,count,inside,outside,cached,force,budget,current,terminated,base):
    end=outside if force else current if not status or cached==inside else cached
    if not force and not status:end=(inside+min((end-inside)&(count-1),budget))&(count-1)
    n=(end-inside)&(count-1) if count else 0
    length=max(1,n+(not terminated));nodes={NODES+i*0x100:(NODES+(i+1)*0x100 if i+1<length else 0) for i in range(length)}
    reads=[]
    if count and not force and(not status or cached==inside):reads.append((1,0x1111,0x10,0xf0000000|((base+current*16)&0xfffff)))
    for k in range(n):
        node=NODES+k*0x100;index=(inside+k)&(count-1)
        reads.extend([(3,node,0x1c if status else 4,nodes[node]),(5,0x2222,index*16+8,0xdead0000+index)])
    return nodes,reads

def emulate(code,status,count,inside,outside,cached,force,budget,current,terminated,base,linked,nodes,reads,deny=None):
    import unicorn as uc
    from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC
    require(uc.__version__=='2.1.4','Use pinned Unicorn2.1.4')
    cpu=uc.Uc(uc.UC_ARCH_ARM,uc.UC_MODE_ARM|uc.UC_MODE_LITTLE_ENDIAN)
    for address in [CODE,CTX,LINKED,STACK,STATUS]:cpu.mem_map(address,0x10000)
    cpu.mem_map(RING,0x100000);cpu.mem_map(NODES,0x100000);cpu.mem_write(CODE,code)
    fields={CTX+0x80:(2,count),CTX+0x82:(2,inside),CTX+0x84:(2,outside),CTX+0x54:(4,RING),CTX+0x4c:(4,STATUS),
        CTX+0xa4:(4,base),CTX+0xe4:(4,0xfffff),CTX+0xe8:(2,cached),CTX+0x124:(4,NODES),
        CTX+0x128:(4,max(nodes)),CTX+0xf0:(4,99),CTX+0xf5:(1,linked),CTX+0xf8:(4,LINKED),
        LINKED+0x82:(2,5),LINKED+0xf0:(4,88)}
    if deny=='unknown-context':del fields[CTX+0x80]
    for address,(width,value) in fields.items():cpu.mem_write(address,value.to_bytes(width,'little'))
    noff=0x1c if status else 4
    for node,value in nodes.items():cpu.mem_write(node+noff,value.to_bytes(4,'little'))
    sp=STACK+0x8000
    for register,value in[(UC_ARM_REG_R0,CTX),(UC_ARM_REG_R1,force if status else budget),
        (UC_ARM_REG_R2,force),(UC_ARM_REG_SP,sp),(UC_ARM_REG_LR,STOP)]:cpu.reg_write(register,value)
    events=[];snapshots=[];remaining=list(reads);stack=set();instructions=[];writes=[]
    def snapshot():
        return tuple(int.from_bytes(cpu.mem_read(address,width),'little') for address,width in
            [(CTX+0x82,2),(CTX+0xf0,4),(CTX+0xe8,2),(CTX+0x124,4),(CTX+0x128,4),(LINKED+0x82,2),(LINKED+0xf0,4)])
    def instruction(engine,address,width,_):
        require(CODE<=address<CODE+len(code) and width==4,'Unknown instruction')
        word=int.from_bytes(engine.mem_read(address,4),'little');require(not((word>>25)&7==5 and word&(1<<24)),'Unexpected call')
        if word==0xf57ff04f:events.append((2,0x1111,0,0));snapshots.append(snapshot())
        instructions.append(address)
    def supply(kind,identity,offset,address):
        require(remaining and remaining[0][:3]==(kind,identity,offset),'Missing/unexpected read input')
        value=remaining.pop(0)[3];cpu.mem_write(address,value.to_bytes(4,'little'))
    def memory(engine,access,address,width,value,_):
        if STATUS<=address<STATUS+0x10000:
            require(access==uc.UC_MEM_READ and width==4 and address==STATUS+0x10,'Unexpected status I/O')
            supply(1,0x1111,0x10,address);events.append((1,0x1111,0x10,0));snapshots.append(snapshot())
        elif RING<=address<RING+0x100000:
            require(access==uc.UC_MEM_READ and width==4 and(address-RING)%16==8 and address<RING+count*16,'Unexpected descriptor I/O')
            supply(5,0x2222,address-RING,address);events.append((5,0x2222,address-RING,0));snapshots.append(snapshot())
        elif NODES<=address<NODES+0x100000:
            node=address&~0xff;offset=address-node;require(node in nodes,'Unknown node')
            if access==uc.UC_MEM_READ:
                require(width==4 and offset==noff,'Unexpected node read');supply(3,node,offset,address);events.append((3,node,offset,0))
            else:
                require(value==0 and(width==1 and status and 0x1c<=offset<=0x1f or width==4 and not status and offset==4),'Unexpected node write')
                events.append((4,node,offset,0));writes.append((node,offset,width,0))
            snapshots.append(snapshot())
        elif sp-0x100<=address<sp:
            touched=set(range(address,address+width))
            if access==uc.UC_MEM_WRITE:stack.update(touched)
            else:require(touched<=stack,'Unsupplied stack read')
        elif access==uc.UC_MEM_READ:require(address in fields and fields[address][0]==width,'Unsupplied context read')
        else:require((address,width) in [(CTX+0x82,2),(CTX+0xe8,2),(CTX+0x124,4),(CTX+0x128,4),
                    (CTX+0xf0,4),(LINKED+0x82,2),(LINKED+0xf0,4)],'Unexpected context write')
    cpu.hook_add(uc.UC_HOOK_CODE,instruction);cpu.hook_add(uc.UC_HOOK_MEM_READ|uc.UC_HOOK_MEM_WRITE,memory)
    cpu.emu_start(CODE,STOP,timeout=2000000,count=100000)
    require(cpu.reg_read(UC_ARM_REG_PC)==STOP and not remaining,'List leaf/read queue did not complete')
    return events,snapshots,writes,(*snapshot(),cpu.reg_read(UC_ARM_REG_R0)),len(instructions)

BRIDGE=r'''
#include "dma_rx_list.h"
#include "dma_rx_status_list.h"
struct bridge_io {axd_rx_list_io_fn io;void *opaque;};
static int status_io(void *opaque,const struct axd_rx_status_list_operation *op,uint32_t *value) {
 struct bridge_io *b=opaque;
 const struct axd_rx_list_operation converted={(enum axd_rx_list_kind)op->kind,op->identity,op->offset,op->value};
 return b->io(b->opaque,&converted,value);
}
int compare_list(unsigned status,uint32_t count,uint32_t in,uint32_t out,uint32_t cached,uint32_t force,
 uint32_t budget,uint32_t base,uint32_t tail,unsigned linked,axd_rx_list_io_fn io,void *opaque,uint64_t *result) {
 struct axd_dma_rx_list j;struct axd_dma_rx_status_list s;unsigned steps=0;struct bridge_io b={io,opaque};
 struct axd_rx_list_context c={0};struct axd_rx_status_list_context sc={0};
 c.ring=(struct axd_dma64_ring){count,0xfffff,base,in,out};c.core=130;c.minor=1;c.cached_current=(uint16_t)cached;
 c.head=0x22000000;c.tail=tail;c.available=99;c.status_identity=0x1111;c.descriptor_identity=0x2222;c.descriptor_bytes=count*16;
 c.status_owned=c.descriptor_owned=c.packet_owned=1;c.linked_mode=(uint8_t)linked;c.linked_known=c.linked_owned=1;
 c.linked_identity=0x20100000;c.linked_in=5;c.linked_available=88;
 if(!status){
  if(axd_dma_rx_list_begin(&j,budget,force,&c))return -1;result[8]=(uint64_t)(uintptr_t)&j;
  while(j.phase==AXD_RX_LIST_RUNNING && steps++<10000)axd_dma_rx_list_step(&j,io,opaque);
  c=j.context;result[7]=j.return_value;result[8]=j.attempted_ops;result[9]=j.completed_ops;
  if(j.phase!=AXD_RX_LIST_DONE || !j.return_valid)return -1;
 }else{
  sc.ring=c.ring;sc.core=130;sc.minor=1;sc.cached_current=c.cached_current;sc.head=c.head;sc.tail=tail;sc.available=99;
  sc.status_identity=0x1111;sc.descriptor_identity=0x2222;sc.descriptor_bytes=count*16;sc.status_owned=sc.descriptor_owned=sc.node_owned=1;
  if(axd_dma_rx_status_list_begin(&s,force,&sc))return -1;result[8]=(uint64_t)(uintptr_t)&s;
  while(s.phase==AXD_RX_STATUS_LIST_RUNNING && steps++<10000)axd_dma_rx_status_list_step(&s,status_io,&b);
  c.ring=s.context.ring;c.available=s.context.available;c.cached_current=s.context.cached_current;c.head=s.context.head;c.tail=s.context.tail;
  result[7]=s.return_value;result[8]=s.attempted_ops;result[9]=s.completed_ops;
  if(s.phase!=AXD_RX_STATUS_LIST_DONE || !s.return_valid)return -1;
 }
 result[0]=c.ring.in;result[1]=c.available;result[2]=c.cached_current;result[3]=c.head;result[4]=c.tail;
 result[5]=c.linked_in;result[6]=c.linked_available;return 0;
}
void snapshot_list(const void *opaque,unsigned status,uint32_t *out) {
 const struct axd_dma_rx_list *j=opaque;const struct axd_dma_rx_status_list *s=opaque;
 if(status){out[0]=s->context.ring.in;out[1]=s->context.available;out[2]=s->context.cached_current;out[3]=s->context.head;out[4]=s->context.tail;out[5]=5;out[6]=88;}
 else{out[0]=j->context.ring.in;out[1]=j->context.available;out[2]=j->context.cached_current;out[3]=j->context.head;out[4]=j->context.tail;out[5]=j->context.linked_in;out[6]=j->context.linked_available;}
}
'''
class Operation(ct.Structure):_fields_=[('kind',ct.c_int),('identity',ct.c_uint32),('offset',ct.c_uint32),('value',ct.c_uint32)]
CALLBACK=ct.CFUNCTYPE(ct.c_int,ct.c_void_p,ct.POINTER(Operation),ct.POINTER(ct.c_uint32))
def model(lib,status,count,inside,outside,cached,force,budget,current,terminated,base,linked,nodes,reads):
    del current,terminated
    events=[];snapshots=[];remaining=list(reads);failures=[];writes=[];out=(ct.c_uint64*10)()
    @CALLBACK
    def io(_,pointer,reply):
        op=pointer.contents;events.append((op.kind,op.identity,op.offset,op.value))
        snap=(ct.c_uint32*7)();lib.snapshot_list(ct.c_void_p(out[8]),status,snap);snapshots.append(tuple(snap))
        if op.kind in(1,3,5):
            if not remaining or remaining[0][:3]!=(op.kind,op.identity,op.offset):failures.append('Missing C read');return -1
            reply[0]=remaining.pop(0)[3]
        if op.kind==4:writes.append((op.identity,op.offset,1 if status else 4,op.value))
        return 0
    result=lib.compare_list(status,count,inside,outside,cached,force,budget,base,max(nodes),linked,io,None,out)
    require(result==0 and not remaining and not failures,'C list trace failed')
    require(out[8]==out[9]==len(events),'C operation accounting differs')
    return events,snapshots,writes,tuple(out[:8])

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('module',type=Path);args=parser.parse_args()
    root=Path(__file__).resolve().parents[1];data=args.module.read_bytes();require(hashlib.sha256(data).hexdigest()==MODULE_SHA256,'Unrecognized module')
    spec=importlib.util.spec_from_file_location('list_elf_helpers',root/'tests/check_phy_arm.py');helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
    selected=helper.functions(data,set(FUNCTIONS))
    for name,(size,digest) in FUNCTIONS.items():
        code,relocations=selected[name];require(len(code)==size and hashlib.sha256(code).hexdigest()==digest and not relocations,'Unreviewed list leaf')
    cases=[0,0];instructions=0;rejected=0
    with tempfile.TemporaryDirectory(prefix='axd-rx-list-arm-') as tmp:
        bridge=Path(tmp)/'bridge.c';library=Path(tmp)/'bridge.so';bridge.write_text(BRIDGE)
        subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11','-Wall','-Wextra','-Werror','-pedantic','-shared','-fPIC',
            '-I'+str(root/'driver'),str(bridge),str(root/'driver/dma_rx_list.c'),str(root/'driver/dma_rx_status_list.c'),str(root/'driver/dma_descriptor.c'),'-o',str(library)],check=True)
        lib=ct.CDLL(str(library));lib.compare_list.argtypes=[ct.c_uint32]*10+[CALLBACK,ct.c_void_p,ct.POINTER(ct.c_uint64)];lib.compare_list.restype=ct.c_int
        lib.snapshot_list.argtypes=[ct.c_void_p,ct.c_uint,ct.POINTER(ct.c_uint32)]
        for status in [0,1]:
            code=selected['_dma_getnext_sts_rxlist' if status else '_dma_getnext_rxlist'][0]
            for count in [0,2,4,8,512]:
                pairs=[(0,0)] if count==0 else list(itertools.product(range(2),repeat=2)) if count==2 else[(0,count-1),(count-2,2),(count-1,0),(1,1)]
                for inside,outside in pairs:
                    for same,force,budget,cur_same,terminated,linked in itertools.product([0,1],[0,1,0xffffffff],[0,1,3,0xffffffff] if not status else [0],[0,1],[0,1],[0,0x80] if not status else[0]):
                        cached=inside if same or not count else(inside+1)&(count-1);current=inside if cur_same else outside;base=0x12301000
                        nodes,reads=inputs(status,count,inside,outside,cached,force,budget,current,terminated,base)
                        arguments=(status,count,inside,outside,cached,force,budget,current,terminated,base,linked,nodes,reads)
                        arm=emulate(code,*arguments);c=model(lib,*arguments)
                        require(arm[:4]==c,f'ARM/C list mismatch {arguments[:11]}\nARM:{arm[:4]}\nC:{c}')
                        cases[status]+=1;instructions+=arm[4]
            nodes,reads=inputs(status,8,6,2,6,0,2,1,0,0x12301000)
            probe=(status,8,6,2,6,0,2,1,0,0x12301000,0,nodes,reads)
            for denial in ['unknown-context']:
                try:emulate(code,*probe,deny=denial)
                except ValueError:rejected+=1
                else:raise ValueError('Unknown context accepted')
            for queue in [[],reads+[(3,NODES,4,0)]]:
                try:emulate(code,*probe[:-1],queue)
                except ValueError:rejected+=1
                else:raise ValueError('Bad read queue accepted')
    print(json.dumps({'module_sha256':MODULE_SHA256,'packet_bulk_cases':cases[0],'status_bulk_cases':cases[1],
        'actual_arm_instructions':instructions,'functions':2,'calls':0,'relocations':0,'rejection_probes':rejected,
        'strict_memory_and_read_queues':True,'hardware_operations':0},sort_keys=True))
if __name__=='__main__':main()
