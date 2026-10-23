#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare closed current status-fill and BMAC wrapper executions with real ARM.

Executes actual wrapper tails, status fill and its idle child. The default
closed matrix uses empty pools/no-work/disabled and disabled normal fill. The
optional --hnd module closes positive pools using the real imported address
converter and actual WL descriptor updater. Unknown external calls are rejected;
no implementation is substituted with a success stub. No hardware.
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
EXT,DD='osl_virt_to_phys','dma64_dd_upd'
HND_SHA256='d05ff061b3a636630955a13fdd20a953f3dfb07ee92904a987dd4c77b648b45a'
WRAPPER,STATUS,NORMAL,IDLE='wlc_bmac_dma_rxfill','dma_sts_rxfill','dma_rxfill','dma_rxidle.part.0'
IDENTITIES={WRAPPER:(52,'d009380b30974d1139ed559a5b8340cd30e8ad853f548b01d179ec140f66273f'),
 STATUS:(456,'abdca8db4dc2d32936d9ac7e874efde859779faa91ba799dea9584b036cc246e'),
 NORMAL:(944,'5a81a10f8ebb3aea0fd17dbdfc7050c5b2fa27b3aa9ea4e6df9c5cd873c0c9fb'),
 IDLE:(84,'7d506ff07c05267279dc4402613f1bafe64818a3f5c660bfbf03e49a28d10e56')}
SITES={WRAPPER:{0x28,0x30},STATUS:{0xcc,0x128,0x1c0},NORMAL:{0xa4,0xd4,0x13c,0x140,0x18c,0x1f8,0x2a8,0x308,0x378,0x384,0x39c},IDLE:set()}
BINDINGS={WRAPPER:{0x28:(29,NORMAL),0x30:(29,STATUS)},STATUS:{0x1c0:(28,IDLE)},NORMAL:{},IDLE:{}}
DESCRIPTORS=0x24000000
CODE,HW,WLC,PUB,DI,POOL,CHANNEL,NODE,DATA,STACK=0x10000000,0x20000000,0x20001000,0x20002000,0x20003000,0x20004000,0x21000000,0x22000000,0x23000000,0x30008000
STOP=CODE+0x80000

def require(ok,message):
    if not ok:raise ValueError(message)

def load(data,repo):
    require(hashlib.sha256(data).hexdigest()==MODULE_SHA256,'Unknown module')
    spec=importlib.util.spec_from_file_location('status_fill_elf',repo/'tests/check_phy_arm.py');helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
    blobs=helper.functions(data,set(IDENTITIES));h=struct.unpack_from('<HHIIIIIHHHHHH',data,16);sections=[struct.unpack_from('<10I',data,h[5]+i*40) for i in range(h[11])]
    tables={};selected={}
    for number,s in enumerate(sections):
        if s[1]!=2:continue
        st=sections[s[6]];strings=data[st[4]:st[4]+st[5]];items=[]
        for pos in range(0,s[5],16):
            n,v,z,info,_,owner=struct.unpack_from('<IIIBBH',data,s[4]+pos);name=strings[n:strings.find(b'\0',n)].decode('ascii');items.append((name,v,z,info,owner))
            if name in IDENTITIES:require(name not in selected,'Duplicate function');selected[name]=(owner,v,z)
        tables[number]=items
    result={}
    for number,name in enumerate(sorted(IDENTITIES)):
        code,sites=blobs[name];size,digest=IDENTITIES[name];require(len(code)==size and hashlib.sha256(code).hexdigest()==digest and sites==SITES[name],'Unreviewed helper bytes/sites')
        owner,value,length=selected[name];relocations={}
        for section in sections:
            if section[1] not in(4,9) or section[7]!=owner:continue
            for pos in range(0,section[5],section[9]):
                at,info=struct.unpack_from('<II',data,section[4]+pos)
                if not value<=at<value+length:continue
                off=at-value;target=tables[section[6]][info>>8];relocations[off]=(info&255,target[0])
                require(section[1]==9,'Unreviewed RELA')
                if off in BINDINGS[name]:
                    require(relocations[off]==BINDINGS[name][off],'Wrong actual branch symbol/type')
                    if target[0]==EXT:require((target[4],target[1],target[2])==(0,0,0),'Not an actual undefined imported address converter')
                    else:require((target[4],target[1],target[2])==selected[target[0]],'Wrong actual local branch binding')
        require(set(relocations)==sites,'Relocation disagreement');result[name]={'code':code,'address':CODE+number*0x10000,'relocations':relocations}
    return result

