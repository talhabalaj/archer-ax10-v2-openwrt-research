#!/usr/bin/env python3
"""Compare actual SMC C transfer with the pinned caller-supplied stock image.

Arguments: pinned wl.ko and a trusted host library containing smc_stock_adapter.c.
All proprietary bytes and trace values remain in memory. Output is counts only;
this does not execute the stock binary, MMIO, a device upload or RF operations.
"""
import argparse
import ctypes as c
import importlib.util
from pathlib import Path
import struct


class Operation(c.Structure):
    _fields_ = [("kind", c.c_int), ("offset", c.c_uint32), ("value", c.c_uint32)]


class Result(c.Structure):
    _fields_ = [(name, c.c_uint32) for name in (
        "phase", "error", "image_rows", "table_rows", "data_writes", "download_completed",
        "reset0_completed", "reset1_completed", "loaded", "ac_downloaded", "loaded_committed")]
    _fields_ += [("attempted_ops", c.c_uint64), ("completed_ops", c.c_uint64)]


IO = c.CFUNCTYPE(c.c_int, c.c_void_p, c.POINTER(Operation), c.POINTER(c.c_uint32))
ST, SY, W32, R32, R16, W16 = 1, 2, 3, 4, 5, 7


def expected_trace(raw):
    """Independent literal assembly contract, with strict supplied read replies."""
    expected = []

    def emit(kind, offset=0, value=0, reply=None):
        expected.append((kind, offset, value, reply))

    def direct(offset, value):
        emit(ST)
        emit(W16, offset, value)

    def packed(selector, value):
        emit(ST)
        emit(W32, 0x3fc, selector | value << 16)

    # Actual reset0 is a packed write, followed by the saved-sample pulse.
    packed(0x140b, 0)
    emit(R16, 0x7c0, reply=0x11234)
    emit(SY)
    direct(0x7c0, 0x12b4)
    direct(0x7c0, 0x1234)
    # Explicit sampled MAC-inactive and guard-already-set input path. No unknown
    # read ever obtains an implicit zero or the previous written value.
    emit(R32, 0x120, reply=0)
    emit(SY)
    direct(0x3fc, 0x19e)
    emit(R16, 0x3fe, reply=2)
    emit(SY)
    packed(0xd, 47)
    packed(0xe, 0)
    rows = (len(raw) // 6) & 0xffff
    for index in range(rows):
        low, middle, high = struct.unpack_from("<HHH", raw, index * 6)
        packed(0x11, low)
        direct(0x3fe, middle)
        direct(0x3fe, high)
    # Reset1 runs only after the whole table child has returned.
    for selector, reply, value in ((0x1409, 0x11234, 0x9234), (0x1410, 0x4001, 0x4003),
                                    (0x140b, 0x8002, 0x8003), (0x140b, 0xabc1, 0xabc3)):
        direct(0x3fc, selector)
        emit(ST)
        emit(R16, 0x3fe, reply=reply)
        emit(SY)
        emit(W16, 0x3fe, value)
    return expected, rows


def check(lib, raw):
    expected, rows = expected_trace(raw)
    cursor, errors = [0], []

    @IO
    def record(opaque, pointer, reply):
        del opaque
        if cursor[0] >= len(expected):
            errors.append("Unexpected SMC operation")
            return -1
        op = pointer.contents
        kind, offset, value, supplied_reply = expected[cursor[0]]
        if (op.kind, op.offset, op.value) != (kind, offset, value):
            errors.append("SMC operation order/width/value mismatch")
            return -1
        if kind in (R16, R32):
            if supplied_reply is None:
                errors.append("Missing explicit SMC read reply")
                return -1
            reply[0] = supplied_reply
        cursor[0] += 1
        return 0

    data, result = c.create_string_buffer(raw), Result()
    assert lib.axd_smc_stock_run(data, len(raw), len(raw), record, None, c.byref(result)) == 0
    assert not errors and cursor[0] == len(expected)
    assert (result.phase, result.error) == (2, 0)
    assert result.image_rows == result.table_rows == rows == 1903
    assert result.data_writes == rows * 3
    assert result.download_completed == result.reset0_completed == result.reset1_completed == 1
    assert result.loaded == result.ac_downloaded == result.loaded_committed == 1
    assert result.attempted_ops == result.completed_ops == len(expected)
    return len(expected)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("library", type=Path)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1] / "scripts/radio_firmware_inventory.py"
    spec = importlib.util.spec_from_file_location("firmware_inventory", source)
    inventory = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(inventory)
    inventory.require(args.module.stat().st_size <= 64 * 1024 * 1024, "Input exceeds size bound")
    report, blobs = inventory.inspect(args.module.read_bytes())
    metadata = next(item for item in report["objects"] if item["kind"] == "smc-image")
    assert len(inventory.MICROCODE) == 8
    assert metadata["size"] == metadata["size_symbol"]["value"] == 11420
    assert metadata["row_count"] == 1903 and metadata["ignored_trailing_bytes"] == 2
    assert metadata["transferred_bytes"] == 11418
    raw = blobs["d11smc1"]["data"]
    lib = c.CDLL(str(args.library.resolve(strict=True)))
    lib.axd_smc_stock_run.argtypes = [c.c_void_p, c.c_size_t, c.c_uint32, IO,
                                     c.c_void_p, c.POINTER(Result)]
    lib.axd_smc_stock_run.restype = c.c_int
    operations = check(lib, raw)
    # Prove the exact trailing two bytes are unused rather than padded into a
    # fourth halfword: changing only those private bytes leaves the full trace.
    changed_tail = raw[:-2] + bytes(byte ^ 0xff for byte in raw[-2:])
    assert check(lib, changed_tail) == operations
    # The source binding rejects any different module before looking up objects.
    changed_module = bytearray(args.module.read_bytes())
    changed_module[-1] ^= 1
    try:
        inventory.inspect(changed_module)
    except ValueError as error:
        assert str(error) == "Unrecognized module hash"
    else:
        raise AssertionError("Changed source module was accepted")
    print(f"Stock SMC: 1903 width48 rows, 5709 halfwords and {operations} full ordered I/O operations matched.")
    print("Two trailing bytes ignored exactly; changed source rejected; no payload data was printed or written.")
    print("No stock code, MMIO, device upload or radio operation occurred.")


if __name__ == "__main__":
    main()
