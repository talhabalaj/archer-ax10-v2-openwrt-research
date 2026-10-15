#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Inspect only the pinned PHY51 initialization's three private table spans.

Default output is metadata. Optional --private-output writes the three spans
to a NEW directory outside this checkout. No target code or hardware runs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from radio_firmware_inventory import MODULE_SHA256, require


def inspect(data):
    require(hashlib.sha256(data).hexdigest() == MODULE_SHA256, "Unrecognized module hash")
    require(data[:6] == b"\x7fELF\x01\x01" and len(data) >= 52, "Expected ELF32 LE")
    h = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
    require(h[0] == 1 and h[1] == 40 and h[10] == 40, "Expected ARM ELF32 ET_REL")

    def region(offset, length):
        require(0 <= offset <= len(data) and 0 <= length <= len(data) - offset,
                "ELF region out of bounds")
        return data[offset:offset + length]

    require(0 < h[11] < 65535 and h[12] < h[11], "Unsupported ELF section table")
    region(h[5], h[11] * 40)
    sections = [struct.unpack_from("<10I", data, h[5] + i * 40) for i in range(h[11])]

    def contents(section):
        require(section[1] != 8, "Region is not file-backed")
        return region(section[4], section[5])

    def name(table, offset):
        require(offset < len(table), "Bad string offset")
        end = table.find(b"\0", offset)
        require(end >= 0, "Unterminated string")
        return table[offset:end].decode("ascii")

    section_names = contents(sections[h[12]])
    matches = []
    for table in sections:
        if table[1] != 2:
            continue
        require(table[9] == 16 and table[5] % 16 == 0 and table[6] < len(sections),
                "Invalid symbol table")
        strings = contents(sections[table[6]])
        symbols = list(struct.iter_unpack("<IIIBBH", contents(table)))
        for symbol in symbols:
            if symbol[0] and name(strings, symbol[0]) == "wlc_phy_init_acphy":
                matches.append((symbol, symbols))
    require(len(matches) == 1, "Ambiguous/missing initialization function")
    function, symbols = matches[0]
    require(function[3] & 15 == 2 and 0 < function[5] < len(sections), "Invalid function")
    code = sections[function[5]]
    require(function[2] == 1832 and function[1] + function[2] <= code[5], "Function bounds changed")

    def word(relative):
        require(0 <= relative <= function[2] - 4, "Instruction out of bounds")
        return struct.unpack_from("<I", contents(code), function[1] + relative)[0]

    relocations = {}
    for rel in sections:
        if rel[1] != 9 or rel[7] != function[5]:
            continue
        require(rel[9] == 8 and rel[5] % 8 == 0, "Invalid relocation table")
        for offset, info in struct.iter_unpack("<II", contents(rel)):
            if function[1] <= offset < function[1] + function[2]:
                require(offset not in relocations and info >> 8 < len(symbols), "Bad relocation")
                relocations[offset] = (info & 255, symbols[info >> 8])

    def relocation(relative, kind):
        record = relocations.get(function[1] + relative)
        require(record is not None and record[0] == kind, "Missing source relocation")
        require(0 < record[1][5] < len(sections), "Unbound source relocation")
        return record[1]

    anchor = relocation(4, 43)
    require(relocation(0x10, 44) == anchor, "MOVW/MOVT source mismatch")
    for relative, opcode in ((4, 0x03000000), (0x10, 0x03400000)):
        instruction = word(relative)
        require(instruction & 0x0ff0f000 == opcode | 0x1000, "Source address instruction changed")
        require(((instruction >> 4) & 0xf000) | (instruction & 0xfff) == 0,
                "Unexpected relocated source addend")

    literal = relocation(0x724, 2)
    literal_offset = literal[1] + word(0x724)
    require(literal[5] == anchor[5] and literal_offset == anchor[1] + 256,
            "Table span source relation changed")

    def blob(section_index, offset, length):
        section = sections[section_index]
        require(offset <= section[5] and length <= section[5] - offset, "Span exceeds section")
        for rel in sections:
            if rel[1] in (4, 9) and rel[7] == section_index:
                stride = 12 if rel[1] == 4 else 8
                require(rel[9] == stride and rel[5] % stride == 0, "Bad data relocations")
                for pos in range(0, rel[5], stride):
                    target = struct.unpack_from("<I", contents(rel), pos)[0]
                    require(not offset <= target < offset + length, "Span needs relocation")
        return contents(section)[offset:offset + length], {
            "section": name(section_names, section[0]), "section_offset": offset,
            "file_offset": section[4] + offset,
        }

    block32, source32 = blob(literal[5], literal_offset, 32)
    block256, source256 = blob(anchor[5], anchor[1], 256)
    mov, movt = word(0x28), word(0x2c)
    require(mov & 0x0ffff000 == 0x03a06000 and movt & 0x0ff0f000 == 0x03406000,
            "Immediate scalar construction changed")
    rotation = ((mov >> 8) & 15) * 2
    imm8 = mov & 255
    lower = ((imm8 >> rotation) | (imm8 << (32 - rotation))) & 0xffffffff if rotation else imm8
    upper = ((movt >> 4) & 0xf000) | (movt & 0xfff)
    scalar = struct.pack("<I", (lower & 0xffff) | (upper << 16))
    records, blobs = [], {}
    for offset, count, block, source in ((0x1000, 8, block32, source32),
                                        (0x1040, 64, block256, source256),
                                        (0x1020, 1, scalar, {"source": "decoded ARM immediate construction",
                                                           "function_offsets": [0x28, 0x2c]})):
        filename = f"acphy51_table16_offset{offset:04x}.bin"
        blobs[filename] = block
        records.append({"file": filename, "table_id": 16, "offset": offset,
                        "count": count, "width": 32, "size": len(block),
                        "sha256": hashlib.sha256(block).hexdigest(), "source": source})
    return {"module_sha256": MODULE_SHA256, "function": "wlc_phy_init_acphy",
            "objects": records, "scope": "Metadata only; exact source bindings for three private target table spans",
            "limitations": ["Input identity/source decoding does not prove hardware readiness or initialization",
                            "OTP/PMU/GPIO and runtime object/resource bindings are separate requirements"]}, blobs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("--private-output", type=Path)
    args = parser.parse_args()
    require(args.module.stat().st_size <= 64 * 1024 * 1024, "Input exceeds bound")
    report, blobs = inspect(args.module.read_bytes())
    if args.private_output:
        destination = args.private_output.resolve()
        public = Path(__file__).resolve().parents[1]
        require(not destination.is_relative_to(public), "Private output must be outside repository")
        destination.mkdir(mode=0o700, parents=True, exist_ok=False)
        for filename, block in blobs.items():
            path = destination / filename
            with path.open("xb") as output:
                path.chmod(0o600)
                output.write(block)
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