class Memory:
    def __init__(self):self.bytes={};self.readable=set();self.writable=set()
    def field(self,a,w,v,write=False):
        require((a,w) not in self.readable,'Duplicate typed field');self.readable.add((a,w));self.store(a,w,v)
        if write:self.writable.add((a,w))
    def store(self,a,w,v):
        require(0<=v<1<<(w*8),'Value width');self.bytes.update({a+k:(v>>(8*k))&255 for k in range(w)})
    def read(self,a,w):
        require((a,w) in self.readable and all(a+k in self.bytes for k in range(w)),'Unknown/uninitialized read')
        return sum(self.bytes[a+k]<<(8*k) for k in range(w))
    def write(self,a,w,v):require((a,w) in self.writable,'Unknown typed write');self.store(a,w,v)
    def snapshot(self):return tuple(self.read(a,w) for a,w in[(DI+0x84,2),(DI+0xc,4),(DI+0x124,4),(DI+0x128,4),(POOL+8,4)])

def case(fifo=3,byte=1,disabled=0,count=8,posts=2,mask=0x1fff,status=0xa0000170,producer=0xf0000170,wrap=False):
    m=Memory();inside,outside=(6,7) if count else(0,0);counter=0xffffffff if wrap else 99;failures=0xffffffff if wrap else 9
    for a,w,v,write in[(HW,4,WLC,False),(WLC,4,PUB,False),(PUB+0x1c0,1,byte,False),(HW+(fifo+6)*4,4,DI,False),
        (DI+0x104,1,disabled,False),(DI+4,4,0,False),(DI+0xf4,1,0,False),(DI+0x80,2,count,False),(DI+0x82,2,inside,False),
        (DI+0x84,2,outside,True),(DI+0xb0,4,posts,False),(DI+0xc,4,counter,True),(DI+0x100,4,POOL,False),
        (DI+0x124,4,0,True),(DI+0x128,4,0,True),(DI+0x4c,4,CHANNEL,False),(DI+0xa4,4,0x100,False),(DI+0xe4,4,mask,False),
        (POOL,4,0,True),(POOL+8,4,failures,True),(CHANNEL+4,4,producer,True)]:m.field(a,w,v,write)
    selected=fifo==3 and byte&1;needed=max(0,min(count,posts)-((outside-inside)&(count-1) if count else 0));q={}
    if selected and not disabled and needed:
        q={POOL:[0],POOL+8:[failures],CHANNEL+0x10:[status],CHANNEL+4:[producer]}
    return {'fifo':fifo,'byte':byte,'disabled':disabled,'count':count,'posts':posts,'mask':mask,'memory':m,'queues':q,'selected':bool(selected)}

def positive_case(nodes,posts,parity,head,low,high,wrap):
    c=case(posts=posts,wrap=wrap);c['positive']=True;c['flags']=parity;c['low']=low;c['high']=high;c['buffer_size']=64
    m=c['memory'];m.store(DI+4,4,parity);m.store(DI+0x128,4,NODE+0x8000 if head else 0);m.store(DI+0x124,4,NODE+0x8000 if head else 0)
    for a,v,w in[(DI+0x54,DESCRIPTORS,4),(DI+0xa8,64,2),(DI+0xc0,low,4),(DI+0xc4,high,4),(POOL+4,1 if wrap else 9,4)]:m.field(a,w,v,a==POOL+4)
    if head:m.field(NODE+0x801c,4,0,True)
    q={POOL:[NODE],POOL+4:[1 if wrap else 9]};c['queues']=q
    for i in range(nodes):
        node=NODE+i*0x100;data=DATA+i*0x100;next_node=NODE+(i+1)*0x100 if i+1<nodes else 0
        m.field(node,4,data);m.field(node+0x1c,4,next_node,True);m.writable.add((node+0x18,2))
        for b in range(4):m.writable.add((node+0x1c+b,1))
        m.field(data,4,0xabcdef01,True);q[node]=[data,data];q[node+0x1c]=[next_node]
        if i:q[POOL].append(node);q[POOL+4].append((1 if wrap else 9)-i & 0xffffffff)
        slot=(7+i)&7
        for off in[8,12,0,4]:
            m.field(DESCRIPTORS+slot*16+off,4,0xa5a5a5a5,True)
            if parity:q[DESCRIPTORS+slot*16+off]=[i+1 if off==8 or (parity==3 and off==12) else 0]
    if posts-1>nodes:q[POOL].append(0);q[POOL+8]=[0xffffffff if wrap else 9]
    return c

