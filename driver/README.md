# First BCM63178 driver components

These are original, offline C components for the core 130 investigation. They
have no kernel binding, MMIO backend, firmware uploader or RF operation. They
are useful building blocks and test oracles, not a functioning Wi-Fi driver.
All original code here uses the repository's MIT license.

## Implemented

- `ucode_select.c`: select primary/extended microcode symbol names and pinned
  lengths for D11 core 130 minor 0/1, using bit 0 of the correct slice's
  boardflags. Unknown cores/minors fail with empty output. Selection does not
  establish readiness or permission to upload; the stock downloader also has
  runtime skip/ownership conditions not implemented by this pure selector.
- `init_table.c`: decode the pinned little-endian 8-byte initialization-record
  format and validate the entire input before any recording callback. Preserve
  widths, order and repeated offsets; stop on callback failure without retry.
  Only offsets within a caller-bounded window of at most `0x1000` are accepted.
- `scripts/radio_firmware_inventory.py`: pin the exact stock ELF digest, resolve
  selected file-backed objects through section indices, reject relocations into
  those objects, check microcode size companions, and emit metadata only.

The record validator intentionally rejects nonzero upper bits of 16-bit values,
nonzero terminator fields, trailing records and misalignment. Those restrictions
are stricter than the observed stock consumer, which stops on offset `0xffff`
and truncates 16-bit writes. All four inspected stock tables satisfy the stricter
subset. This is not a general emulation of every vendor table.

## Important missing semantics

The real `wlc_write_inits` is context dependent. For core 130, a write request at
offset `0x130` receives an adjustment from runtime hardware context. Offsets at
or above `0x1000` take an object-memory path rather than ordinary MMIO. Our
replayer emits **raw, unadjusted records** to a recorder and rejects the latter
range. Do not connect its callback directly to a register-write function.

Microcode upload, PSM/PSMx reset/start/readiness, register descriptor typing,
clock/resource ownership, calibration, AC-PHY 51 initialization and Linux TX/RX
integration remain absent. PHY type 11 and the name `ax51` do not establish an
upstream driver implementation. Neither component loads or executes microcode.

See [REA attach/selection evidence](../docs/DRIVER_ATTACH_AUDIT.md),
[source map](../docs/DRIVER_SOURCE_MAP.md),
[upstream baseline](../docs/DRIVER_BASELINE_AUDIT.md), and
[object inventory](../findings/RADIO_FIRMWARE_INVENTORY.json).

## Validation on macOS

Build outputs stay outside the public checkout. A pinned stock `wl.ko` is needed
only for the second, private-input check; it is not distributed here.

```sh
AXD_BUILD_DIR="$(mktemp -d)"
cc -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  driver/init_table.c tests/test_init_table.c -o "$AXD_BUILD_DIR/test_init_table"
"$AXD_BUILD_DIR/test_init_table"

cc -std=c11 -Wall -Wextra -Werror -pedantic -dynamiclib \
  driver/init_table.c driver/ucode_select.c -o "$AXD_BUILD_DIR/libaxd_offline.dylib"
python3 tests/check_stock_firmware.py /private/path/to/wl.ko \
  "$AXD_BUILD_DIR/libaxd_offline.dylib"
```

The checks passed on 2026-10-15:

- Synthetic malformed/truncated/missing-terminator, width, alignment, bounds,
  duplicate-order, sentinel-only and callback-failure cases under ASan/UBSan.
- Exact C-versus-Python raw replay equality for 1,404 records across four stock
  tables, including a sentinel-only extended band table.
- Eight selection cases match the stock primary/extended array names and sizes;
  five unsupported core/minor identities return empty output.
- Both C files cross-compile with Clang for `armv7-none-eabi` in freestanding
  mode with warnings treated as errors. This proves compilation, not linking
  into Linux, execution on ARM or hardware behavior.

## Private object extraction

Default inventory prints only hashes, offsets, sizes and counts:

```sh
python3 scripts/radio_firmware_inventory.py /private/path/to/wl.ko
```

The optional `--private-output /private/path/to/new-directory` exports the
selected proprietary objects with private permissions. The directory must be
new and outside this checkout. No extracted objects or table values belong in
Git. The tool rejects a different module digest, including the older GPL object;
firmware/table versions must not be silently mixed.

## Next implementation boundary

Recover `wlc_ucode_write` and its extended-bank companion, map the relevant
core-130 register descriptors, and model the upload/readiness sequence against
a simulated register backend. The stock slice-specific boardflags and runtime
adjustments must be supplied explicitly before a hardware-equivalent plan can
be claimed. Hardware execution remains a later, separately validated stage.
