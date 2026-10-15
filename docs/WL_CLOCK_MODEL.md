# Composed WL clock selection

`driver/wl_clock.c` composes the actual `wlc_clkctl_clk` selection caller for
D11 core 130 minor 0/1. Its selected children are the existing direct D11
clock controller and the actual HND SI/ChipCommon resource wrapper. It supplies
no MMIO, IRQ, PCI, mapping or kernel backend and makes no clock-ready claim.

The retained caller assembly Evidence ID is
`ev_fcc33e70b1e7b43825b239589a5d0e106190027002658aae1f07a6061446202e`.
The HND wrapper and body IDs are
`ev_21ec41d06a093aa45736a9cbc89ca95774687529dd2bb4d0a62323bff251dda6`
and `ev_11b7ca1bf8c23f4e76bba58369273728ef7d4ea7c8f2cc808c417ea016460f24`.
See the [resource audit](DRIVER_RESOURCE_AUDIT.md),
[HND fallback audit](DRIVER_CLOCK_FALLBACK_AUDIT.md) and
[selected Evidence](../findings/DRIVER_RESOURCE_DEPENDENCY_EVIDENCE.json).
The exact artifacts and their hashes are recorded there; no vendor bytes or
raw disassembly are embedded in this component or its fixtures.

## Selection and software stores

The context owns copied `d11` and `fallback` child contexts. Their catalog and
other external storage remain borrowed, immutable caller resources. Begin
checks the explicit core/minor identity and copies context before clearing the
job, including when passed `&job.context`. It does not validate unused child
flags, ownership or catalogs early.

On entry, `d11.si_capabilities & 0x10000000` selects the direct D11 child.
That real child executes the stock select command with the full mode argument;
its final cache byte is `mode == 0`. This branch leaves the reason word
unchanged, including reason mask `0x10`. Unused fallback facts are not read.

When that capability bit is clear, duplicated `fallback.capabilities` must
exactly equal the caller's SI capability word, then the real SI wrapper runs.
This shared-fact binding is checked even when a later signed CC-revision gate
returns early. Unused direct clock-valid/PLL/RSDB flags cannot reject fallback.
The actual SI wrapper preserves its signed revision, fast-map, resource
selection, IRQ-state, clock-body and restoration conditions. A revision at most
5 returns zero; a later revision generally returns `mode == 0`, even when a
capability/mode gate skips the clock body. Neither return means readiness.

Only after the SI child completes does the coordinator retain its full
32-bit return and enter two separate internal steps:

1. Sample the old reason word, then store the return's low eight bits in
   `context.d11.fast_clock` and set `cache_committed`.
2. Test the **full return** against zero, set or clear mask `0x10` in the saved
   reason word, store it in `context.d11.wake_reasons`, and set `reason_committed`.

Cancellation between these steps retains the byte store and the original
reason word. The shipped HND return is 0/1; tests do not invent an unreachable
high-valued return. The full-width condition remains explicit in the code.
This fallback branch does not call any PSM wake setter/clearer, MAC-control,
wake-policy or wait-for-wake helper. Its reason update is inline software work.
The direct branch has no HND return and no fallback reason commit.

## Typed operations and partial state

The outer operation tags D11 versus SI and contains the corresponding original
child operation. D11 current/unit-1 regions remain typed; SI operations retain
core/primary/secondary mapping, ChipCommon, PCI configuration and IRQ resources,
addresses, widths, callback labels and contexts. Equal offsets in D11 and
ChipCommon are not the same resource. IRQ operations expose the child's
read-only SI-state pointer only for the duration of the callback.

The reply carries the child read/mapping/token value plus actual reported SI
state and its known flag. A successful IRQ callback must report its resulting
state, even if unchanged. Missing state produces dependency; entered IRQ error
leaves the SI child's `state_valid` false. An old copied snapshot must not then
be treated as current. Inspect the retained SI child for that flag, token,
selection/restoration progress and detailed dependency/error. Inspect the
retained D11 child for status freshness, skipped requests and exhausted polls.

Every step enters at most one user callback or makes a bounded internal
transition. Both child contexts propagate after every child step, including
failure and cancellation. Successful callback effects survive cancellation;
a simultaneous callback error wins. Callback counters count entered user calls,
not internal adapters. A missing callback fails without counting a user call.
Cancellation before ENTRY or between software commits is supported. Terminal
states are silent, and cancellation reaches the active real child. No terminal
exit invents resource restoration, IRQ restoration, retry, cache update or
reason change. Callbacks may cancel but may not reenter or mutate jobs.

## Validation

The original literal fixtures independently specify direct fast/dynamic events,
ordinary SI mapping and IRQ restoration, fast SI mapping, old-revision PCI
crystal operations, capability/mode skips and signed revision early returns.
They verify cache-before-reason commits with nonzero prior reason words, both
set and clear outcomes, direct reason preservation, shared-capability mismatch,
IRQ unknown/invalid state, successful and failed IRQ mutation, ownership/catalog
dependencies, alias-safe reuse of latest mapping caches and terminal silence.
Selected callback positions cover 174 total error/cancellation/coincident runs;
41 internal-step prefixes additionally check cancellation without later effects.
These checks supplement the independent child tests, rather than replacing
their complete CC-revision and selector coverage.

Direct verification from the checkout root:

```sh
clang -std=c11 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  tests/test_wl_clock.c driver/wl_clock.c driver/clock_control.c \
  driver/si_clock.c driver/si_select.c driver/chipcommon_clock.c \
  -o /tmp/axd-test-wl-clock
/tmp/axd-test-wl-clock
clang --target=armv7-none-eabi -ffreestanding -fno-builtin \
  -std=c11 -Wall -Wextra -Werror -pedantic \
  -c driver/wl_clock.c -o /tmp/axd-wl-clock-armv7.o
```

Both checks pass. These are offline host composition and cross-compilation
checks; actual instruction differential coverage of the whole coordinator,
modern-kernel linkage, physical mappings, IRQ semantics and hardware clock
behavior remain unverified.