def emulate(functions,c,deny=None):
    import unicorn as uc
    from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC
    require(uc.__version__=='2.1.4','Use pinned Unicorn2.1.4');cpu=uc.Uc(uc.UC_ARCH_ARM,uc.UC_MODE_ARM|uc.UC_MODE_LITTLE_ENDIAN)
    for address,size in[(HW,0x10000),(CHANNEL,0x10000),(NODE,0x10000),(DATA,0x10000),(DESCRIPTORS,0x10000),(STACK-0x8000,0x10000)]:cpu.mem_map(address,size)
    loaded={};resolved={}
    for name,item in functions.items():
        raw=bytearray(item['code']);start=item['address']
        for off,(kind,target) in BINDINGS.get(name,{}).items():
            word=struct.unpack_from('<I',raw,off)[0];require((word>>25)&7==5 and bool(word&(1<<24))==(kind==28),'Unreviewed branch opcode')
            add=(word&0xffffff)<<2
            if add&(1<<25):add-=1<<26
            require(add==-8,'Unreviewed branch addend');destination=functions[target]['address'];delta=destination+add-start-off
            struct.pack_into('<I',raw,off,(word&0xff000000)|((delta>>2)&0xffffff));resolved[start+off]=destination
        cpu.mem_map(start,0x1000);cpu.mem_write(start,bytes(raw));loaded[name]=(start,start+len(raw))
    if deny=='unbound-tail':resolved.clear()
    memory=copy.deepcopy(c['memory']);queues=copy.deepcopy(c['queues'])
    if deny=='unknown-public':memory.readable.remove((PUB+0x1c0,1))
    if deny=='missing-read':queues[CHANNEL+4]=[]
    if deny=='unused-read':queues[CHANNEL+0x18]=[0x1234]
    if deny=='reached-translation':
        queues[POOL]=[NODE];queues[POOL+4]=[1];queues[NODE]=[DATA,DATA];queues[NODE+0x1c]=[0];memory.field(POOL+4,4,1,True)
        memory.field(NODE,4,DATA);memory.field(NODE+0x1c,4,0,True);memory.field(DATA,4,0xabcdef01,True)
        for b in range(4):memory.writable.add((NODE+0x1c+b,1))
    for a,v in memory.bytes.items():cpu.mem_write(a,bytes([v]))
    cpu.reg_write(UC_ARM_REG_R0,HW);cpu.reg_write(UC_ARM_REG_R1,c['fifo']);cpu.reg_write(UC_ARM_REG_SP,STACK);cpu.reg_write(UC_ARM_REG_LR,STOP)
    stack=set();events=[];snapshots=[];instructions=[];visited=set();branches=[];owner=[None]
    def event(path,kind,resource,identity,off,value=0):events.append((path,kind,resource,identity,off,value));snapshots.append(memory.snapshot())
    def instruction(engine,a,w,_):
        names=[n for n,(lo,hi) in loaded.items() if lo<=a<hi];require(w==4 and len(names)==1,'Unknown execution');name=names[0];owner[0]=name;visited.add(name);off=a-functions[name]['address']
        if name==EXT and off==0:event(3,5,3,engine.reg_read(UC_ARM_REG_R0),0)
        if off in functions[name]['relocations']:
            require(a in resolved,'Reached unknown/external or unbound branch');branches.append((name,off,functions[name]['relocations'][off][1]))
        word=int.from_bytes(engine.mem_read(a,4),'little');require(not((word>>25)&7==5 and word&(1<<24)) or a in resolved,'Unknown direct CALL')
        require((word&0x0ffffff0)!=0x012fff30,'Unknown indirect CALL')
        if word in(0xf57ff04e,0xf57ff04f):event(3,6 if word==0xf57ff04e else 7,5,CHANNEL,0)
        instructions.append(a)
    def access(engine,kind,a,w,v,_):
        if STACK-0x2000<=a<STACK:
            touched=set(range(a,a+w))
            if kind==uc.UC_MEM_WRITE:stack.update(touched)
            else:require(touched<=stack,'Uninitialized stack read')
            return
        if a==PUB+0x1c0:
            require(kind==uc.UC_MEM_READ and w==1,'Invalid public byte observation');memory.read(a,w);event(1,0,0,PUB,0x1c0);return
        region=1 if POOL<=a<POOL+0x100 else 5 if CHANNEL<=a<CHANNEL+0x100 else 2 if NODE<=a<NODE+0x10000 else 3 if DATA<=a<DATA+0x10000 else 4 if DESCRIPTORS<=a<DESCRIPTORS+0x10000 else 0
        if kind==uc.UC_MEM_READ:
            if region in(1,2,4,5):
                require(w==4 and a in queues and queues[a],'Missing fresh pool/channel read');sample=queues[a].pop(0);engine.mem_write(a,sample.to_bytes(4,'little'))
            else:memory.read(a,w)
            if region:
                identity=POOL if region==1 else CHANNEL if region==5 else (a&~0xff) if region in(2,3) else DESCRIPTORS
                event(3,1,region,identity,a-identity)
        else:
            v&=(1<<(8*w))-1
            if region:
                identity=POOL if region==1 else CHANNEL if region==5 else (a&~0xff) if region in(2,3) else DESCRIPTORS
                event(3,2 if w==4 else 3 if w==2 else 4,region,identity,a-identity,v)
            memory.write(a,w,v)
    cpu.hook_add(uc.UC_HOOK_CODE,instruction);cpu.hook_add(uc.UC_HOOK_MEM_READ|uc.UC_HOOK_MEM_WRITE,access)
    cpu.emu_start(functions[WRAPPER]['address'],STOP,count=10000)
    require(cpu.reg_read(UC_ARM_REG_PC)==STOP and not any(queues.values()),'Execution/fresh read queue incomplete')
    return events,snapshots,memory.snapshot(),cpu.reg_read(UC_ARM_REG_R0),len(instructions),visited,branches,tuple(sorted((a,v) for a,v in memory.bytes.items() if a>=POOL))

