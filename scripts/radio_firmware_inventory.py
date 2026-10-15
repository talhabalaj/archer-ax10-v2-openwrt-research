#!/usr/bin/env python3
"""Inventory selected firmware objects in the pinned stock ARM32 wl.ko.

Default output is metadata only. --private-output writes proprietary input
objects to a NEW private directory: keep it outside the public repository.
No target code is executed. This is not a firmware installer or driver loader.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

MODULE_SHA256 = "8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c"
TABLES = ("d11ax51initvals130", "d11ax51initvalsx130",
          "d11ax51bsinitvals130", "d11ax51bsinitvalsx130")
MICROCODE = tuple("d11" + side + "_" + profile + "130" + minor
                 for side in ("ucode", "ucodex")
                 for profile in ("mu", "btcxmu") for minor in ("", "_1"))


SMC = ("d11smc1",)
SMC_SHA256 = "0e791de621714ef23006b41687ae8338c24a034097f9f18a174f0441225784a4"


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def objects(data, wanted):
    """Resolve STT_OBJECT through ELF section indices, never virtual addresses."""
    require(data[:6] == b"\x7fELF\x01\x01" and len(data) >= 52, "Expected ELF32 LE")
    h = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
    require(h[0] == 1 and h[1] == 40 and h[10] == 40, "Expected ARM ELF32 ET_REL")
    require(0 < h[11] < 65535 and h[12] < h[11], "Unsupported ELF section table")

    def region(offset, length):
        require(0 <= offset <= len(data) and 0 <= length <= len(data) - offset,
                "ELF region out of bounds")
        return data[offset:offset + length]

    region(h[5], 40 * h[11])
    sections = [struct.unpack_from("<10I", data, h[5] + 40 * i) for i in range(h[11])]

    def contents(section):
        require(section[1] != 8, "Object is not file-backed")
        return region(section[4], section[5])

    def cstring(table, offset):
        require(offset < len(table), "Bad ELF string offset")
        end = table.find(b"\0", offset)
        require(end >= 0, "Unterminated ELF string")
        return table[offset:end].decode("ascii")

    names = contents(sections[h[12]])
    found = {}
    for section in sections:
        if section[1] != 2:
            continue
        require(section[9] == 16 and section[5] % 16 == 0
                and section[6] < len(sections), "Invalid symbol table")
        strings = contents(sections[section[6]])
        symbols = contents(section)
        for offset in range(0, section[5], 16):
            n, value, size, info, other, index = struct.unpack_from("<IIIBBH", symbols, offset)
            if not n:
                continue
            name = cstring(strings, n)
            if name not in wanted:
                continue
            require(name not in found, "Ambiguous selected symbol")
            require(info & 15 == 1 and 0 < index < len(sections), "Invalid selected object")
            owner = sections[index]
            require(value <= owner[5] and 0 < size <= owner[5] - value, "Object exceeds section")
            block = contents(owner)[value:value + size]
            # Linked relocations would require loader context, not raw slicing.
            for rel in sections:
                if rel[1] in (4, 9) and rel[7] == index:
                    stride = 12 if rel[1] == 4 else 8
                    require(rel[9] == stride and rel[5] % stride == 0, "Invalid relocation table")
                    entries = contents(rel)
                    for pos in range(0, len(entries), stride):
                        target = struct.unpack_from("<I", entries, pos)[0]
                        require(not value <= target < value + size, "Selected object needs relocation")
            found[name] = {"data": block, "section": cstring(names, owner[0]),
                           "section_offset": value, "file_offset": owner[4] + value}
    require(set(found) == set(wanted), "Missing selected objects")
    return found


def inspect(data):
    require(hashlib.sha256(data).hexdigest() == MODULE_SHA256, "Unrecognized module hash")
    selected = TABLES + MICROCODE + SMC + tuple(name + "sz" for name in MICROCODE + SMC)
    blobs = objects(data, selected)
    records = []
    for name in TABLES + MICROCODE + SMC:
        item = blobs[name]
        block = item["data"]
        record = {"symbol": name, "section": item["section"],
                  "file_offset": item["file_offset"], "section_offset": item["section_offset"],
                  "size": len(block), "sha256": hashlib.sha256(block).hexdigest()}
        if name in TABLES:
            require(len(block) % 8 == 0, "Partial init record")
            rows = list(struct.iter_unpack("<HHI", block))
            require(rows and rows[-1] == (0xffff, 0, 0), "Invalid terminator")
            require(all(width in (2, 4) and offset < 0x1000 and offset % width == 0
                        and offset + width <= 0x1000 and (width == 4 or value <= 0xffff)
                        for offset, width, value in rows[:-1]), "Unsupported init record")
            record.update(kind="init-table", records=len(rows),
                          writes16=sum(r[1] == 2 for r in rows[:-1]),
                          writes32=sum(r[1] == 4 for r in rows[:-1]))
        elif name in SMC:
            companion = blobs[name + "sz"]
            size_word = companion["data"]
            require(len(size_word) == 4 and struct.unpack("<I", size_word)[0] == len(block),
                    "SMC size-symbol mismatch")
            require(len(block) == 11420 and record["sha256"] == SMC_SHA256,
                    "Unrecognized target SMC image")
            rows = (len(block) // 6) & 0xffff
            record.update(kind="smc-image", table_id=47, width=48, row_count=rows,
                          transferred_bytes=rows * 6, ignored_trailing_bytes=len(block) % 6,
                          size_symbol_matches=True,
                          size_symbol={"symbol": name + "sz", "section": companion["section"],
                              "file_offset": companion["file_offset"],
                              "section_offset": companion["section_offset"],
                              "size": len(size_word), "value": struct.unpack("<I", size_word)[0]})
        else:
            size_word = blobs[name + "sz"]["data"]
            require(len(size_word) == 4 and struct.unpack("<I", size_word)[0] == len(block),
                    "Microcode size-symbol mismatch")
            require(len(block) % 4 == 0, "Partial microcode word")
            record.update(kind="microcode", words32=len(block) // 4, size_symbol_matches=True)
        records.append(record)
    return {"module_sha256": MODULE_SHA256, "objects": records,
            "scope": "Selected file-backed objects; no raw data, execution or RF operation",
            "limitations": ["Array names are not proof of runtime selection",
                "Init records remain unadjusted; stock consumer has runtime-dependent fixups",
                "Inventory does not execute uploads, PHY/radio programming or device validation"]}, blobs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("--private-output", type=Path)
    args = parser.parse_args()
    require(args.module.stat().st_size <= 64 * 1024 * 1024, "Input exceeds size bound")
    report, blobs = inspect(args.module.read_bytes())
    if args.private_output:
        destination = args.private_output.resolve()
        public = Path(__file__).resolve().parents[1]
        require(not destination.is_relative_to(public), "Private output must be outside this repository")
        destination.mkdir(mode=0o700, parents=True, exist_ok=False)
        for name in TABLES + MICROCODE + SMC:
            with (destination / (name + ".bin")).open("xb") as output:
                (destination / (name + ".bin")).chmod(0o600)
                output.write(blobs[name]["data"])
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
