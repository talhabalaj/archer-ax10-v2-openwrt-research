#!/usr/bin/env python3
"""Read ARM ELF32 module metadata and kernel/BSP linkage without executing it."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def inspect(path):
    data = path.read_bytes()
    if data[:6] != b"\x7fELF\x01\x01":
        raise ValueError("Expected ELF32 little-endian input")
    header = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
    kind, machine = header[:2]
    if machine != 40:
        raise ValueError("Expected EM_ARM")
    section_offset, section_stride, section_count, strings_index = header[5], header[10], header[11], header[12]
    if section_stride != 40 or not 0 < section_count < 65535 or strings_index >= section_count:
        raise ValueError("Unsupported section table")
    if section_offset + section_stride * section_count > len(data):
        raise ValueError("Truncated section table")
    sections = [struct.unpack_from("<IIIIIIIIII", data, section_offset + i * section_stride) for i in range(section_count)]

    def contents(section):
        if section[1] == 8:  # SHT_NOBITS
            return b""
        start, size = section[4:6]
        if start + size > len(data):
            raise ValueError("Truncated section")
        return data[start:start + size]

    def cstring(table, offset):
        if offset >= len(table):
            raise ValueError("Bad string-table offset")
        end = table.find(b"\0", offset)
        if end < 0:
            raise ValueError("Unterminated string")
        return table[offset:end].decode("utf-8", errors="replace")

    names = contents(sections[strings_index])
    by_name = {cstring(names, s[0]): s for s in sections}
    metadata = {}
    if ".modinfo" in by_name:
        for field in contents(by_name[".modinfo"]).split(b"\0"):
            key, sep, value = field.partition(b"=")
            # Only portable build/ABI metadata; never arbitrary parameters or strings.
            if sep and key in (b"license", b"version", b"vermagic", b"depends", b"srcversion"):
                metadata[key.decode()] = value.decode(errors="replace")
    defined, undefined = [], []
    for section in sections:
        if section[1] not in (2, 11):  # SHT_SYMTAB / SHT_DYNSYM
            continue
        if section[9] != 16 or section[6] >= len(sections):
            raise ValueError("Unsupported symbol table")
        symbols, strings = contents(section), contents(sections[section[6]])
        if len(symbols) % 16:
            raise ValueError("Truncated symbol table")
        for offset in range(0, len(symbols), 16):
            name, value, size, info, other, index = struct.unpack_from("<IIIBBH", symbols, offset)
            if not name:
                continue
            name = cstring(strings, name)
            if index == 0:
                undefined.append(name)
            elif info & 15 == 2:  # STT_FUNC
                defined.append(name)
    return {
        "artifact": path.name,
        "sha256": hashlib.sha256(data).hexdigest(),
        "bytes": len(data),
        "elf": {"class": 32, "byte_order": "little", "machine": "ARM", "type": kind},
        "module_metadata": metadata,
        "defined_function_count": len(set(defined)),
        "undefined_symbol_count": len(set(undefined)),
        "undefined_symbols": sorted(set(undefined)),
        "defined_functions": sorted(set(defined)),
        "scope": "Static symbol/ABI observations only; not runtime behavior or recovered source.",
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", type=Path, nargs="+")
    parser.add_argument("--summary", action="store_true", help="Omit complete symbol names")
    args = parser.parse_args()
    results = [inspect(path) for path in args.paths]
    if args.summary:
        for result in results:
            del result["undefined_symbols"], result["defined_functions"]
    print(json.dumps(results, indent=2))
