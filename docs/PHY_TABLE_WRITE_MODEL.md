# Actual AC PHY table writes

`driver/phy_table_write.c` implements the recovered core130/AC PHY51
`wlc_phy_table_write_acphy` wrapper, its extended writer and the actual direct
packed PHY-write primitive. It composes existing real PHY-register and PSM
children. The new writer embeds no table payload, firmware or calibration data.
It has no MMIO backend or hardware-ready result.

## Wrapper and physical order

The outer wrapper reads main control at D11+120 then SY. Only sampled bit0
requests actual MAC suspend. Next it reads PHY19e and, only when bit1 was clear,
sets bit1 by an actual fresh RMW. After table transfer it clears that bit only
when this invocation set it, then conditionally invokes real MAC enable. No
extra PHY nesting or lock operation is invented. The called HWACI disable and
restore helpers in this exact stock input are proven zero-return/no-op leaves.

Header writes use packed PHY selectorD for table ID low16, then selectorE for
offset low16. Count0 still writes both headers. Supported data widths follow
these actual protocols:

| Width | Input bytes per record | Ordered writes |
| --- | --- | --- |
| 8 | 1 | Packed selectorF with the byte zero extended |
| 16 | 2 | Packed selectorF with the low16 value |
| 32 | 4 | Packed selector10 with high16, then packed selectorF with low16 |
| 42 / 48 | 6 | Packed selector11 with first16, then two direct ST/W16 data-port continuations |
| 60 / 64 | 8 | Packed selector11 with first16, then three direct ST/W16 data-port continuations |

Byte data is explicitly little-endian and immutable. Width42/60 do not mask
the extra supplied high bits: the stock helpers write the same complete
six/eight bytes as width48/64. No source payloads are provided by this API.

Each packed PHY write emits `ST,W32(3fc, selector | value16<<16)`. On shared bus
word1, first increment the u16 bookkeeping counter with wrap. If the narrowed
counter is at least the supplied threshold, reset it to zero and emit
`R16(3e0),SY` before the packed store. Threshold0 therefore always flushes.
Other bus words do not update this counter. Wide data continuations emit
`ST,W16(3fe,value16)` directly and do not increment the packed-write counter.

`axd_phy_packed_write_begin` exposes that same direct leaf for selectors below
C000, including its counter/flush behavior, without MAC or table guards. High
selectors require the actual separate table29 dispatch and are rejected by this
direct-only entry. The SMC reset0 path can compose the actual packed write
instead of substituting selector/data halfword writes.

## Context, limits and failure

The writer shares `axd_phy_table_context`: PHY bookkeeping, MAC caches/depths,
policy facts, bus word and threshold remain canonical. Nested pre-I/O software
stores are published before the outer callback. Every child partial context is
retained after its step, including errors and cancellation. A sampled running
MAC requires a valid coherent MAC binding and real suspend/enable; unknown
policy/MBSS/fatal/band state propagates as a dependency rather than a successful
placeholder. The resource owner supplies stable powered/clocks-enabled D11/PHY
access, serialization and any outer PHY nesting required by its caller.

The request and context are copied before job clearing, including own-context
aliases. Payload storage is borrowed outside all active jobs through terminal
state; its byte length must cover count times the width's stride. Unsupported
widths, short/null reached data and multiplication overflow are rejected before
I/O. This is stricter than stock's unchecked caller inputs. Count itself is an
explicit immutable u32; stock reloads the descriptor during its loop.

Each step performs at most one synchronous callback. Pre-I/O counter stores
survive a later failure; successful guard-set writes retain `changed_guard`.
`data_writes` counts successful data callbacks, including a callback that
cancels; headers and attempted failures are excluded. Callback error wins over
simultaneous cancellation. Terminal states are silent and perform no invented
guard restoration, MAC enable, retry or rollback. Trace completion does not
prove a physical table transfer or readiness.

## Independent validation

`tests/test_phy_table_write.c` uses original literal queues for all seven
widths, two records, zero count, low16 headers, high-before-low width32 order,
wide continuations, guard set/clear, active MAC suspend/enable and reached MAC
dependencies. It checks bus1 thresholds0/1/3, other bus words, u16 wrapping,
direct packed writes, aliases and pre-step cancellation. Every callback in
wide/flush/guard/MAC paths receives error, cancellation and both together.
Partial data counts, counter commits, guard state, canonical MAC cache timing,
one callback per step and terminal silence are asserted.

Strict C11 ASan/UBSan checks and ARMv7 freestanding compilation passed. Six
private mutation copies were rejected: reversed width32 halves, packed wide
continuations, late threshold comparison, missing guard clear, lost cancellation
and stale pre-I/O MAC publication. These checks execute original C with synthetic
inputs; they do not execute the stock binary or a router.

The [selected evidence](../findings/DRIVER_PHY_INIT_EVIDENCE.json) includes the
actual wrapper, extended writer, generic writer, packed leaf and HWACI helpers.
Targeted four-byte jump-table observations independently resolve width32 and48
case entry points without publishing the raw table or payloads.
