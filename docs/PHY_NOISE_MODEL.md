# Actual PHY51 noise initialization

The original [noise coordinator](../driver/phy_noise.c) now executes the real
priority12 noise child for supplied mode1 and0x89abcdef scenarios. The complete
sixteen-callback synthetic5GHz vector consequently reaches eleven completed
callbacks and stops at priority13, `phy_ac_chanmgr_init_chanspec`. Its independent
28,039-event queue is consumed. The [verification loop](VERIFICATION_LOOP.md)
returns exit2, incomplete; the next required child is a functional gate, not a
passing regression assertion. This does not complete PHY initialization or the
driver, and establishes no physical radio behavior.

## Actual child order and bindings

The scheduler preserves the common callback's selected-mode shared store, then
calls the registered AC mode child with its actual reset argument0. After that
child returns, a separate software step stores the selected mode to PI378,
before the callback can return or the registry advances. The optional reset
argument's PI3c8/PI3cc stores are outside this initialization entry.

For PHY51, mode bit0 clear first reaches the still-unimplemented real desense
reset/apply child, followed by conditional RXGCRS sampling/calibration. That
branch stops explicitly before those effects. Mode bit0 set skips those actual
calls and executes the following source-backed sequence:

1. HWACI off or the real28nm enable body, selected by mode bit1. Off performs
   three actual PHY modifications. Enable executes53 actual scalar PHY
   operations and five following wrapper modifications, including the1092
   pulse. Four values depend on2GHz/5GHz. When the actual AC ADC byte equals1,
   the body first reads the actual SHM16 field, ORs8 and writes it through the
   real object writer. Other byte values skip that operation. Both paths then
   commit six individual noise-state bytes, using actual2cd/2ce flags and
   separately bound2GHz/5GHz state pointers.
2. Preemption, selected by mode bit3. It first stores the actual data byte10
   and halfworde separately. Disabled preemption performs two real PHY
   modifications. Enabled preemption executes its packed writes, per-core
   modification and fresh19e guard handling. Both execute the actual postfilter
   child, then a fresh PHY1040 read and a later halfworde store. The enabled
   postfilter has eight common packed writes, seven PHY51 packed writes and
   the actual47dc/SHM tail; the disabled path still executes its tail.
3. [Actual HWOBSS](PHY_CHANNEL_INIT_MODEL.md), enabled only by the recovered
   bandwidth predicate together with mode bit6. The20MHz path disables it even
   when that mode bit is set; the40MHz fixture reaches its real enabled body.
4. ELNA bypass, selected by mode bit7, with actual radio-ID and chanspec guards.
   ID50e0 composes the real per-core radio modifications, preserving the
   core0/core1/ffff selector cases and reached bus-counter behavior. Another
   supplied ID follows the actual no-write branch; it is never inferred from
   the BCM63178 name.
5. MCLIP, selected by mode bit8. It reads physical maincontrol/SY, performs real
   PSM suspend and PHY-register nesting when active, saves/sets19e and reads six
   fresh width8 table bytes per core. It compares those bytes with the actual
   local zero/6464 construction and calls the real writer only when changed.
   It then performs the source's unconditional19e restoration, real nesting
   exit and PSM enable. No invented cache/DMA operation or cleanup is added.

The postfilter always accesses the current-channel binding, including when
disabled. A known-null pointer reaches the real desense channel lookup/update
child, which remains an explicit dependency; unknown is separate from null.
The current channel's byte1c changes an unused local subgroup on this selected
PHY51 branch, so no fabricated value for that byte is supplied. The real
time-bearing lookup/update path must still be implemented before its null
branch can complete.

All pointers, selected modes, radio identity/subrevision, state flags, SHM
offsets and resource ownership are caller-known coherent inputs. The main
`hardware` snapshot is canonical; inactive child snapshots are rebound from it
at dispatch. Active children publish partial state before user callbacks and
after each step. The scheduler also propagates channel/radio semantic state.
No board defaults, private calibration data or arbitrary successful vendor
callback is provided. Explicit initialization helper commands accept only the
actual boolean0/1 call arguments; they do not claim broader standalone APIs.

## Verification and its limits

[The independent fixture](../tests/fixtures/phy_noise_trace.h) supplies literal
physical read replies and source-ordered expectations, never model-generated
output or implicit zeros. [Noise tests](../tests/test_phy_noise.c) exercise both
supplied modes with one/two cores, optional SHM,2GHz scalar variations,20/40MHz
HWOBSS selection, matched/changed MCLIP tables and actual active-MAC nesting.
They check callback errors, cancellation and simultaneous errors, canonical
pre-callback bookkeeping, earlier software stores and reached/unused facts.
The enclosing scheduler tests verify actual continuation and cancel between
child return and PI378 store. [Reader tests](../tests/test_phy_table_read.c)
independently check output-byte commitment and wrapper/guard order. Strict
ASan/UBSan tests and freestanding ARMv7 compilation pass. Nine private semantic
mutants are rejected, including omitted parent publication and the later
PI378 commit.

Selected actual ARM execution additionally recovered four HWACI28nm variants
and both wrapper branches, using explicit synthetic context and validated
relocations. Unknown calls, memory and uninitialized stack reads fail. This
captures exact child-call contracts and software-store order; real PHY/SHM
operations are separate known children in that reduction. It is not a claim
that the entire new noise coordinator was compared as a composed stock ARM
execution. Existing primitive instruction comparisons remain separate proof.
The bounded native session was exported and closed. All raw records, private
inputs and analysis output remain outside the public checkout; only selected
[portable evidence metadata](../findings/PHY_NOISE_EVIDENCE.json) is published.

## Remaining required gates

The full supplied vector first reaches chanspec initialization at priority13.
Its actual channel setup/tuning module list, applicable ADC/AFE work and
idle-TSSI setup/measurement remain required. The alternate mode-bit0-clear
desense/apply/calibration branch and null-current-channel lookup/time update
also remain genuine source gaps; a completed supplied mode does not close
them. Actual radio ID/subrevision, OTP/NVRAM/calibration acquisition and private
payload provenance remain board/runtime obligations.

The enclosing attach/reset/upload/start/BMAC path, complete RX/TX delivery,
kernel/resource/IRQ/coherency/lifecycle integration and eventual exact-board
validation remain separate functional gates. No hardware, RF, router access,
module loading or device operation was performed for this work.
