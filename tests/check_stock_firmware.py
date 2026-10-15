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


class Upload(c.Structure):
    _fields_ = [("phase", c.c_int), ("error", c.c_int),
                ("image", c.c_void_p * 2), ("length", c.c_size_t * 2),
                ("word_index", c.c_size_t), ("words", c.c_size_t * 2),
                ("bank", c.c_uint), ("attempted_ops", c.c_uint64),
                ("completed_ops", c.c_uint64)]


Write32 = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_uint16, c.c_uint32)
Read32 = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_uint16, c.POINTER(c.c_uint32))
Barrier = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_int)


class UploadIO(c.Structure):
    _fields_ = [("context", c.c_void_p), ("write32", Write32),
                ("read32", Read32), ("barrier", Barrier)]


class InitOperation(c.Structure):
    _fields_ = [("kind", c.c_int), ("offset", c.c_uint32),
                ("value", c.c_uint32), ("selector", c.c_uint32)]


class EffectiveInit(c.Structure):
    _fields_ = [("phase", c.c_int), ("error", c.c_int), ("data", c.c_void_p),
                ("record_count", c.c_size_t), ("record_index", c.c_size_t),
                ("completed_records", c.c_size_t), ("skipped_records", c.c_size_t),
                ("addend", c.c_uint32), ("operation", InitOperation),
                ("attempted_ops", c.c_uint64), ("completed_ops", c.c_uint64)]


InitEmit = c.CFUNCTYPE(c.c_int, c.c_void_p, c.POINTER(InitOperation))


def check_effective_init(lib, raw, adjustment):
    expected, count, skipped = [], 0, 0
    for offset, width, value in struct.iter_unpack("<HHI", raw):
        if offset == 0xffff:
            break
        count += 1
        value = (value + (adjustment if offset == 0x130 else 0)) & 0xffffffff
        if offset >= 0x1000:
            expected.append((4, ((offset - 0x400) >> 1) << 2, value & 0xffff, 0x30000))
        elif width in (2, 4):
            expected.append((1, 0, 0, 0))
            expected.append((2 if width == 2 else 3, offset,
                             value & 0xffff if width == 2 else value, 0))
        else:
            skipped += 1
    observed = []

    @InitEmit
    def record(context, pointer):
        op = pointer.contents
        observed.append((op.kind, op.offset, op.value, op.selector))
        return 0

    job, buffer, addend = EffectiveInit(), c.create_string_buffer(raw), c.c_uint32(adjustment)
    assert lib.axd_effective_init_begin(c.byref(job), 130, 1, buffer, len(raw),
                                       c.byref(addend), 0x1000, 0x20000) == 0
    for _ in range(len(expected) + count + 1):
        if lib.axd_effective_init_step(c.byref(job), record, None) in (4, 5, 6):
            break
    assert job.phase == 4 and observed == expected
    assert job.completed_records == count and job.skipped_records == skipped
    assert job.attempted_ops == job.completed_ops == len(expected)
    return count


def check_upload(lib, primary, extended, minor, boardflags):
    # Expected trace is specified independently from recovered writer assembly.
    expected = []
    for raw, selector in [(primary, 0x03000000), (extended, 0x03080000)]:
        expected.extend([("b", 1), ("w", 0x160, selector), ("r", 0x160), ("b", 2)])
        for (word,) in struct.iter_unpack("<I", raw):
            expected.extend([("b", 1), ("w", 0x164, word)])
    cursor, errors = [0], []

    def accept(event):
        if cursor[0] >= len(expected) or event != expected[cursor[0]]:
            errors.append("Upload trace mismatch")
            return -1
        cursor[0] += 1
        return 0

    @Write32
    def write(context, reg, value):
        return accept(("w", reg, value))

    @Read32
    def read(context, reg, value):
        value[0] = 0xdeadbeef  # Readback is observed, not compared by stock code.
        return accept(("r", reg))

    @Barrier
    def barrier(context, kind):
        return accept(("b", kind))

    buffers = [c.create_string_buffer(raw) for raw in (primary, extended)]
    io, job = UploadIO(None, write, read, barrier), Upload()
    assert lib.axd_upload_begin(c.byref(job), 130, minor, boardflags, 0,
                               buffers[0], len(primary), buffers[1], len(extended)) == 0
    for _ in range(len(expected) + 1):
        if lib.axd_upload_step(c.byref(job), c.byref(io)) in (7, 8, 9):
            break
    assert not errors and job.phase == 7 and cursor[0] == len(expected)
    assert job.attempted_ops == job.completed_ops == len(expected)
    assert tuple(job.words) == (len(primary) // 4, len(extended) // 4)
    assert lib.axd_upload_step(c.byref(job), None) == 7 and cursor[0] == len(expected)
    return sum(job.words)


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
    lib.axd_upload_begin.argtypes = [c.POINTER(Upload), c.c_uint16, c.c_uint16,
        c.c_uint32, c.c_uint32, c.c_void_p, c.c_size_t, c.c_void_p, c.c_size_t]
    lib.axd_upload_begin.restype = c.c_int
    lib.axd_upload_step.argtypes = [c.POINTER(Upload), c.POINTER(UploadIO)]
    lib.axd_upload_step.restype = c.c_int
    lib.axd_effective_init_begin.argtypes = [c.POINTER(EffectiveInit), c.c_uint16,
        c.c_uint16, c.c_void_p, c.c_size_t, c.POINTER(c.c_uint32), c.c_uint32, c.c_uint32]
    lib.axd_effective_init_begin.restype = c.c_int
    lib.axd_effective_init_step.argtypes = [c.POINTER(EffectiveInit), InitEmit, c.c_void_p]
    lib.axd_effective_init_step.restype = c.c_int
    total = 0
    effective_records = 0
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
        for adjustment in (0, 0x12345678, 0xffffffff):
            effective_records += check_effective_init(lib, raw, adjustment)
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
    uploaded_words = 0
    for minor in (0, 1):
        for boardflags in (0, 1):
            chosen = Selection()
            assert lib.axd_select_ucode(130, minor, boardflags, c.byref(chosen)) == 0
            uploaded_words += check_upload(lib, blobs[chosen.primary.decode()]["data"],
                blobs[chosen.extended.decode()]["data"], minor, boardflags)
    print(f"Four stock tables: {total} raw records validated and replayed exactly.")
    print(f"Effective init: {effective_records} records matched under three explicit synthetic runtime contexts.")
    print("Eight selector cases match stock object sizes; five unsupported identities rejected.")
    print(f"Four stock pairs: {uploaded_words} little-endian words matched independent simulated upload traces.")
    print("No MMIO, device firmware upload, calibration access or radio operation occurred.")


if __name__ == "__main__":
    main()
