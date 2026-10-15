# Registered status release and SI switching

`driver/offload_status.c` implements current `wlc_offload_release_rxstatus`.
`driver/si_switch.c` implements its actual HND `si_switch_core` and
`si_restore_core` children using the existing real SI selectors. The
[selected source index](../findings/OFFLOAD_STATUS_EVIDENCE.json) pins both
modules and retained records. These original portable jobs have no physical
backend. No router or RF operation was performed.

## Registration and real composition

Current BMAC init conditionally writes the recovered release callback to
HW+0x538 and the offload object from WLC+0x714 to HW+0x534, guarded by public
byte0x1c1. Relative to the status-pool base HW+0x524 these are callback0x14 and
argument0x10. Attach pseudocode associates that pool with the status DMA
context through `dma_sts_mp_set`; the latter's actual eight-byte body stores
the pointer at DI+0x100. This identifies the callback; it does not implement
runtime pool acquisition or registration.

The [status processor](RX_PROCESSING_MODEL.md) freshly reads callback, signed
node index and argument in actual order. Null callback or index-1 skips the
call. Otherwise both callback and argument must match explicit known current
registration labels. Only that binding composes the real release job.
Unknown or different registrations remain a dependency. A successful real
return is followed by two byte writes of0xff at node0x18/0x19, then the
existing pool-count/NEXT/high-water/head operations. Failed, cancelled or
dependent children do not reach those later stores.

RX status and packet I/O now have typed reply structures. Their nested replies
carry actual post-IRQ SI state without an output pointer hidden in an operation
or an assumed unchanged SI context. Partial offload and SI contexts propagate
before callbacks and after every child step, including terminal steps.

## Bitmap, consumer and register order

Release reads the indexed bitmap byte and writes that byte with its bit cleared,
even if it was already clear. It then reads the current consumer's bitmap byte
freshly. An active bit or consumer==producer ends the scan. Otherwise consumer
advances as `(consumer+1)&(count-1)`, and its software store occurs **before**
the next bitmap read. Every new byte sample is independent; no cache or assumed
write echo substitutes for a read.

If the original consumer, narrowed to16 bits, equals the resulting consumer,
there is no register access or SI requirement. Otherwise two local SI outputs
start at0. Bus1 calls the real switch child for coreID0x844; other buses skip
that child. The body emits ST, then W32(register base+0x20,current consumer).
A separate fresh source bus test guards the real restore child. No final
readback, barrier or invented cleanup follows.

Serialized software/configuration snapshots make bus and metadata immutable
during a job; this is an explicit narrowing of stock live reloads. Ownership
of bitmap, OS barrier and register region is required only when reached.
Required null identities fail. The adapter owns real byte capacity, register
lifetime and actual effects. A returned cursor does not authorize physical
buffer reuse by itself.

## Actual HND switch and restore

For bus1 with bus-coreID0x83c/0x820, or0x804 with revision>12, switch first
stores the requested coreID into the caller's saved slot. Requested0x800
returns the current map plus0x3000 only if the original map is nonzero.
Requested bus-coreID returns current map plus0x2000, including a zero original
map. Both additions wrap as u32. Other requests fall through to the ordinary
body, overwriting the saved slot at the later actual store. These two fast
restore IDs return without a selector or an IRQ callback.

Ordinary switch optionally calls the real registered IRQ-disable service only
when the current catalog ID matches the IRQ ID. Its actual token is saved;
the restoration index is loaded from the **post-callback** SI state. The
actual ID/unit0 selector then runs. Ordinary restore runs the actual index
selector first, then independently tests the current ID for IRQ restoration.
Disable and restore callbacks need not both execute. Selector cache/current
map stores are visible before later mapping or PCI callbacks.

IRQ services are typed OS boundaries, not vendor helpers replaced with success.
Each successful IRQ callback must report its actual resulting SI state, even
if unchanged. Entering one invalidates the previous state's freshness; error
leaves it unknown, and a successful reply explicitly restores freshness.
`context.state_known` carries that fact through a restarted begin. Ordinary
paths cannot consume unknown SI state; a fast restore skip can leave it unused.
No automatic restoration runs after error, cancellation or a dependency.

## Bounds, cancellation and checks

The implementation narrows unchecked stock memory behavior to a power-of-two
count2..65536, in-range cursors, nonnegative in-range release index and checked
reached bitmap-byte capacity. A scan is bounded by count. The RX caller performs
the actual signed16 extension, so a malformed negative index other than the
already skipped-1 fails rather than indexing before the bitmap. These checks
are model policies, not recovered vendor validation.

Every step performs at most one callback. Successful observations/effects remain
committed on cancellation; later software transitions do not run. Callback
error wins simultaneous cancellation. Terminals are silent. Aliased own-context
begins snapshot before clearing. No retry, rollback, buffer free or unmap is
invented.

Independent literal ASan/UBSan tests cover wrapping bitmap scans, no-progress
skips, producer and active-bit termination, ST/publication order, real bus1
PCI/IRQ composition, all callback error/cancel/both boundaries, fast SI windows,
post-disable index changes, unknown-state restart, and pre-callback mapping
state. Status tests exercise the real child and subsequent two-byte index
clear/pool stores, including faults throughout the composed timeout path.

The optional checker uses only caller-supplied pinned stock instructions:

```sh
python3 tests/check_offload_status_arm.py /private/path/to/wl.ko
```

With Unicorn2.1.4 it matches220 bus0 cases,997976 actual instructions and three
fail-closed probes. Typed synthetic memory rejects unknown context/stack reads,
unexpected accesses, unreviewed relocations and **every reached call**. No
external stub executes. Bus0 genuinely skips the two HND calls; the checker
does not claim ARM comparison of bus1, pool registration, physical MMIO or
complete RX. Eight meaningful compiling status/SI mutants are rejected.
Literal source-backed tests cover bus1 separately. Status fill,
BMAC header/status merge, IRQ/deferred work, packet decoding and Linux delivery
remain required.
