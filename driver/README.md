# First BCM63178 driver components

These are original, offline C components for the core 130 investigation. They
have no kernel binding, MMIO backend, device firmware uploader or RF operation.
They are useful building blocks and test oracles, not a functioning Wi-Fi driver.
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
- `ucode_upload.c`: reproduce the fresh-chunk core-130 transfer trace, one
  simulated I/O event per step. Both input lengths and the core/minor are
  validated before callbacks. Input chunk state must be supplied explicitly
  as zero. Failure/cancellation is terminal; no automatic retry or reset occurs.
- `status_poll.c`: independent caller-configured status polling with an injected
  monotonic timestamp. Each step performs at most one read. This is a generic
  test facility, with no claimed BCM63178 ready register, mask or start sequence.

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

Device microcode upload, PSM/PSMx reset/start/readiness, complete register
descriptor typing, clock/resource ownership, calibration, AC-PHY 51 initialization and Linux TX/RX
integration remain absent. PHY type 11 and the name `ax51` do not establish an
upstream driver implementation. These components execute no device microcode.

See [REA attach/selection evidence](../docs/DRIVER_ATTACH_AUDIT.md),
[upload writer evidence](../docs/DRIVER_UPLOAD_AUDIT.md),
[source map](../docs/DRIVER_SOURCE_MAP.md),
[upstream baseline](../docs/DRIVER_BASELINE_AUDIT.md), and
[object inventory](../findings/RADIO_FIRMWARE_INVENTORY.json).

## Run the checks

Build outputs stay outside the public checkout. A pinned stock `wl.ko` is needed
only for the second, private-input check; it is not distributed here.

```sh
sh scripts/test_driver.sh
# Optional additional comparison using caller-supplied private stock input:
sh scripts/test_driver.sh /private/path/to/wl.ko
```

The checks passed on 2026-10-15:

- Synthetic malformed/truncated/missing-terminator, width, alignment, bounds,
  duplicate-order, sentinel-only and callback-failure cases under ASan/UBSan.
- Exact C-versus-Python raw replay equality for 1,404 records across four stock
  tables, including a sentinel-only extended band table.
- Eight selection cases match the stock primary/extended array names and sizes;
  five unsupported core/minor identities return empty output.
- The transfer trace matches 104,440 little-endian words across four stock pairs
  and 208,912 callback events. A deliberately different selector readback value
  is accepted because the recovered writer discards it.
- Synthetic upload tests cover 15 failure points across both banks and each
  operation type, cancellation before and during transfer, invalid inputs and
  missing callbacks. Seven callback-triggered cancellations include the final
  data write; a callback error takes precedence. Calls after terminal
  states produce no additional I/O.
- Generic fake-clock status tests cover immediate/delayed success, waiting for
  bits to clear, exact deadline precedence, no extra reads with a stalled clock,
  backward-clock rejection, read failure, cancellation and invalid predicates.
  Cancellation from a successful matching read remains cancelled; a read error
  takes precedence when the callback also requests cancellation.
- All four C files cross-compile with Clang for `armv7-none-eabi` in freestanding
  mode with warnings treated as errors. This proves compilation, not linking
  into Linux, execution on ARM or hardware behavior.

## Transfer and status semantics

For each bank, the recovered fresh-chunk trace is store barrier, selector write
to `0x160`, readback of `0x160`, full barrier, then a store barrier and a write to
`0x164` for every source word. Main selector is `0x03000000`; extended selector
is `0x03080000`. Both use the same ports; main precedes extended. There is no
extra final readback or barrier. Barriers are recorded events on the host, not
ARM instructions executed by this library.

`axd_upload_step()` makes at most one callback. `TRANSFERRED` means the software
trace ended. It does not mean firmware ran or reported ready. Both buffers and
the backend must remain immutable/stable during a job. Selection/size checking
does not authenticate firmware; the private-input tool supplies digest-pinned
provenance. Nonzero chunk state is rejected instead of inventing continuation
preconditions. The simulator also rejects wrong lengths instead of silently
discarding trailing bytes like the low-level stock writers.

The fake backend checks repeated port writes, selectors and source words against
an independently specified trace. This does not prove physical bank contents,
cursor wrap, auto-increment behavior, reset state or radio execution. Injected
errors may have already caused backend effects: `attempted_ops` counts callbacks
entered, while `completed_ops` counts callbacks returning success. Neither counter
proves hardware commitment. Failure/cancellation causes no invented rollback.
The APIs are single-threaded. Callbacks may request cancellation, but may not
reenter begin/step or modify job fields. Successful callbacks still count toward
progress when they request cancellation; callback errors take precedence over a
simultaneous request. This is cooperative cancellation, not a thread-safe signal.

`axd_poll_step()` takes a caller timestamp and never sleeps. Timeout occurs before
a read when elapsed time is at least the configured timeout. A readiness value
at the exact deadline therefore does not win over timeout. A stagnant clock does
not advance the deadline, but each call remains bounded and no redundant poll
occurs before its scheduled interval. Clock wrap/backward movement is rejected.
Terminal states remain terminal until an explicit new begin call.

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

Recover the actual PSM/PSMx halt, reset, start and hardware-acknowledgement path,
including timeout/recovery behavior. Then add that evidence-derived lifecycle
around the existing transfer trace. The stock slice-specific boardflags, chunk
state, resource ownership and runtime adjustments must be supplied explicitly
before a hardware-equivalent plan can be claimed. PHY 51 initialization and
calibration remain separate work. Hardware execution remains untested.
