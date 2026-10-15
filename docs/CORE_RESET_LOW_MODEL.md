# Lower core-reset and selected-wrapper helpers

`driver/core_reset_low.c` implements the target `wlc_bmac_core_reset` wrapper
around the existing target D11 AI-reset child, generic selected-wrapper AI
reset, SI selection, actual iscoreup and main
mctrl. This is an offline trace implementation with explicit caller bindings.
It does not establish successful physical reset, RF operation or Linux integration.

The input callback-presence bit means the registered reset callback is the
recovered SI reset implementation. It is not authorization to substitute any
arbitrary callback. The current target is supplied through coherent SI catalog,
selected-index, mapping and wrapper state. All catalogs are borrowed immutable
storage outside every job. Unknown/null reached wrapper mappings stop as a
binding dependency rather than producing a successful synthetic hardware result.
The caller supplies register ownership, mapping/window serialization and actual
I/O adapters. Input context may alias the job's previous context.

The wrapper returns before other work for absent hardware/reset callback.
Revisions below130 preserve both caller words. Later revisions set reset mask `0x20`;
core130 sets normal mask `0x20` only with W present and W138 equal2, otherwise clears
it. Other later revisions set normal mask `0x20`. After the actual reset callback
returns, it saves the current SI index and selects core849 unit0. A zero returned
mapping ends immediately, including after a selection that committed a new index;
there is no automatic restore or bus0 tail on this branch.

A nonzero auxiliary mapping runs fresh actual `si_iscoreup`. A down result runs
actual generic SI/AI reset with bits0/resetbits0. `core_reset_low_ai.c` implements
the selected-wrapper protocol for generic cores as well as the D11 revision61 or
128..132 special predicate: generic reset omits both special force-gated-clock
clear sequences and its final control write is bits|1. Both paths retain actual
busy/release reads, barriers, bounded delays and stuck-reset diagnostics. A
completed trace does not establish reset success. `si_select.c` exposes a shared selected-wrapper entry, preserving its real
map/cache/index ordering; the existing selector entry defaults to primary. When secondary physical metadata is
nonnull, SI reset selects secondary, resets it, selects primary and resets it.
It does not merely annotate a secondary reset while leaving SI state unchanged.

The original SI index is then restored through the real selector. Only bus0
runs actual main mctrl(FFFFFFFF,400), followed by ST/W16(a08,0), ST/W16(a06,0).
The mctrl and SI contexts propagate after each child step, including successful
callbacks that cancel the parent. Callback error takes precedence over
simultaneous cancellation. Terminal states emit no operations. No rollback,
retry, cleanup, final restoration or successful stub is invented.

`driver/si_core.c` separately exposes actual SI/AI iscoreup and coreflags.
Iscoreup short-circuits after R32(408)/SY when low2 differs from1, otherwise
samples R32(800)/SY and inverts low1. Coreflags implements the observed chip/core
exemptions and either a read/SY or read/SY/ST/write/read/SY protocol, without
adding delays. SB remains an explicit dependency; unsupported interconnects
perform the observed return-zero/no-op. Concrete wrapper identity is part of
each operation. The exact retained static Evidence IDs are selected in
[CORE_RESET_LOW_EVIDENCE.json](../findings/CORE_RESET_LOW_EVIDENCE.json).

`tests/test_core_reset_low.c` constructs an independent ordered trace with
strict supplied read values. It covers D11 and generic auxiliary paths,
secondary-then-primary reset, up/down short circuit, zero-map early return,
revision/W flags, every callback failure/cancellation/error-plus-cancellation,
map-cache effects on cancellation, primary/secondary PCI-window selection and
own-context alias initialization. The callback checks canonical main-cache
publication before the first bus0 mctrl barrier, including failed barriers.
`tests/test_si_core.c` independently checks read-only and writing flags protocols,
unmasked value bits, chip/core exemptions, iscoreup short-circuit/fresh samples,
unknown/null bindings, interconnect dispatch, aliases and all callback exits. It also
asserts at most one callback per step and silent terminal calls. These synthetic
host checks and freestanding ARMv7 compilation do not execute the stock binary
or any router. Physical bindings, OS ownership, SB paths and complete radio/
DMA/RX/TX/kernel integration remain outside this component.
