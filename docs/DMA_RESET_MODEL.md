# Current DMA reset implementation

Original offline `driver/dma_reset.c`, based on exact-current-WL
[resource evidence](DRIVER_RESOURCE_AUDIT.md#actual-dma-reset-callees).
[Published older DMA source](DMA64_SOURCE_AUDIT.md) corroborates common concepts;
it does not substitute for the current binary's predicates, cache behavior or
queue resources.

## Boundary

Callbacks identify TX-channel, RX-channel or indirect-selector resources.
Control/status offsets are relative to a supplied channel (`0`, `0x10`);
selector offset zero means its separately supplied register. This API neither
chooses a physical queue nor derives register mappings from core ID. Runtime
context explicitly supplies ring count, indirect flag, flag-`0x100` post-poll
choice, selector value/cache and TX cache. Core 130 minor 0/1 is the supported
model boundary; these inputs do not establish a valid queue allocation.

| Path | Operations and return behavior |
| --- | --- |
| Empty count | Return true, no I/O or cache changes; no disabled status observed. |
| Direct TX already disabled | Read status/`SY`, return true without changing TX cache. |
| Direct TX active | Write control 2 after `ST`; poll until state is zero or `(status & 0xe0000000) == 0x20000000`. Exhaustion still proceeds to disable. |
| Indirect TX | Always `ST`, write selector, then copy its low byte into selector cache; no initial status read or cache shortcut. |
| TX disable | `ST`, write control zero. Direct mode and indirect flag-`0x100` paths poll disabled; ordinary indirect skips the poll and returns true. Paths reaching disable invalidate TX cache on normal return. |
| TX post-disable timeout | After the bounded poll requests 300 us, invalidates TX cache, returns false; writes remain issued. |
| RX | `ST`, write control zero and poll disabled. No selector operation, TX-cache invalidation or 300-us timeout delay. |

Each wait has an initial status sample followed by at most 1,000 delay-10/read
iterations; each read has `SY`. There is no extra final confirmation. Disabled
means `(status & 0xf0000000) == 0`. A suspend-request bit alone is not completed
suspension. Status bits outside the predicate do not change the result.

`TRACE_COMPLETE` exposes the stock boolean `return_value`. It can be false.
`disabled_observed` distinguishes an actual zero-state sample from empty or
indirect-no-poll success. `suspend_exhausted` records direct suspend exhaustion
even if later disable succeeds. None attests over-the-air transmission or safe
packet-buffer reclamation.

Each step performs one callback maximum or a bounded internal transition.
Entered/successful callback counters do not prove hardware commitment. Failed
callbacks and cancellation terminate without retry/rollback. A successful
callback that cancels remains cancelled; simultaneous callback error wins.
Context is snapshotted before clearing the job, including a restart through
`&job.context`. Callbacks are single-threaded and may not reenter or mutate fields.

## Validation

Independent literal traces and assembly-derived long scripts pass under
ASan/UBSan: both directions, direct/indirect and flag-`0x100` cases, empty/early
returns, exact 1,000-delay boundaries, suspend exhaustion, TX-only timeout delay,
selector/cache behavior and observed-state distinctions. Every callback in the
selected normal and maximum-wait traces covers error, successful cancellation
and simultaneous error/cancellation, including the final 300-us request.
Aliased-context restart, invalid inputs and terminal silence are covered too.
Freestanding ARMv7 compilation passes; these are host/build results only.

## Remaining integration

The higher core reset must supply proven queue lists/presence, mapping and flags,
serialize selector access, coordinate MAC/PHY/clock state and handle a false
reset result. No buffer mapping, descriptor ownership/reclaim, DMA barriers for
packet memory, interrupt handling or Linux backend is supplied here. Exact-board
hardware tests and the larger driver milestones remain unfulfilled.
