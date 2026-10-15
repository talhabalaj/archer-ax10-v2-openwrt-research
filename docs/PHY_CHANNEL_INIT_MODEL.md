# Actual PHY51 radar and HWOBSS initialization

The original [channel helper](../driver/phy_channel_init.c) implements the complete
current AC PHY51 radar-init body and the separate HWOBSS enable/disable body for
core130.0/1. The [ordered scheduler](../driver/phy_scheduler.c) now composes radar
at actual priority10 when chanspec bit14 requires it. Other noise-mode and
channel/tuning/calibration children remain incomplete. This is offline source
and transport work; no router, hardware, RF, module load or readiness claim is
involved.

## Radar state and real children

The common radar callback takes its actual enable byte from shared state. Its
5GHz guard precedes the AC child. The scheduler requires a coherent known
`radar_enabled` only when that child is reached; any nonzero byte enables it.
The helper requires the actual radar state binding and selects one of eight
runtime threshold pairs by channel below71/at least71 and bandwidth
1000/1800/2000/other. It commits state170 and state172 as separate software
stores before hardware work, including when radar is disabled. No thresholds
or board defaults are supplied by this implementation.

Enabled initialization emits the actual eight initial packed PHY writes,
including the bandwidth/channel-specific25f value, then composes the real
SHM16 writer. Only after that child returns does it copy runtime state1b0 to
PI47c and execute the next three packed writes. Disabled initialization writes
25c=0 and skips that enabled-only SHM/state work. Both branches execute the
three common tail writes and five bandwidth-specific writes in recovered
order; the80MHz/other order differs from20/40MHz. A packed write uses the
existing actual counter/threshold and ST/W32 protocol, without an invented
MAC suspend or PHY guard.

The tail is the actual literal `MHF(index0, mask2000, value=enabled?2000:0,
band0)` call, including its wrappers. The recovered call supplies band0;
the selected four-byte control-flow table entry confirms its current-band
case. This implementation does not generalize it to other MHF indexes/bands.
A known-null current band returns. A nonnull band requires its real cache
binding and current MHF0; the cache halfword is stored unconditionally before
the clock/changed-word guard. If changed and clocks are nonzero, the actual
runtime descriptor offset feeds the primary SHM writer with selector10000.
Only after its return is the full extended-enable byte used; nonzero emits
the second real writer with selector110000. Those diagnostic readbacks are
mandatory supplied observations, not readiness tests or write echoes.

## HWOBSS and canonical partial state

The enabled HWOBSS branch performs seven actual packed writes and the real
PHY16c modify; the disabled branch performs four packed writes and the real
PHY35a modify. After those children, it requires the actual runtime SHM offset,
then performs a fresh PHY13a read and the real SHM16 writer. It does not reuse
the earlier13a write as a read result. Missing SHM-offset metadata therefore
stops after the preceding PHY effects and before the fresh read.

The helper owns a copied coherent snapshot, with `hardware` canonical for
PHY/PSM state and `object` an explicit owned core/minor binding. Scheduler
dispatch binds that hardware from `table.hardware`; every child publishes
partial canonical state before user callbacks and after each step. Runtime
facts, pointers, descriptors, thresholds and resource lifetime remain caller
obligations. Known required nulls fail, unknown reached facts stop explicitly,
and unused facts do not block unrelated branches. Begin supports aliases.

Every step invokes at most one synchronous callback. A successful cancelling
callback retains its actual observation/effect, including pre-I/O packed
counter changes and successful register bookkeeping. Later software stores,
helper returns, registry advancement and slice-up remain separate steps.
Callback error wins simultaneous cancellation; terminals are silent, with no
cleanup, retries, rollback or arbitrary successful callback substitutions.

## Evidence and validation

[Selected evidence metadata](../findings/PHY_CHANNEL_INIT_EVIDENCE.json) records
the pinned module identity and actual assembly/pseudocode/control-flow IDs.
The new bounded native session reused earlier registration and parent records,
was exported and closed, and left all raw output private. No vendor array,
calibration payload, board identifier or local host path is published here.

[Independent literal C tests](../tests/test_phy_channel_init.c) cover all eight
threshold selections with radar on/off, the channel71 boundary and the
bandwidth-specific order/value changes. The representative enabled/disabled
radar traces contain56/30 callbacks including both MHF writes. HWOBSS traces
contain30/24 callbacks with distinct fresh read samples. All420 failure,
cancellation and coincident-error boundaries across those four traces pass,
alongside reached/unused bindings, separate threshold stores, primary-return
before extended binding, canonical counter timing, aliases and terminal
silence. The scheduler test adds a real18-callback disabled-radar child,
continuation into the actual BTCX software callback and slice-up, and all54
callback exit variants with pre-callback canonical snapshots. Strict C11
ASan/UBSan tests and freestanding ARMv7 compilation pass. Eight private
semantic mutants are rejected. These helpers have not been compared against
executed stock ARM instructions; compilation and literal C traces are the
validation boundary.

## Remaining required PHY graph

Priority12 still reaches the actual noise-mode child after its existing shared
mode store. For PHY51, mode bit0 clear first requires real desense reset/apply
and RXGCRS calibration. The following actual sequence requires HWACI enable,
preemption/postfilter, HWOBSS, ELNA bypass and MCLIP. HWOBSS is closed here;
the enclosing noise body is not. MCLIP includes fresh per-core width8 table
reads/comparison and conditional writes; the existing table29/width32 reader
cannot be substituted. Actual registered noise sampling/calibration, PHY
state, radio identity and per-core data must remain explicit bindings.

Priority13's AC chanspec initializer clears a channel-state byte, sets the AC
in-progress bit and conditionally sets the radar first indicator before the
actual chanspec dispatcher. That dispatcher calls the real radio-chanspec
state helper, then its registered ordered channel module list. Later work
includes radio/PHY setup and tuning, noise/RXGCRS, TX/RX path, firmware enable,
cleanup and coexistence, followed by applicable low-rate ADC/20704 AFE and
idle-TSSI setup/measurement. Those bodies and their private/runtime inputs
remain required. Recovering a dispatcher or preserving an early software
store does not complete those descendants.

Offline completion requires source closure, real children and independent
traces for each reached body. Board binding requires actual radio ID and
subrevision, OTP/NVRAM/calibration inputs, coherent object graphs and verified
private spans. Physical timing/calibration, DMA/IRQ/OS adapters and Linux WLAN
integration still require separate implementation and eventual exact-board
validation. Radar trace completion does not establish legal RF operation,
channel availability, calibrated power or a working replacement driver.