BRIDGE=r'''
#include "bmac_rx_fill.h"
typedef int (*normalized_io)(void *,unsigned,unsigned,unsigned,uint32_t,uint32_t,uint32_t,uint32_t *);
struct bridge {struct axd_bmac_rx_fill j;normalized_io io;void *opaque;};
static int io(void *opaque,const struct axd_bmac_rx_fill_operation *op,uint32_t *v) {
 struct bridge *b=opaque;
 if(op->kind==AXD_BRF_PUBLIC_READ8)return b->io(b->opaque,1,0,0,op->identity,op->offset,0,v);
 if(op->kind==AXD_BRF_STATUS)return b->io(b->opaque,3,op->status.kind,op->status.resource,op->status.identity,op->status.offset,op->status.value,v);
 return -1;
}
void snapshot_fill(const void *opaque,unsigned status,uint32_t *out) {
 const struct bridge *b=opaque;
 if(status){out[0]=b->j.context.status.rx.ring.out;out[1]=b->j.context.status.no_buffer_count;out[2]=b->j.context.status.rx.head;out[3]=b->j.context.status.rx.tail;}
 else{out[0]=b->j.context.normal.rx.ring.out;out[1]=b->j.context.normal.no_buffer_count;out[2]=b->j.context.normal.rx.head;out[3]=b->j.context.normal.rx.tail;}
}
int compare_fill(const uint32_t *cfg,normalized_io transport,void *opaque,uint64_t *out) {
 struct bridge b={0};struct axd_bmac_rx_fill_context c={0};unsigned steps=0;uint32_t final[4];unsigned status=cfg[0]==3 &&(cfg[1]&1);
 c.hardware=(struct axd_wake_u32){0x20000000,1};c.wlc=(struct axd_wake_u32){0x20001000,1};c.pub=(struct axd_wake_u32){0x20002000,1};c.public_owned=1;
 c.selected_fifo=cfg[0];c.selected_di=c.normal_binding=c.status_binding=(struct axd_wake_u32){0x20003000,1};c.normal_known=c.status_known=1;
 c.normal.disabled=(uint8_t)cfg[2];c.normal.rx.ring.out=cfg[3]?7:0;c.normal.no_buffer_count=cfg[6];
 c.status.disabled=(uint8_t)cfg[2];c.status.rx.core=130;c.status.rx.minor=1;
 c.status.rx.ring=(struct axd_dma64_ring){cfg[3],cfg[5],0x100,cfg[3]?6:0,cfg[3]?7:0};c.status.rx.status_identity=0x21000000;c.status.rx.status_owned=1;
 c.status.flags=cfg[7];c.status.data_offset_low=cfg[8];c.status.data_offset_high=cfg[9];c.status.buffer_size=(uint16_t)cfg[10];
 c.status.rx.head=c.status.rx.tail=cfg[11];c.status.rx.descriptor_identity=0x24000000;c.status.rx.descriptor_bytes=128;
 c.status.rx.node_owned=c.status.rx.descriptor_owned=c.status.data_owned=c.status.os_owned=1;
 c.status.receive_posts=cfg[4];c.status.pool=(struct axd_wake_u32){0x20004000,1};c.status.pool_owned=1;c.status.no_buffer_count=cfg[6];
 b.io=transport;b.opaque=opaque;if(axd_bmac_rx_fill_begin(&b.j,cfg[0],&c))return -1;out[5]=(uint64_t)(uintptr_t)&b;
 while(b.j.phase==AXD_BRF_RUNNING && steps++<1000)axd_bmac_rx_fill_step(&b.j,io,&b);
 if(b.j.phase!=AXD_BRF_TRACE_COMPLETE || !b.j.child_return_valid)return -1;
 snapshot_fill(&b,status,final);for(unsigned i=0;i<4;i++)out[i]=final[i];out[4]=b.j.child_return_value;out[5]=0;out[6]=b.j.attempted_ops;out[7]=b.j.completed_ops;return 0;
}
'''
CALLBACK=ct.CFUNCTYPE(ct.c_int,ct.c_void_p,ct.c_uint,ct.c_uint,ct.c_uint,ct.c_uint32,ct.c_uint32,ct.c_uint32,ct.POINTER(ct.c_uint32))
def model(lib,c):
    memory=copy.deepcopy(c['memory']);queues=copy.deepcopy(c['queues']);out=(ct.c_uint64*8)();events=[];snapshots=[];errors=[]
    @CALLBACK
    def io(_,path,kind,resource,identity,off,value,reply):
        events.append((path,kind,resource,identity,off,value));snap=(ct.c_uint32*4)();lib.snapshot_fill(ct.c_void_p(out[5]),c['selected'],snap);snapshots.append((*tuple(snap),memory.read(POOL+8,4)))
        try:
            a=identity+off
            if path==1:reply[0]=memory.read(a,1)
            elif kind==1:
                require(a in queues and queues[a],'Missing C fresh read');reply[0]=queues[a].pop(0)
            elif kind in(2,3,4):memory.write(a,{2:4,3:2,4:1}[kind],value)
            elif kind==5:reply[0]=(identity+0x81000000)&0xffffffff
            else:require(kind in(6,7),'Unexpected C operation')
        except ValueError as e:errors.append(str(e));return -1
        return 0
    cfg=(ct.c_uint32*12)(c['fifo'],c['byte'],c['disabled'],c['count'],c['posts'],c['mask'],memory.read(DI+0xc,4),c.get('flags',0),c.get('low',0),c.get('high',0),c.get('buffer_size',0),memory.read(DI+0x124,4))
    require(lib.compare_fill(cfg,io,None,out)==0 and not errors and not any(queues.values()),f'C wrapper/fill failed {errors}')
    require(out[6]==out[7]==len(events),'C operation accounting mismatch')
    return events,snapshots,(*tuple(out[:4]),memory.read(POOL+8,4)),out[4],tuple(sorted((a,v) for a,v in memory.bytes.items() if a>=POOL))

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('module',type=Path);parser.add_argument('--hnd',type=Path,help='Optional pinned HND module closes actual positive-pool translation');args=parser.parse_args();root=Path(__file__).resolve().parents[1]
    if args.hnd:
        IDENTITIES[DD]=(200,'9748022cfb4db093dc534589086fd41751cba07d2938655408748a753ba402cd');SITES[DD]=set();BINDINGS[DD]={}
        BINDINGS[STATUS].update({0xcc:(28,DD),0x128:(28,EXT)})
    functions=load(args.module.read_bytes(),root)
    if args.hnd:
        hnd=args.hnd.read_bytes();require(hashlib.sha256(hnd).hexdigest()==HND_SHA256,'Unknown HND module')
        spec=importlib.util.spec_from_file_location('hnd_status_elf',root/'tests/check_phy_arm.py');helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
        code,sites=helper.functions(hnd,{EXT})[EXT]
        require(len(code)==8 and hashlib.sha256(code).hexdigest()=='156b789b61d4f1f7c768fa456f1e924676012680eb3d90fe92819d704940772e' and not sites,'Unknown HND converter bytes/relocations')
        functions[EXT]={'code':code,'address':CODE+0x60000,'relocations':{}}
    cases=[case(fifo=f,byte=b,disabled=0xff) for f,b in itertools.product([0,2,3,71],range(256))]
    cases +=[case(posts=p,mask=m,status=s,producer=q,wrap=w) for p,m,s,q,w in itertools.product([0,1,2,4],[0x1fff,0xffffffff],[0,0x170,0xa0000170],[0,0x180,0xf0000170],[False,True])]
    cases +=[case(count=0,posts=p) for p in[0,8,0xffffffff]]
    if args.hnd:cases +=[positive_case(n,p,f,h,l,u,w) for (n,p),f,h,l,u,w in itertools.product([(1,2),(1,3),(1,4),(2,3),(2,4),(7,8)],[0,1,3],[False,True],[0,0x1000,0xffffffff],[0,0x55],[False,True])]
    instructions=0;idle_calls=0;positive_cases=0;visited=set()
    with tempfile.TemporaryDirectory(prefix='axd-statusfill-arm-') as tmp:
        bridge=Path(tmp)/'bridge.c';library=Path(tmp)/'bridge.so';bridge.write_text(BRIDGE)
        subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11','-Wall','-Wextra','-Werror','-pedantic','-shared','-fPIC','-I'+str(root/'driver'),str(bridge)]+[str(root/'driver'/n) for n in ['bmac_rx_fill.c','dma_rx_fill.c','dma_rx_status_fill.c','dma_descriptor.c']]+['-o',str(library)],check=True)
        lib=ct.CDLL(str(library));lib.compare_fill.argtypes=[ct.POINTER(ct.c_uint32),CALLBACK,ct.c_void_p,ct.POINTER(ct.c_uint64)];lib.compare_fill.restype=ct.c_int;lib.snapshot_fill.argtypes=[ct.c_void_p,ct.c_uint,ct.POINTER(ct.c_uint32)]
        for number,c in enumerate(cases):
            arm=emulate(functions,c);portable=model(lib,c);require((*arm[:4],arm[7])==portable,f'ARM/C status-fill mismatch case{number}')
            positive_cases+=bool(c.get('positive'));instructions+=arm[4];visited.update(arm[5]);idle_calls+=sum(name==STATUS and off==0x1c0 for name,off,_ in arm[6])
        rejected=0
        for denial in['unbound-tail','unknown-public','missing-read','unused-read','reached-translation']:
            try:
                blocked=functions
                if denial=='reached-translation' and args.hnd:
                    blocked=copy.deepcopy(functions);blocked[STATUS]['relocations'][0x128]=(28,EXT)
                    saved=BINDINGS[STATUS].pop(0x128)
                emulate(blocked,case(),denial)
            except ValueError as e:
                if denial=='reached-translation':require('unknown/external' in str(e),'Translation probe failed too early')
                rejected+=1
            else:raise ValueError('Unsafe probe accepted')
            finally:
                if denial=='reached-translation' and args.hnd:BINDINGS[STATUS][0x128]=saved
    require(visited==set(functions),'Not all selected actual bodies executed')
    print(json.dumps({'module_sha256':MODULE_SHA256,'hnd_sha256':HND_SHA256 if args.hnd else None,'cases':len(cases),'actual_arm_instructions':instructions,'actual_idle_calls':idle_calls,
        'functions_executed':len(visited),'rejection_probes':rejected,'positive_pool_arm_execution':bool(args.hnd),'positive_pool_cases':positive_cases,'external_stubs':0,'hardware_operations':0},sort_keys=True))
if __name__=='__main__':main()
