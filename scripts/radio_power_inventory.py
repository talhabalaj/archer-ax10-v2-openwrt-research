#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Inspect two private radio power constant spans; emit metadata only by default.

Pins the current module and exact ELF relocation provenance. Optional extraction
is only to a NEW directory outside this repository. No target code is executed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from radio_firmware_inventory import MODULE_SHA256, require

SPANS = (("logen-mode", 1616940, 18, "f63725df206797c5ddabaa57c26813dc1ec3c749db0eae90bb6f5ee57bfa4c10"),
         ("rccal-setup", 1616960, 8, "2b05d447dc95fce19a1e4529c56a42aaa609c46bb00787b0d79097894c2f0d83"))


def inspect(data):
    require(hashlib.sha256(data).hexdigest() == MODULE_SHA256, "Unrecognized module hash")
    require(len(data) >= 52 and data[:6] == b"\x7fELF\x01\x01", "Expected ELF32 LE")
    h = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
    require(h[0] == 1 and h[1] == 40 and h[10] == 40 and 0 < h[11] < 65535 and h[12] < h[11], "Expected ARM ET_REL")

    def region(offset, size):
        require(0 <= offset <= len(data) and 0 <= size <= len(data)-offset, "Invalid ELF bounds")
        return data[offset:offset+size]

    region(h[5], h[11]*40)
    sections = [struct.unpack_from("<10I", data, h[5]+i*40) for i in range(h[11])]

    def contents(s):
        require(s[1] != 8, "Non-file-backed selected section")
        return region(s[4], s[5])

    def string(table, offset):
        require(offset < len(table), "Invalid ELF string offset")
        end=table.find(b"\0", offset)
        require(end >= 0, "Unterminated ELF string")
        return table[offset:end].decode("ascii")

    names=contents(sections[h[12]])
    ro=[i for i,s in enumerate(sections) if string(names,s[0]) == ".rodata"]
    require(len(ro)==1, "Ambiguous rodata section")
    ro=ro[0];tables={};selected={}
    wanted={"wlc_phy_radio20704_sel_logen_mode", "wlc_phy_switch_radio_acphy.constprop.21"}
    for number,s in enumerate(sections):
        if s[1]!=2:continue
        require(s[9]==16 and s[5]%16==0 and s[6]<len(sections), "Invalid ELF symbol table")
        strings=contents(sections[s[6]]);symbols=contents(s);items=[]
        for off in range(0,len(symbols),16):
            n,value,size,info,_,owner=struct.unpack_from("<IIIBBH",symbols,off)
            name=string(strings,n);item=(name,value,size,info,owner);items.append(item)
            if name in wanted:
                require(name not in selected and info&15==2 and 0<owner<len(sections), "Invalid selected function")
                require(value<=sections[owner][5] and size<=sections[owner][5]-value,"Function bounds")
                selected[name]=item
        tables[number]=items
    require(set(selected)==wanted, "Missing power functions")

    def relocation(function, relative, kind):
        _,start,size,_,owner=selected[function];found=[]
        require(relative+4<=size, "Function relocation outside body")
        for s in sections:
            if s[1] not in (4,9) or s[7]!=owner:continue
            stride=8 if s[1]==9 else 12
            require(s[9]==stride and s[5]%stride==0 and s[6] in tables,"Invalid ELF relocation table")
            raw=contents(s)
            for pos in range(0,len(raw),stride):
                at,info=struct.unpack_from("<II",raw,pos)
                if at==start+relative:
                    require(s[1]==9 and info&255==kind and info>>8<len(tables[s[6]]),"Unexpected power relocation")
                    found.append(tables[s[6]][info>>8])
        require(len(found)==1, "Missing or duplicate power relocation")
        return found[0],struct.unpack_from("<I",contents(sections[owner]),start+relative)[0]

    mode="wlc_phy_radio20704_sel_logen_mode"
    low,a=relocation(mode,0,43);high,b=relocation(mode,4,44)
    require(low==high and low[0]==".LANCHOR0" and low[4]==ro and low[1]==1616840,"Mode anchor provenance mismatch")
    immediate=lambda word:((word>>4)&0xf000)|(word&0xfff)
    require(immediate(a)==immediate(b)==0, "Unexpected mode relocation addend")
    literal,c=relocation("wlc_phy_switch_radio_acphy.constprop.21",0x162c,2)
    require(literal[4]==ro and literal[1]==0 and c==1616960,"RCCAL literal provenance mismatch")
    blocks={};records=[]
    raw=contents(sections[ro])
    for name,offset,size,digest in SPANS:
        require(offset<=len(raw) and size<=len(raw)-offset,"Power span outside rodata")
        block=raw[offset:offset+size]
        require(hashlib.sha256(block).hexdigest()==digest,"Power span digest mismatch")
        # These selected anonymous constant spans need no further relocation.
        for s in sections:
            if s[1] not in (4,9) or s[7]!=ro:continue
            stride=8 if s[1]==9 else 12
            require(s[9]==stride and s[5]%stride==0,"Invalid selected rodata relocations")
            rel=contents(s)
            require(all(not offset<=struct.unpack_from("<I",rel,p)[0]<offset+size for p in range(0,len(rel),stride)),"Power span needs relocation")
        blocks[name]=block
        records.append({"name":name,"section":".rodata","section_offset":offset,
                        "file_offset":sections[ro][4]+offset,"size":size,"sha256":digest})
    return {"module_sha256":MODULE_SHA256,"spans":records,
            "provenance":"Exact mode MOVW/MOVT anchor pair and switch ABS32 literal relocation"},blocks


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module",type=Path);parser.add_argument("--private-output",type=Path);args=parser.parse_args()
    require(args.module.stat().st_size<=64*1024*1024,"Input exceeds bound")
    metadata,blocks=inspect(args.module.read_bytes())
    if args.private_output is not None:
        destination=args.private_output.resolve();repo=Path(__file__).resolve().parents[1]
        require(destination!=repo and repo not in destination.parents,"Private extraction must be outside checkout")
        require(not destination.exists(),"Private output must be a new directory")
        destination.mkdir(mode=0o700)
        for name,block in blocks.items():
            output=destination/(name+".bin");output.write_bytes(block);output.chmod(0o600)
        (destination/"metadata.json").write_text(json.dumps(metadata,indent=2)+"\n")
    print(json.dumps(metadata,indent=2))


if __name__=="__main__":main()
