#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Pin rev51 power-on descriptor provenance; print metadata, never payload.

Optional extraction writes normalized descriptors and nine private payloads
only to a NEW external directory. No target code or hardware is executed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from radio_firmware_inventory import MODULE_SHA256, require


def inspect(data):
    require(hashlib.sha256(data).hexdigest()==MODULE_SHA256,"Unrecognized module hash")
    require(len(data)>=52 and data[:6]==b"\x7fELF\x01\x01","Expected ELF32 LE")
    h=struct.unpack_from("<HHIIIIIHHHHHH",data,16)
    require(h[0]==1 and h[1]==40 and h[10]==40 and 0<h[11]<65535 and h[12]<h[11],"Expected ARM ET_REL")
    def region(offset,size):
        require(0<=offset<=len(data) and 0<=size<=len(data)-offset,"ELF bounds")
        return data[offset:offset+size]
    region(h[5],h[11]*40)
    sections=[struct.unpack_from("<10I",data,h[5]+40*i) for i in range(h[11])]
    def contents(s):
        require(s[1]!=8,"Expected file-backed section")
        return region(s[4],s[5])
    def string(table,offset):
        require(offset<len(table),"Invalid string offset");end=table.find(b"\0",offset)
        require(end>=0,"Unterminated ELF string");return table[offset:end].decode("ascii")
    symbols={};tables={}
    wanted={"wlc_phy_set_regtbl_on_pwron_acphy","wlapi_si_coreunit","wlc_bmac_coreunit","acphytbl_info_rev51",
        "acphytbl_info_sz_rev51","acphyzerotbl_info_rev51","acphyzerotbl_info_cnt_rev51"}
    for number,s in enumerate(sections):
        if s[1]!=2:continue
        require(s[9]==16 and s[5]%16==0 and s[6]<len(sections),"Bad symbol table")
        strings=contents(sections[s[6]]);items=[]
        for n,value,size,info,_,owner in struct.iter_unpack("<IIIBBH",contents(s)):
            name=string(strings,n);item=(name,value,size,info,owner);items.append(item)
            if name in wanted:
                require(name not in symbols and 0<owner<len(sections),"Ambiguous selected symbol")
                require(value<=sections[owner][5] and size<=sections[owner][5]-value,"Symbol bounds")
                symbols[name]=item
        tables[number]=items
    require(set(symbols)==wanted,"Missing selected symbols")
    relocations={};owners={s[4] for s in symbols.values()}
    for s in sections:
        if s[1] not in(4,9) or s[7] not in owners:continue
        require(s[1]==9 and s[9]==8 and s[5]%8==0 and s[6] in tables and s[7]<len(sections),"Unexpected relocations")
        for at,info in struct.iter_unpack("<II",contents(s)):
            require(info>>8<len(tables[s[6]]) and (s[7],at) not in relocations,"Ambiguous relocation")
            relocations[s[7],at]=(info&255,tables[s[6]][info>>8])
    f=symbols["wlc_phy_set_regtbl_on_pwron_acphy"]
    require(f[2]==0x10ec and f[3]&15==2,"Function identity changed")
    wrapper=symbols["wlapi_si_coreunit"]
    require(wrapper[2]==12 and wrapper[3]&15==2,"Coreunit wrapper changed")
    body=contents(sections[wrapper[4]])[wrapper[1]:wrapper[1]+12]
    require(struct.unpack_from("<2I",body)==(0xe5903000,0xe5930000),"Coreunit wrapper loads changed")
    jump=relocations.get((wrapper[4],wrapper[1]+8))
    require(jump is not None and jump[0]==29 and jump[1]==symbols["wlc_bmac_coreunit"],"Coreunit wrapper tail binding changed")
    require(symbols["wlc_bmac_coreunit"][2]==24 and symbols["wlc_bmac_coreunit"][3]&15==2,"BMAC coreunit leaf changed")
    bindings=((0xee4,0xee8,"acphytbl_info_sz_rev51"),(0xef4,0xefc,"acphytbl_info_rev51"),
        (0xf20,0xf28,"acphyzerotbl_info_cnt_rev51"),(0xf24,0xf2c,"acphyzerotbl_info_rev51"))
    for low,high,name in bindings:
        for relative,kind,opcode in((low,43,0x03000000),(high,44,0x03400000)):
            require(relocations.get((f[4],f[1]+relative))==(kind,symbols[name]),"Function descriptor binding changed")
            word=struct.unpack_from("<I",contents(sections[f[4]]),f[1]+relative)[0]
            require(word&0x0ff00000==opcode and (((word>>4)&0xf000)|(word&0xfff))==0,"Descriptor instruction/addend changed")
    private={"tables":[],"zeros":[]};blobs={};public=[]
    for group,name,count_name,count in(("tables","acphytbl_info_rev51","acphytbl_info_sz_rev51",9),
        ("zeros","acphyzerotbl_info_rev51","acphyzerotbl_info_cnt_rev51",8)):
        symbol=symbols[name];counter=symbols[count_name]
        require(counter[2]==4 and struct.unpack_from("<I",contents(sections[counter[4]]),counter[1])[0]==count,"Descriptor count changed")
        require(symbol[2]==count*20,"Descriptor span changed")
        span=contents(sections[symbol[4]])[symbol[1]:symbol[1]+symbol[2]]
        records=[]
        for index in range(count):
            at=symbol[1]+index*20;pointer,length,table_id,offset,width=struct.unpack_from("<5I",span,index*20)
            require(width in(8,16,32,42,48,60,64),"Unsupported source table width")
            stride=1 if width==8 else 2 if width==16 else 4 if width==32 else 6 if width<=48 else 8
            require(length<=65536,"Table count exceeds bound")
            descriptor={"table_id":table_id,"count":length,"offset":offset,"width":width}
            # Only the pointer word may carry a descriptor relocation.
            require(all((symbol[4],at+delta) not in relocations for delta in(4,8,12,16)),"Unexpected descriptor relocation")
            binding=relocations.get((symbol[4],at))
            if group=="tables":
                require(binding is not None and binding[0]==2,"Missing payload binding")
                target=binding[1];size=length*stride
                require(0<target[4]<len(sections) and target[1]+pointer<=sections[target[4]][5] and size<=sections[target[4]][5]-target[1]-pointer,"Payload bounds")
                begin=target[1]+pointer
                require(all(not begin<=where<begin+size for owner,where in relocations if owner==target[4]),"Payload needs relocation")
                block=contents(sections[target[4]])[begin:begin+size];filename=f"table-{index}.bin";blobs[filename]=block
                descriptor["file"]=filename
                records.append({"index":index,"bytes":size,"sha256":hashlib.sha256(block).hexdigest(),
                    "source_symbol":target[0],"section_offset":begin,"file_offset":sections[target[4]][4]+begin})
            else:require(binding is None and pointer==0,"Unexpected zero-descriptor payload")
            private[group].append(descriptor)
        public.append({"symbol":name,"count":count,"bytes":len(span),"sha256":hashlib.sha256(span).hexdigest(),
            "section_offset":symbol[1],"file_offset":sections[symbol[4]][4]+symbol[1],"payloads":records})
    return {"module_sha256":MODULE_SHA256,"function":"wlc_phy_set_regtbl_on_pwron_acphy",
        "ignored_coreunit_wrapper":"Validated two object-pointer loads and exact local wlc_bmac_coreunit tail relocation",
        "descriptor_groups":public,"scope":"Metadata only; normalized descriptors and payload are private extraction"},private,blobs


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module",type=Path);parser.add_argument("--private-output",type=Path);args=parser.parse_args()
    require(args.module.stat().st_size<=64*1024*1024,"Input exceeds bound")
    public,descriptors,blobs=inspect(args.module.read_bytes())
    if args.private_output is not None:
        output=args.private_output.resolve();repo=Path(__file__).resolve().parents[1]
        require(not output.is_relative_to(repo),"Private extraction must be outside checkout")
        output.mkdir(mode=0o700,exist_ok=False)
        for name,block in blobs.items():
            target=output/name
            with target.open("xb")as stream:target.chmod(0o600);stream.write(block)
        target=output/"descriptors.json"
        with target.open("x")as stream:target.chmod(0o600);json.dump(descriptors,stream,indent=2);stream.write("\n")
    print(json.dumps(public,indent=2))


if __name__=="__main__":main()
