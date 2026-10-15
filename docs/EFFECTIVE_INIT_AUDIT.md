# Effective core-130 initialization model

Original implementation and independent tests, 2026-10-15. Input identity and
consumer Evidence IDs are in [the attach audit](DRIVER_ATTACH_AUDIT.md#initialization-record-consumer-semantics).
This extends the raw recorder with the recovered consumer semantics; it has no
MMIO backend and does not run on the router.

## Evidence-derived operations

The little-endian record is `u16 offset, u16 width, u32 value`. Offset `0xffff`
terminates before processing; other sentinel fields and following whole records
are ignored. An unterminated or non-record-sized buffer fails validation.

For supported core 130 minor 0/1:

| Condition | Semantic output |
| --- | --- |
| Offset below `0x1000`, width 2 or 4 | Store-barrier event followed by a write of that width. Width 2 truncates the final value to 16 bits. |
| Offset `0x130`, admitted width | Add the explicit `hw + 0x260` runtime word, modulo 2^32, before width truncation. |
| Offset below `0x1000`, another width | Skip the record, emitting no barrier/write. |
| Offset at least `0x1000`, excluding sentinel | Object-copy helper operation at **u32 byte offset** `((offset - 0x400) >> 1) << 2`, low-16-bit **value**, byte length 2, selector `0x30000`, regardless of width. |

The object transform is deliberately not narrowed to 16 bits: offset `0xfffe`
becomes byte offset `0x1f7fc`. Adjacent odd/even record offsets alias through the
right shift. The model stops at `wlc_bmac_copyto_objmem` arguments; its physical
indirect-port expansion remains unimplemented.

Consumer pseudocode Evidence:
`ev_5b7779d029093445701be0f68e1de8b4ac9e5bfeaf2f8bca9d47015237fb1b67`.
Assembly:
`ev_1238784e268a149f826b7819cf3f3ee1cc094d848c1075722c28c506f0388386`.
Core-130 descriptor fixup register:
`ev_cb33e86ef10716e3f43721f11e81b303b339505151b5ba940887f4376ed7e179`.
[Published source notes](D2_CONTEXT_SOURCE_NOTES.md) corroborate address-unit and
halfword-lane concepts, without substituting an older debug reader for the
current writer.

## Explicit model policy and API

`driver/effective_init.c` validates all actionable records before callbacks.
Caller-supplied MMIO/object capacities and MMIO alignment are stricter checks
than the unchecked consumer. Capacities do not claim hardware geometry.
A null addend pointer means unknown and rejects an actionable write at `0x130`;
an explicit zero is valid. Other tables need no unused addend. The addend is
snapshotted; table bytes remain immutable/alive throughout the job.

Each step performs at most one callback or skips one record. The callback gets a
typed barrier, MMIO16, MMIO32 or semantic object-copy event. Terminal states emit
nothing further. Progress distinguishes entered/successful callbacks and
completed/skipped records; failures have no invented rollback/retry.
A successful callback may cancel, including the final operation, without losing
that cancellation. An error wins over simultaneous cancellation. APIs remain
single-threaded; callbacks may cancel but must not reenter begin/step or mutate
job fields. These contracts do not supply kernel synchronization or hardware
cleanup.

## Validation and limits

- Literal mixed synthetic trace covers both widths, addend wrap/carry/truncation,
  unchanged adjacent registers, skipped widths, object dispatch for all tested
  widths, odd aliases, highest nonsentinel offset and ignored trailing records.
- Every one of the 13 callback positions is tested for error, cancellation and
  simultaneous error/cancellation, including terminal behavior and progress.
- Invalid late rows, missing context, bounds/alignment and malformed buffers
  fail before any callback.
- A separate Python formula oracle compares full operation tuples for all four
  pinned stock tables under three **synthetic** addends: 4,212 record comparisons.
  This does not recover the active router's runtime addend.
- Synthetic tests pass under ASan/UBSan; the component compiles for freestanding
  ARMv7 with strict warnings. Raw/selection/upload comparisons remain separate.

Remaining work includes addend acquisition, physical object-helper lowering,
complete SHM/register contracts, lifecycle/clock/resource ownership, PHY 51,
DMA/RX/TX and Linux integration. No firmware execution or radio operation is
established by these tests.
