#!/usr/bin/env python3
"""Inspect an official AX10 v2 Cloud image without executing or extracting it.

Emits only image-format metadata. CRC checks establish consistency, not signer
identity, installed bootloader state, or permission to flash this image.
"""
import argparse
import hashlib
import json
import lzma
from pathlib import Path
import re
import struct
import zlib


def require(condition, message):
    if not condition:
        raise ValueError(message)


def region(data, offset, size):
    require(0 <= offset <= len(data) and 0 <= size <= len(data) - offset,
            "Image region is out of bounds")
    return data[offset:offset + size]


def partition_table(data, prefix):
    pattern = rb"(?:^|\n)" + prefix + rb" ([a-z0-9_-]+) base 0x([0-9a-fA-F]+) size 0x([0-9a-fA-F]+)"
    rows = []
    for match in re.finditer(pattern, data):
        rows.append({"name": match[1].decode("ascii"),
                     "offset": int(match[2], 16), "size": int(match[3], 16)})
    require(rows and len({r["name"] for r in rows}) == len(rows),
            "Missing or duplicate partition names")
    ordered = sorted(rows, key=lambda row: row["offset"])
    require(all(a["offset"] + a["size"] <= b["offset"]
                for a, b in zip(ordered, ordered[1:])), "Overlapping partitions")
    return rows


def compressed_header(data):
    require(len(data) >= 33 and data[12:16] == b"BRCM", "Missing BRCM header")
    load, entry, length = struct.unpack_from("<3I", data)
    require(length in (len(data) - 20, len(data) - 21), "Unexpected BRCM compressed length")
    require(data[20 + length:] in (b"", b"\0"), "Unexpected BRCM trailer")
    decoder = lzma.LZMADecompressor(lzma.FORMAT_ALONE, memlimit=64 * 1024 * 1024)
    raw = decoder.decompress(data[20:20 + length], max_length=32 * 1024 * 1024)
    require(decoder.eof, "Incomplete or oversized LZMA member")
    return {"load_address": hex(load), "entry_address": hex(entry),
            "compressed_size": length, "decompressed_size": len(raw),
            "decompressed_sha256": hashlib.sha256(raw).hexdigest()}


def inspect(data):
    require(data[20:34] == b"fw-type:Cloud\n", "Expected Cloud image header")
    require(struct.unpack_from(">I", data)[0] == len(data), "Outer size mismatch")
    payload = 0x1014
    rows = partition_table(region(data, payload, 0x800).split(b"\0", 1)[0], b"fwup-ptn")
    parts = {row["name"]: region(data, payload + row["offset"], row["size"])
             for row in rows}
    table = parts["partition-table"]
    require(len(table) >= 4, "Partition table too short")
    # This is a fixed four-byte marker in tplink-safeloader, not a length.
    require(table[:4] == b"\x00\x04\x00\x00", "Unexpected partition table marker")
    flash = partition_table(table[4:].split(b"\0", 1)[0], b"partition")
    capacity = {row["name"]: row["size"] for row in flash}
    require(all(row["name"] in capacity and row["size"] <= capacity[row["name"]]
                for row in rows), "Payload exceeds declared flash partition")
    image = parts["os-image"]
    widths = [("tag_version", 4), ("company", 20), ("format_version", 14),
              ("chip_id", 6), ("board_id", 16), ("big_endian", 2),
              ("total_size", 10), ("cfe_address", 12), ("cfe_size", 10),
              ("rootfs_address", 12), ("rootfs_size", 10),
              ("kernel_address", 12), ("kernel_size", 10),
              ("sequence", 4), ("image_version", 32),
              ("dtb_address", 12), ("dtb_size", 10)]
    tag, offset = {}, 0
    for name, width in widths:
        tag[name] = region(image, offset, width).rstrip(b"\0").decode("ascii")
        offset += width
    require(tag["tag_version"] == "7" and tag["chip_id"] == "63178"
            and tag["board_id"] == "TP6750_AX1500V1", "Not the inspected AX10 v2 tag")
    numbers = {name: int(tag[name]) for name in ["total_size", "cfe_address", "cfe_size",
               "rootfs_address", "rootfs_size", "kernel_address", "kernel_size",
               "dtb_address", "dtb_size"]}
    require(numbers["cfe_size"] == 0 and numbers["total_size"] <= len(image) - 256,
            "Unexpected combined image size")
    require(image[256 + numbers["total_size"]:] in (b"", b"\0"), "Unexpected os-image trailer")
    require(numbers["rootfs_size"] + numbers["kernel_size"] + numbers["dtb_size"]
            == numbers["total_size"], "Component sizes do not sum")
    checks = {"tag_crc32": (zlib.crc32(image[:236]) ^ 0xffffffff)
              == struct.unpack_from("<I", image, 236)[0],
              "image_crc32": (zlib.crc32(image[256:256 + numbers["total_size"]]) ^ 0xffffffff)
              == struct.unpack_from("<I", image, 216)[0]}
    require(all(checks.values()), "Broadcom CRC mismatch")
    components = {}
    for name in ["rootfs", "kernel", "dtb"]:
        relative = numbers[name + "_address"] - numbers["rootfs_address"] + 256
        block = region(image, relative, numbers[name + "_size"])
        components[name] = {"os_image_offset": relative, "size": len(block),
                            "sha256": hashlib.sha256(block).hexdigest()}
        if name == "rootfs":
            require(block[:4] == b"hsqs", "Missing SquashFS")
            sb = struct.unpack_from("<5I6H8Q", block)
            require(sb[9:11] == (4, 0) and sb[12] <= len(block), "Invalid SquashFS header")
            components[name]["squashfs"] = {"major": sb[9], "minor": sb[10],
                "compression_id": sb[5], "block_size": sb[3], "inodes": sb[1],
                "bytes_used": sb[12]}
        elif name == "kernel":
            components[name]["brcm_lzma"] = compressed_header(block)
        else:
            require(block[:4] == b"\xd0\x0d\xfe\xed"
                    and struct.unpack_from(">I", block, 4)[0] == len(block), "Invalid DTB header")
    for name in ["rootfs_address", "kernel_address", "dtb_address", "cfe_address"]:
        tag[name] = hex(numbers[name])
    return {"scope": "Official image metadata; no installed flash or fuse-state claim",
            "image_size": len(data), "image_sha256": hashlib.sha256(data).hexdigest(),
            "format": "TP-Link Cloud/safeloader with Broadcom FILE_TAG v7 os-image",
            "payload_offset": payload,
            "image_partitions": [dict(row, file_offset=payload + row["offset"]) for row in rows],
            "declared_flash_partitions": flash,
            "declared_flash_end": max(row["offset"] + row["size"] for row in flash),
            "broadcom_tag": tag, "checks": checks, "components": components,
            "fs_uboot": compressed_header(parts["fs-uboot"]),
            "limitations": ["Outer Cloud header authentication is not validated",
                "Declared flash map does not establish physical chip capacity",
                "No firmware or bootloader bytes were executed",
                "No installed recovery or RAM boot was tested"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    args = parser.parse_args()
    require(args.image.stat().st_size <= 64 * 1024 * 1024, "Image exceeds inspection bound")
    print(json.dumps(inspect(args.image.read_bytes()), indent=2))


if __name__ == "__main__":
    main()
