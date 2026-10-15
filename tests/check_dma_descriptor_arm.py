#!/usr/bin/env python3
"""Compare the current closed ARM descriptor updater with portable C.

Requires Unicorn2.1.4, a host C compiler and the caller-supplied pinned module.
No target code executes outside the emulator; no raw module bytes are output.
"""
import argparse, ctypes, hashlib, itertools, json, os, pathlib, shlex, struct, subprocess, tempfile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE,__version__
assert __version__=='2.1.4', 'Unicorn2.1.4 is required'
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_SP,UC_ARM_REG_LR
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('module',type=pathlib.Path)
args=parser.parse_args()
MODULE=args.module
scratch=tempfile.TemporaryDirectory(prefix='axd-dma-descriptor-arm-')
root=pathlib.Path(__file__).resolve().parents[1]
library=pathlib.Path(scratch.name)/'formatter.so'
subprocess.run(shlex.split(os.environ.get('CC','cc'))+[
    '-std=c11','-Wall','-Wextra','-Werror','-pedantic','-shared','-fPIC',
    str(root/'driver/dma_descriptor.c'),'-o',str(library)],check=True)
b=MODULE.read_bytes();assert hashlib.sha256(b).hexdigest()=='8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c'
eh=struct.unpack_from('<16sHHIIIIIHHHHHH',b);secs=[struct.unpack_from('<IIIIIIIIII',b,eh[6]+i*eh[11]) for i in range(eh[12])]
found=[]
for s in secs:
 if s[1]!=2:continue
 st=secs[s[6]];names=b[st[4]:st[4]+st[5]]
 for i in range(0,s[5],s[9]):
  no,val,size,info,other,idx=struct.unpack_from('<IIIBBH',b,s[4]+i)
  name=names[no:names.find(b'\0',no)].decode(errors='replace')
  if name=='dma64_dd_upd':found.append((val,size,idx))
assert len(found)==1
val,size,idx=found[0];sec=secs[idx];code=b[sec[4]+val:sec[4]+val+size];assert len(code)==200
assert hashlib.sha256(code).hexdigest()=='9748022cfb4db093dc534589086fd41751cba07d2938655408748a753ba402cd'
# No relocated operands or calls are permitted inside this closed function.
for rs in secs:
 if rs[1] not in (9,4) or rs[7]!=idx:continue
 for off in range(0,rs[5],rs[9]):
  target=struct.unpack_from('<I',b,rs[4]+off)[0]
  assert not val<=target<val+size
class Input(ctypes.Structure):_fields_=[(n,ctypes.c_uint32) for n in ['mapped','low','high','flags','count']]+[('parity',ctypes.c_uint8)]
class Descriptor(ctypes.Structure):_fields_=[(n,ctypes.c_uint32) for n in ['ctrl1','ctrl2','low','high']]
lib=ctypes.CDLL(str(library));lib.axd_dma64_format.argtypes=[ctypes.POINTER(Descriptor),ctypes.POINTER(Input)];lib.axd_dma64_format.restype=ctypes.c_int
CODE=0x10000000;CTX=0x20000000;RING=0x21000000;FLAGS=0x22000000;STACK=0x30000000;STOP=CODE+0x1000
cases=0;writecases={4:0,5:0};all_orders=set()
for mapped,low,high,count,pen,flags in itertools.product([0,1,0x12345678,0x41234567,0x81234567,0xcfffffff],[0,1,0x40000000,0xffffffff],[0,0x87654321],[0,0x7fff,0x12345678],[0,1],[0,0xf1234567,0x80000000]):
 u=Uc(UC_ARCH_ARM,UC_MODE_ARM)
 for base in [CODE,CTX,RING,FLAGS,STACK]:u.mem_map(base,0x10000)
 u.mem_write(CODE,code);u.mem_write(RING,b'\xa5'*128)
 sp=STACK+0x8000
 def word(a,v):u.mem_write(a,struct.pack('<I',v))
 word(CTX+4,pen);word(CTX+0xc0,low);word(CTX+0xc4,high);word(FLAGS,flags)
 word(sp,FLAGS);word(sp+4,count)
 for reg,value in [(UC_ARM_REG_R0,CTX),(UC_ARM_REG_R1,RING),(UC_ARM_REG_R2,mapped),(UC_ARM_REG_R3,3),(UC_ARM_REG_SP,sp),(UC_ARM_REG_LR,STOP)]:u.reg_write(reg,value)
 writes=[];reads=[]
 def instruction(uc,address,sz,opaque):
  if address==STOP:uc.emu_stop();return
  assert CODE<=address<CODE+len(code),(hex(address),'unexpected instruction')
 def read(uc,access,address,sz,value,opaque):
  assert sz==4
  assert address in [CTX+4,CTX+0xc0,CTX+0xc4,FLAGS,sp,sp+4] or sp-20<=address<sp or RING+48<=address<RING+64,(hex(address),'unknown read')
  reads.append(address)
 def write(uc,access,address,sz,value,opaque):
  assert sz==4
  assert sp-20<=address<sp or RING+48<=address<RING+64,(hex(address),'unknown write')
  if RING<=address<RING+128:writes.append((address-(RING+48),value))
 u.hook_add(UC_HOOK_CODE,instruction);u.hook_add(UC_HOOK_MEM_READ,read);u.hook_add(UC_HOOK_MEM_WRITE,write)
 u.emu_start(CODE,STOP+4,count=256)
 expected=u.mem_read(RING+48,16)
 assert u.mem_read(RING,48)==b'\xa5'*48 and u.mem_read(RING+64,64)==b'\xa5'*64
 inp=Input(mapped,low,high,flags,count,pen);out=Descriptor();assert lib.axd_dma64_format(ctypes.byref(out),ctypes.byref(inp))==0
 assert bytes(out)==bytes(expected),(mapped,low,high,count,pen,flags,bytes(out).hex(),expected.hex())
 assert [x[0] for x in writes] in [[8,12,0,4],[8,12,0,4,4]]
 assert set([CTX+4,CTX+0xc0,CTX+0xc4,FLAGS,sp,sp+4]).issubset(reads)
 cases+=1;writecases[len(writes)]+=1;all_orders.add(tuple(x[0] for x in writes))
result={'module_sha256':hashlib.sha256(b).hexdigest(),'function':'dma64_dd_upd','function_bytes':len(code),'function_sha256':hashlib.sha256(code).hexdigest(),'cases':cases,'word_write_orders':[list(x) for x in sorted(all_orders)],'write_count_cases':writecases,'calls':0,'relocations':0,'strict_memory_ranges':True,'hardware_operations':0}
print(json.dumps(result,sort_keys=True));scratch.cleanup()
