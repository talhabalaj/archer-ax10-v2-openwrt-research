#!/usr/bin/env python3
"""Check our compiled host library against selected private stock data in memory.

Arguments: the pinned wl.ko, and a trusted host library built from driver/*.c.
Neither proprietary arrays nor raw records are printed or written.
"""
import argparse
import ctypes as c
import importlib.util
from pathlib import Path
import struct


class Stats(c.Structure):
    _fields_ = [(name, c.c_size_t) for name in ("writes", "writes16", "writes32")]


class Selection(c.Structure):
    _fields_ = [("primary", c.c_char_p), ("extended", c.c_char_p),
                ("primary_bytes", c.c_size_t), ("extended_bytes", c.c_size_t)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("library", type=Path)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1] / "scripts/radio_firmware_inventory.py"
    spec = importlib.util.spec_from_file_location("firmware_inventory", source)
    inventory = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(inventory)
    report, blobs = inventory.inspect(args.module.read_bytes())
    lib = c.CDLL(str(args.library.resolve(strict=True)))
    callback = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_uint16, c.c_uint16, c.c_uint32)
    lib.axd_table_validate.argtypes = [c.c_void_p, c.c_size_t, c.c_uint32, c.POINTER(Stats)]
    lib.axd_table_validate.restype = c.c_int
    lib.axd_table_replay.argtypes = [c.c_void_p, c.c_size_t, c.c_uint32,
                                    callback, c.c_void_p, c.POINTER(c.c_size_t)]
    lib.axd_table_replay.restype = c.c_int
    lib.axd_select_ucode.argtypes = [c.c_uint16, c.c_uint16, c.c_uint32, c.POINTER(Selection)]
    lib.axd_select_ucode.restype = c.c_int
    total = 0
    for item in report["objects"]:
        if item["kind"] != "init-table":
            continue
        raw = blobs[item["symbol"]]["data"]
        buffer = c.create_string_buffer(raw)
        expected = list(struct.iter_unpack("<HHI", raw))[:-1]
        stats, written, observed = Stats(), c.c_size_t(), []

        @callback
        def record(context, offset, width, value):
            observed.append((offset, width, value))
            return 0

        assert lib.axd_table_validate(buffer, len(raw), 0x1000, c.byref(stats)) == 0
        assert (stats.writes16, stats.writes32) == (item["writes16"], item["writes32"])
        assert lib.axd_table_replay(buffer, len(raw), 0x1000, record, None, c.byref(written)) == 0
        assert observed == expected and written.value == len(expected) == stats.writes
        total += written.value
    for minor in (0, 1):
        for boardflags in (0, 1, 0xfffffffe, 0xffffffff):
            chosen = Selection()
            assert lib.axd_select_ucode(130, minor, boardflags, c.byref(chosen)) == 0
            family = "btcxmu" if boardflags & 1 else "mu"
            suffix = "_1" if minor == 1 else ""
            primary = "d11ucode_" + family + "130" + suffix
            extended = "d11ucodex_" + family + "130" + suffix
            assert chosen.primary.decode() == primary and chosen.extended.decode() == extended
            assert chosen.primary_bytes == len(blobs[primary]["data"])
            assert chosen.extended_bytes == len(blobs[extended]["data"])
    for core, minor in ((30, 0), (42, 0), (129, 1), (130, 2), (131, 0)):
        chosen = Selection(b"stale", b"stale", 1, 1)
        assert lib.axd_select_ucode(core, minor, 0, c.byref(chosen)) != 0
        assert chosen.primary is None and chosen.extended is None
        assert chosen.primary_bytes == chosen.extended_bytes == 0
    print(f"Four stock tables: {total} raw records validated and replayed exactly.")
    print("Eight selector cases match stock object sizes; five unsupported identities rejected.")
    print("No MMIO, firmware upload, calibration access or radio operation occurred.")


if __name__ == "__main__":
    main()
