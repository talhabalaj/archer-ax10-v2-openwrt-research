#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare composed current DMA RX packet/status processing with actual ARM.

Loads four reviewed functions from a caller-supplied pinned immutable module.
Only actual reviewed internal REL CALLs bind to actual selected bodies. Every
reached OS/registered/unknown call fails. Cases use positive lengths and linear
packets, so OS delay, trim and free are deliberately outside this comparison.
Typed initialized synthetic memory and fresh status/descriptor queues are strict;
no target driver, hardware, implicit zero read or external success stub runs.
"""
import argparse
import copy
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
PACKET,STATUS,PLIST,SLIST='dma_rx','_dma_sts_rx','_dma_getnext_rxlist','_dma_getnext_sts_rxlist'
FUNCTIONS={PACKET:(532,'b351e00556dc0afadc2b43148e89602e0f7f011b2d3f025c2439580104229f6a'),
 STATUS:(324,'b874c626cfaed1a57188c3bd4da877fb12359261633ced7fd25cbd220437862c'),
 PLIST:(324,'74e18628df25fdbb4a62f289a0ff855a595796878342556825d21319dfc38450'),
 SLIST:(280,'cd8eb6ca8fde6cdb9e229f70984867a8118d9a74779b55041f436f7236c78ce3')}
INTERNAL={PACKET:{0x1c:PLIST,0x13c:PLIST,0x1c8:STATUS},STATUS:{0x14:SLIST},PLIST:{},SLIST:{}}
RELOCATIONS={PACKET:{0x1c:PLIST,0x48:'osl_delay',0x128:'___pskb_trim',0x13c:PLIST,
 0x198:'linux_pktfree',0x1c8:STATUS,0x200:'___pskb_trim'},STATUS:{0x14:SLIST,0x3c:'osl_delay'},PLIST:{},SLIST:{}}
CODE,DI,SDI,LDI,POUT,SOUT=0x10000000,0x20000000,0x20001000,0x20002000,0x20003000,0x20004000
PRING,SRING,PNODES,SNODES,PDATA,SDATA,STACK,PCHANNEL,SCHANNEL=0x21000000,0x21010000,0x22000000,0x22100000,0x23000000,0x23100000,0x30008000,0x31000000,0x31001000
STOP=CODE+0x80000
R32,R16,W32,W8,SY=1,2,3,4,5
# Snapshot includes both primary/linked DMA contexts, errors and both outputs.
SNAP=[(DI+0x82,2),(DI+0xf0,4),(DI+0xe8,2),(DI+0x124,4),(DI+0x128,4),
 (LDI+0x82,2),(LDI+0xf0,4),(DI+8,4),(SDI+0x82,2),(SDI+0xf0,4),(SDI+0xe8,2),
 (SDI+0x124,4),(SDI+0x128,4),(SDI+8,4),(POUT,4),(POUT+4,4),(POUT+8,4),(SOUT,4),(SOUT+4,4),(SOUT+8,4)]

def require(ok,message):
    if not ok:raise ValueError(message)

def load(data,repo):
    require(hashlib.sha256(data).hexdigest()==MODULE_SHA256,'Unrecognized stock module')
    spec=importlib.util.spec_from_file_location('rx_processing_elf',repo/'tests/check_phy_arm.py')
    helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper);blobs=helper.functions(data,set(FUNCTIONS))
    h=struct.unpack_from('<HHIIIIIHHHHHH',data,16);sections=[struct.unpack_from('<10I',data,h[5]+i*40) for i in range(h[11])]
    tables={};selected={}
    for number,s in enumerate(sections):
        if s[1]!=2:continue
        st=sections[s[6]];strings=data[st[4]:st[4]+st[5]];items=[]
        for pos in range(0,s[5],16):
            n,v,z,info,_,owner=struct.unpack_from('<IIIBBH',data,s[4]+pos)
            name=strings[n:strings.find(b'\0',n)].decode('ascii');items.append((name,v,z,info,owner))
            if name in FUNCTIONS:
                require(name not in selected,'Duplicate selected symbol');selected[name]=(owner,v,z)
        tables[number]=items
    functions={}
    for number,name in enumerate(sorted(FUNCTIONS)):
        code,sites=blobs[name];size,digest=FUNCTIONS[name]
        require(len(code)==size and hashlib.sha256(code).hexdigest()==digest,'Modified selected helper bytes')
        owner,value,length=selected[name];relocations={}
        for section in sections:
            if section[1] not in(4,9) or section[7]!=owner:continue
            require(section[9]==(8 if section[1]==9 else 12),'Unknown relocation stride')
            for pos in range(0,section[5],section[9]):
                at,info=struct.unpack_from('<II',data,section[4]+pos)
                if not value<=at<value+length:continue
                target=tables[section[6]][info>>8];off=at-value
                require(section[1]==9 and info&255==28 and RELOCATIONS[name].get(off)==target[0],'Unreviewed actual relocation')
                if off in INTERNAL[name]:require((target[4],target[1],target[2])==selected[target[0]],'Wrong selected CALL target')
                relocations[off]=target[0]
        require(set(relocations)==sites==set(RELOCATIONS[name]),'Relocation-site disagreement')
        functions[name]={'code':code,'address':CODE+number*0x10000,'relocations':relocations}
    return functions

class Memory:
    def __init__(self):self.bytes={};self.readable=set();self.writable=set()
    def field(self,address,width,value,write=False):
        require((address,width) not in self.readable,'Duplicate typed field')
        self.readable.add((address,width));self.store(address,width,value)
        if write:self.writable.add((address,width))
    def store(self,address,width,value):
        require(0<=value<1<<(8*width),'Value wider than typed store')
        for k in range(width):self.bytes[address+k]=(value>>(8*k))&255
    def read(self,address,width):
        require((address,width) in self.readable,'Unknown typed memory read')
        require(all(address+k in self.bytes for k in range(width)),'Uninitialized memory read')
        return sum(self.bytes[address+k]<<(8*k) for k in range(width))
    def write(self,address,width,value):
        require((address,width) in self.writable,'Unknown typed memory write');self.store(address,width,value)
    def snapshot(self):return tuple(self.read(a,w) for a,w in SNAP)

def scenario(mode,count,inside,kind,status_nodes,cached_status,linked,offset=4,output_count=7):
    """Construct independent explicit objects, without calling the C model."""
    m=Memory();normal=kind.startswith('normal');number=int(kind[-1]) if normal else 3 if kind=='fragment2' or kind=='fragment1' else 2 if kind=='refill' else 1
    if mode:number=0
    completed=number;budget=1 if kind=='refill' else number
    outside=(inside+min(number+1,count-1))&(count-1) if count else 0
    current=(inside+completed)&(count-1) if count else 0
    phead=PNODES if number else 0;ptail=PNODES+(number-1)*0x1000 if number else 0
    sn=status_nodes;sin=6;sout=(sin+sn)&7;scache=sin if cached_status else sout
    shead=SNODES if sn else 0;stail=SNODES+(sn-1)*0x1000 if sn else 0
    cfg=[count,inside,outside,inside,phead,ptail,budget,linked,offset,64,4,1 if sn or mode else 0,8,sin,sout,scache,shead,stail,output_count]
    # Main and status DI metadata: each subsequent live load is explicitly typed.
    for base,ring_count,ri,ro,cache,head,tail,ring,channel in[
        (DI,count,inside,outside,inside,phead,ptail,PRING,PCHANNEL),(SDI,8,sin,sout,scache,shead,stail,SRING,SCHANNEL)]:
        for off,width,value,write in[(0x80,2,ring_count,False),(0x82,2,ri,True),(0x84,2,ro,False),(0xe8,2,cache,True),
            (0xf0,4,99,True),(0x124,4,head,True),(0x128,4,tail,True),(0x54,4,ring,False),
            (0x4c,4,channel,False),(0xe4,4,0x1fff,False),(0xa4,4,0x100,False),(8,4,0xffffffff,True)]:m.field(base+off,width,value,write)
    for off,width,value in[(0xb4,4,offset),(0xa8,2,64),(4,4,4),(0x2c,4,0x77),(0xfc,4,SDI if cfg[11] else 0),(0xf5,1,linked),(0xf8,4,LDI)]:m.field(DI+off,width,value)
    m.field(LDI+0x82,2,5,True);m.field(LDI+0xf0,4,88,True)
    for base,values in[(POUT,[0xaaaa,0xbbbb,output_count]),(SOUT,[0xcccc,0xdddd,3])]:
        for k,v in enumerate(values):m.field(base+k*4,4,v,True)
    for i in range(number):
        node=PNODES+i*0x1000;data=PDATA+i*0x1000;next_node=node+0x1000 if i+1<number else 0
        values={0:0x12345678,4:next_node,0x28:data,0x2c:0x87654321,0xe8:0,0x15c:0xabcdef00}
        for off,value in values.items():m.field(node+off,4,value,off in(0,4,0x2c,0x15c))
        length=[1,10,20][i%3];fragments=0
        if i==0 and not normal:
            length=150 if kind=='fragment2' else 100;fragments=2 if kind=='fragment2' else 1
        m.field(data,4,(fragments<<24)|length);m.readable.add((data,2))
    for i in range(sn):
        node=SNODES+i*0x1000;data=SDATA+i*0x1000
        m.field(node,4,data);m.field(node+0x1c,4,node+0x1000 if i+1<sn else 0,True)
        for b in range(4):m.writable.add((node+0x1c+b,1))
        m.field(data,2,i+1)
    queues={PCHANNEL+0x10:[],SCHANNEL+0x10:[]}
    if not mode and count:
        queues[PCHANNEL+0x10]=[0xf0000100+current*16]
        if kind=='refill':queues[PCHANNEL+0x10].append(0xa0000100+current*16)
        for k in range(number):queues[PRING+((inside+k)&(count-1))*16+8]=[0xdead0000+k]
    if cfg[11] and scache==sin:queues[SCHANNEL+0x10]=[0xb0000100+sout*16]
    if cfg[11]:
        for k in range(sn):queues[SRING+((sin+k)&7)*16+8]=[0xbeef0000+k]
    return {'mode':mode,'cfg':cfg,'memory':m,'queues':queues,'kind':kind}

def emulate(functions,case,deny=None):
    import unicorn as uc
    from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC
    require(uc.__version__=='2.1.4','Use pinned Unicorn2.1.4')
    cpu=uc.Uc(uc.UC_ARCH_ARM,uc.UC_MODE_ARM|uc.UC_MODE_LITTLE_ENDIAN)
    for address,size in[(DI,0x10000),(PRING,0x20000),(PNODES,0x200000),(PDATA,0x200000),(STACK-0x8000,0x10000),(PCHANNEL,0x10000)]:cpu.mem_map(address,size)
    loaded={};resolved={}
    for name,item in functions.items():
        code=bytearray(item['code']);start=item['address']
        for off,target in INTERNAL[name].items():
            require(item['relocations'][off]==target,'CALL target mismatch');word=struct.unpack_from('<I',code,off)[0]
            require(word>>28!=15 and(word>>25)&7==5 and word&(1<<24),'Non-ARM CALL')
            addend=(word&0xffffff)<<2
            if addend&(1<<25):addend-=1<<26
            require(addend==-8,'Unexpected CALL addend');dest=functions[target]['address'];delta=dest+addend-start-off
            require(delta%4==0 and -(1<<25)<=delta<1<<25,'CALL binding overflow')
            struct.pack_into('<I',code,off,(word&0xff000000)|((delta>>2)&0xffffff));resolved[start+off]=dest
        cpu.mem_map(start,0x1000);cpu.mem_write(start,bytes(code));loaded[name]=(start,start+len(code))
    if deny=='unbound-call':resolved.clear()
    memory=copy.deepcopy(case['memory']);queues=copy.deepcopy(case['queues'])
    if deny=='unknown-packet':memory.readable.discard((PNODES+0x28,4))
    if deny=='missing-descriptor':queues.pop(PRING+case['cfg'][1]*16+8,None)
    if deny=='unused-read':queues[PRING+0xff8]=[0x1234]
    for address,value in memory.bytes.items():cpu.mem_write(address,bytes([value]))
    args=(SDI,SOUT,0,0) if case['mode'] else(DI,POUT,SOUT if case['cfg'][11] else 0,case['cfg'][6])
    for register,value in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),args):cpu.reg_write(register,value)
    cpu.reg_write(UC_ARM_REG_SP,STACK);cpu.reg_write(UC_ARM_REG_LR,STOP)
    stack=set();events=[];snapshots=[];executed=[];calls=[];owner=[None]
    def event(path,kind,address,width,value=0):
        events.append((path,kind,address,width,value));snapshots.append(memory.snapshot())
    def instruction(engine,address,width,_):
        require(width==4,'Unexpected instruction width');matches=[name for name,(lo,hi) in loaded.items() if lo<=address<hi]
        require(len(matches)==1,'Unknown instruction target');name=matches[0];owner[0]=name;off=address-functions[name]['address']
        word=int.from_bytes(engine.mem_read(address,4),'little')
        if off in functions[name]['relocations']:
            require(address in resolved,'Reached unknown/external or unbound call');calls.append((name,off,functions[name]['relocations'][off]))
        if (word>>25)&7==5 and word&(1<<24):require(address in resolved,'Unknown direct call')
        require((word&0x0ffffff0)!=0x012fff30,'Unknown registered/indirect call')
        if word==0xf57ff04f:event(1 if name==PLIST else 3,SY,PCHANNEL if name==PLIST else SCHANNEL,0)
        executed.append(address)
    def access(engine,kind,address,width,value,_):
        if STACK-0x2000<=address<STACK:
            touched=set(range(address,address+width))
            if kind==uc.UC_MEM_WRITE:stack.update(touched)
            else:require(touched<=stack,'Uninitialized stack read')
            return
        name=owner[0];path={PACKET:2,PLIST:1,STATUS:4,SLIST:3}[name]
        observed=(PNODES<=address<PNODES+0x200000 or PDATA<=address<PDATA+0x200000 or
                  PRING<=address<PRING+0x20000 or PCHANNEL<=address<PCHANNEL+0x10000)
        if kind==uc.UC_MEM_READ:
            if address in queues:
                require(width==4 and queues[address],'Missing fresh status/descriptor observation');sample=queues[address].pop(0)
                engine.mem_write(address,sample.to_bytes(4,'little'))
            else:sample=memory.read(address,width)
            if observed:event(path,R16 if width==2 else R32,address,width)
        else:
            value&=(1<<(8*width))-1;memory.write(address,width,value)
            if observed:event(path,W8 if width==1 else W32,address,width,value)
    cpu.hook_add(uc.UC_HOOK_CODE,instruction);cpu.hook_add(uc.UC_HOOK_MEM_READ|uc.UC_HOOK_MEM_WRITE,access)
    entry=STATUS if case['mode'] else PACKET
    cpu.emu_start(functions[entry]['address'],STOP,timeout=2000000,count=200000)
    require(cpu.reg_read(UC_ARM_REG_PC)==STOP and not any(queues.values()),'Processing/read queues did not complete')
    return events,snapshots,memory.snapshot(),memory.bytes,len(executed),calls

BRIDGE=r'''
#include "rx_packets.h"
#include "rx_status.h"
typedef int (*normalized_io)(void *,unsigned,unsigned,uint32_t,unsigned,uint32_t,uint32_t *);
struct bridge {unsigned mode;struct axd_rx_packets p;struct axd_rx_status s;normalized_io io;void *opaque;};
static int list_io(struct bridge *b,unsigned status,const struct axd_rx_list_operation *op,uint32_t *value) {
 unsigned kind=op->kind==AXD_RX_LIST_FULL_BARRIER?5:op->kind==AXD_RX_LIST_PACKET_NEXT_WRITE32?(status?4:3):1;
 unsigned width=kind==5?0:kind==4?1:4;
 return b->io(b->opaque,status?3:1,kind,op->identity+op->offset,width,op->value,value);
}
static int status_io(void *opaque,const struct axd_rx_status_operation *op,uint32_t *value) {
 struct bridge *b=opaque;
 if(op->kind==AXD_RS_LIST){
  const struct axd_rx_list_operation list={(enum axd_rx_list_kind)op->list.kind,op->list.identity,op->list.offset,op->list.value};
  return list_io(b,1,&list,value);
 }
 if(op->kind==AXD_RS_NODE_READ32)return b->io(b->opaque,4,1,op->identity+op->offset,4,0,value);
 if(op->kind==AXD_RS_NODE_WRITE32)return b->io(b->opaque,4,3,op->identity+op->offset,4,op->value,value);
 if(op->kind==AXD_RS_NODE_WRITE8)return b->io(b->opaque,4,4,op->identity+op->offset,1,op->value,value);
 if(op->kind==AXD_RS_DATA_READ16 || op->kind==AXD_RS_INDEX_READ16)return b->io(b->opaque,4,2,op->identity+op->offset,2,0,value);
 return -1;
}
static int packet_io(void *opaque,const struct axd_rx_packets_operation *op,uint32_t *value) {
 static const uint32_t offsets[]={0,0,4,0x28,0x2c,0xe8,0x15c};struct bridge *b=opaque;
 if(op->kind==AXD_RP_LIST)return list_io(b,0,&op->list,value);
 if(op->kind==AXD_RP_STATUS)return status_io(b,&op->status,value);
 if(op->kind==AXD_RP_PACKET_READ32 || op->kind==AXD_RP_PACKET_WRITE32){
  if(op->field<AXD_RP_FRAGMENT || op->field>AXD_RP_TAIL)return -1;
  return b->io(b->opaque,2,op->kind==AXD_RP_PACKET_READ32?1:3,op->packet+offsets[op->field],4,op->value,value);
 }
 if(op->kind==AXD_RP_DATA_READ16 || op->kind==AXD_RP_DATA_READ32)
  return b->io(b->opaque,2,op->kind==AXD_RP_DATA_READ16?2:1,op->data,op->kind==AXD_RP_DATA_READ16?2:4,0,value);
 return -1;
}
static void snapshot(const struct bridge *b,uint32_t *out) {
 const struct axd_rx_list_context *p=&b->p.context.list;
 const struct axd_rx_status_context *s=b->mode?&b->s.context:&b->p.context.status;
 const struct axd_rx_status_output *so=b->mode?&b->s.output:&b->p.context.status_output;
 out[0]=p->ring.in;out[1]=p->available;out[2]=p->cached_current;out[3]=p->head;out[4]=p->tail;
 out[5]=p->linked_in;out[6]=p->linked_available;out[7]=b->p.context.error_count;
 out[8]=s->list.ring.in;out[9]=s->list.available;out[10]=s->list.cached_current;out[11]=s->list.head;out[12]=s->list.tail;out[13]=s->error_count;
 out[14]=b->p.output.head;out[15]=b->p.output.tail;out[16]=b->p.output.count;out[17]=so->head;out[18]=so->tail;out[19]=so->count;
}
void snapshot_processing(const void *opaque,uint32_t *out){snapshot(opaque,out);}
int compare_processing(unsigned mode,const uint32_t *cfg,normalized_io io,void *opaque,uint64_t *out) {
 struct bridge b={0};struct axd_rx_packets_context c={0};struct axd_rx_packets_request r={0};unsigned steps=0;uint32_t final[20];
 c.list.ring=(struct axd_dma64_ring){cfg[0],0x1fff,0x100,cfg[1],cfg[2]};c.list.core=130;c.list.minor=1;c.list.cached_current=(uint16_t)cfg[3];
 c.list.head=cfg[4];c.list.tail=cfg[5];c.list.available=99;c.list.status_identity=0x31000000;c.list.descriptor_identity=0x21000000;
 c.list.descriptor_bytes=cfg[0]*16;c.list.status_owned=c.list.descriptor_owned=c.list.packet_owned=1;
 c.list.linked_mode=(uint8_t)cfg[7];c.list.linked_known=c.list.linked_owned=1;c.list.linked_identity=0x20002000;c.list.linked_in=5;c.list.linked_available=88;
 c.receive_offset=(struct axd_wake_u32){cfg[8],1};c.buffer_size=(struct axd_wake_u32){cfg[9],1};c.flags=(struct axd_wake_u32){cfg[10],1};
 c.os_context=(struct axd_wake_u32){0x77,1};c.status_context=(struct axd_wake_u32){cfg[11]?0x20001000:0,1};
 c.status_binding=c.status_context;c.status_output_known=1;c.status_output=(struct axd_rx_status_output){0xcccc,0xdddd,3};
 c.work_limit=100;c.error_count=0xffffffff;c.packets_owned=c.data_owned=c.os_owned=1;
 c.status.list.ring=(struct axd_dma64_ring){cfg[12],0x1fff,0x100,cfg[13],cfg[14]};c.status.list.core=130;c.status.list.minor=1;c.status.list.cached_current=(uint16_t)cfg[15];
 c.status.list.head=cfg[16];c.status.list.tail=cfg[17];c.status.list.available=99;c.status.list.status_identity=0x31001000;c.status.list.descriptor_identity=0x21010000;
 c.status.list.descriptor_bytes=cfg[12]*16;c.status.list.status_owned=c.status.list.descriptor_owned=c.status.list.node_owned=1;
 c.status.error_count=0xffffffff;c.status.work_limit=100;c.status.nodes_owned=c.status.data_owned=c.status.os_owned=1;
 r.budget=cfg[6];r.status_output=cfg[11]?0x20004000:0;r.output=(struct axd_rx_packets_summary){0xaaaa,0xbbbb,cfg[18]};
 b.mode=mode;b.io=io;b.opaque=opaque;
 if(axd_rx_packets_begin(&b.p,&r,&c))return -1;
 if(mode && axd_rx_status_begin(&b.s,&c.status_output,&c.status))return -1;
 out[20]=(uint64_t)(uintptr_t)&b;
 if(mode){
  while(b.s.phase==AXD_RS_RUNNING && steps++<10000)axd_rx_status_step(&b.s,status_io,&b);
  if(b.s.phase!=AXD_RS_TRACE_COMPLETE)return -1;out[21]=b.s.attempted_ops;out[22]=b.s.completed_ops;
 }else{
  while(b.p.phase==AXD_RP_RUNNING && steps++<10000)axd_rx_packets_step(&b.p,packet_io,&b);
  if(b.p.phase!=AXD_RP_TRACE_COMPLETE)return -1;out[21]=b.p.attempted_ops;out[22]=b.p.completed_ops;
 }
 snapshot(&b,final);for(unsigned i=0;i<20;i++)out[i]=final[i];out[20]=0;return 0;
}
'''
CALLBACK=ct.CFUNCTYPE(ct.c_int,ct.c_void_p,ct.c_uint,ct.c_uint,ct.c_uint32,ct.c_uint,ct.c_uint32,ct.POINTER(ct.c_uint32))
def model(lib,case):
    memory=copy.deepcopy(case['memory']);queues=copy.deepcopy(case['queues']);events=[];snapshots=[];failures=[];out=(ct.c_uint64*23)()
    @CALLBACK
    def io(_,path,kind,address,width,value,reply):
        events.append((path,kind,address,width,value));snap=(ct.c_uint32*20)();lib.snapshot_processing(ct.c_void_p(out[20]),snap);snapshots.append(tuple(snap))
        try:
            if kind in(R32,R16):
                if address in queues:
                    require(width==4 and queues[address],'Missing C fresh read');reply[0]=queues[address].pop(0)
                else:reply[0]=memory.read(address,width)
            elif kind in(W32,W8):memory.write(address,width,value)
            else:require(kind==SY and width==0,'Unknown C operation')
        except ValueError as error:failures.append(str(error));return -1
        return 0
    cfg=(ct.c_uint32*len(case['cfg']))(*case['cfg'])
    status=lib.compare_processing(case['mode'],cfg,io,None,out)
    require(status==0 and not failures and not any(queues.values()),f'C processing failed {failures}')
    require(out[21]==out[22]==len(events),'C callback accounting differs')
    # Physical packet/status memory is separate from modelled context snapshots.
    physical={a:v for a,v in memory.bytes.items() if PNODES<=a<PNODES+0x200000 or PDATA<=a<PDATA+0x200000}
    return events,snapshots,tuple(out[:20]),physical

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('module',type=Path);args=parser.parse_args()
    root=Path(__file__).resolve().parents[1];functions=load(args.module.read_bytes(),root);cases=[]
    for count,kind,linked,sn,same,wrap in itertools.product([4,8],['normal0','normal1','normal2','fragment1','fragment2','refill'],[0,0x80],[0,1,2],[0,1],[0,1]):
        inside=count-1 if wrap else 0;cases.append(scenario(0,count,inside,kind,sn,same,linked))
    for sn,same in itertools.product([0,1,2,3],[0,1]):cases.append(scenario(1,0,0,'normal0',sn,same,0))
    for offset,count_value in itertools.product([0,1,4,16],[0,0xffffffff]):cases.append(scenario(0,8,6,'normal2',2,1,0x80,offset,count_value))
    instructions=0;calls={};refills=0
    with tempfile.TemporaryDirectory(prefix='axd-rx-processing-arm-') as tmp:
        bridge=Path(tmp)/'bridge.c';library=Path(tmp)/'bridge.so';bridge.write_text(BRIDGE)
        sources=['rx_packets.c','rx_status.c','dma_rx_list.c','dma_rx_status_list.c','dma_descriptor.c']
        subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11','-Wall','-Wextra','-Werror','-pedantic','-shared','-fPIC','-I'+str(root/'driver'),str(bridge)]+[str(root/'driver'/name) for name in sources]+['-o',str(library)],check=True)
        lib=ct.CDLL(str(library));lib.compare_processing.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint32),CALLBACK,ct.c_void_p,ct.POINTER(ct.c_uint64)];lib.compare_processing.restype=ct.c_int
        lib.snapshot_processing.argtypes=[ct.c_void_p,ct.POINTER(ct.c_uint32)]
        for number,case in enumerate(cases):
            arm=emulate(functions,case);c=model(lib,case);physical={a:v for a,v in arm[3].items() if PNODES<=a<PNODES+0x200000 or PDATA<=a<PDATA+0x200000}
            require((arm[0],arm[1],arm[2],physical)==c,f'ARM/C processing mismatch case{number} config{case["cfg"]}\nARM:{arm[:3]}\nC:{c[:3]}')
            instructions+=arm[4];refills+=any(name==PACKET and off==0x13c for name,off,_ in arm[5])
            for name,off,target in arm[5]:
                key=f'{name}+0x{off:x}->{target}';calls[key]=calls.get(key,0)+1
        probe=scenario(0,8,6,'refill',2,1,0)
        rejected=0
        for denial in ['unbound-call','unknown-packet','missing-descriptor','unused-read']:
            try:emulate(functions,probe,deny=denial)
            except ValueError:rejected+=1
            else:raise ValueError(f'Rejection probe accepted: {denial}')
        # A zero length would need real osl_delay. An actual non-linear packet
        # would need real ___pskb_trim. Neither is replaced with success.
        for route in ['delay','trim','free']:
            hostile=copy.deepcopy(probe)
            if route=='delay':hostile['memory'].store(PDATA,4,0)
            elif route=='trim':hostile['memory'].store(PNODES+0xe8,4,1)
            else:hostile['memory'].store(DI+4,4,0x40)
            try:emulate(functions,hostile)
            except ValueError as error:
                require('unknown/external' in str(error),'External-call probe rejected for unrelated reason');rejected+=1
            else:raise ValueError('Reached external helper accepted')
    print(json.dumps({'module_sha256':MODULE_SHA256,'cases':len(cases),'actual_arm_instructions':instructions,
        'selected_functions':4,'bound_actual_call_counts':calls,'refill_cases':refills,'rejection_probes':rejected,
        'strict_typed_memory_and_fresh_reads':True,'external_helper_stubs':0,'hardware_operations':0},sort_keys=True))
if __name__=='__main__':main()
