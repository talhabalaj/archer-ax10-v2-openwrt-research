# Current progress and continuation checkpoint

Updated 2026-10-23. This checkpoint advances the full16-stage synthetic PHY
probe from five to **eleven completed required callbacks**. New real bodies:
the registered [radio-on register table](docs/RADIO_REGTBL_MODEL.md), the PHY51
[noise mode child](docs/PHY_NOISE_MODEL.md) with a generic width8
[table reader](docs/PHY_TABLE_READ_MODEL.md), and
[status-pool RX fill](docs/DMA_RX_STATUS_FILL_MODEL.md) with its BMAC wrapper.
Callback13 now executes its real modules0-2 and
[completes modules0-2](docs/PHY_CHANSPEC_MODEL.md), including radio20704 PLL tuning and VCO
calibration, and stops at module3 `chanspec_setup_phy`.
These are offline components, not a working radio driver.
Read [ROADMAP.md](ROADMAP.md) for the full objective and acceptance criteria.

## Current reset checkpoint

[The enclosing reset](docs/CORE_RESET_MODEL.md) composes real WL clocks, actual
SI core-up/coreflags, per-slot DMA reset, suspended-FIFO wake policy, lower
reset, main/extended caches, target PHY reset and a freshly guarded PLL child.
[The lower wrapper](docs/CORE_RESET_LOW_MODEL.md) composes actual D11 AI reset,
generic auxiliary reset and primary/secondary SI selection, preserving the
auxiliary zero-map early return. [Main-control helpers](docs/MCTRL_MODEL.md)
preserve cache/mirror timing and the exact reset software fields.

SI selection now has an explicit secondary-wrapper entry; the existing primary
entry shares the same implementation. The clock cache accepts the stock's full
fast-clock byte. Optional PSM band bindings distinguish an unknown pointer from
a known null pointer at the actual wait guard. These are original offline
components, with no hardware backend or claimed working Wi-Fi driver.

## Functional verification loop

Run `python3 scripts/verify_driver.py --scope target`; see the complete
[loop and functional gates](docs/VERIFICATION_LOOP.md). The real probe preserves
all16 required PHY callbacks, uses explicit synthetic50e0/5GHz20MHz/channel36
inputs and consumes a joined independent28039-event oracle (the count derives
from original synthetic table fixtures, so it is not itself a progress metric).

The current observation is: **eleven callbacks completed (priorities1-12),
current priority13 having completed its real modules0-2, required module3
`chanspec_setup_phy` unresolved** (29680 trace events consumed). Completed
callbacks stay at eleven until callback13 returns after all fourteen modules.
The published predecessor stopped after five callbacks at the radio7 regtbl
tail. Because the fixture changed, the verifier reports
`fixture_changed_requires_review`. An independent review compared the regtbl,
noise, status-fill and BMAC wrapper bodies with retained assembly (no mismatch)
and found the new probe inputs to be explicit facts for newly reached stages;
reverting unrelated label/allocator moves gave the identical observation.
There is no final return or slice-up store. The command returns2 (incomplete),
not a functional pass merely because existing tests expect this boundary.
Unexpected operations, unused read queues, bad input identity, compiler/check
failures or changing sources invalidate the run. A genuine failed probe retains
its actual failure diagnostics even when it exits nonzero.

The checkpoint scope also runs the sanitizer suite, ARMv7 builds and explicit
private-input/Unicorn checks when supplied. The exact staged checkpoint passed
all78 executed checks: target build/run, the57-program sanitizer/private-comparison
suite,56 C ARMv7 objects plus the combined-header probe, the warning-free Linux
6.12 kernel module build (`--kernel-build`), and all16 pinned ARM
comparison programs. The new status-fill checker executes the real wrapper,
status fill, idle child and, with an optional pinned `--hnd` module, the actual
HND address converter for432 positive-pool cases (1603 total, zero stubs).
Without `--hnd` that positive-pool proof is recorded as `not_run`. The overall
result remains incomplete/exit2, preserving the real required-child dependency.

Integration fixes in this checkpoint: the five new components were missing from
the shared runner and header probe (link failure); ARM checkers used a wall-clock
emulation timeout, so host load could fail an unchanged program, and now stop on
their instruction-count bound only; the regtbl fixture's descriptors are now
arbitrary synthetic rows rather than the stock rev51 tuples, and the native9/8
row counts are enforced exactly. Reports, logs and build outputs go
to a fresh external directory; optional previous reports compare only coherent
same-fixture observations. A fixture change requires independent source review.
Four verifier regression tests reject false success, partial vectors, a2GHz
fixture bypassing the5GHz radar guard, malformed observations and lost nonzero
probe diagnostics. The focused radio fault suite retains its unchanged oracle,
now shared with the full-vector probe.

This is one necessary PHY scenario, not full enclosing initialization. BMAC
initialization, valid RX delivery, normal TX/completion, kernel integration and
physical exact-board operation remain separate unverified gates. Keep the full
working-driver/OpenWrt objective active; no device/RF operation is authorized
or performed by this tool.

## Resume here

**Next: implement required priority13 channel initialization, then the
remaining callbacks16-21, then BMAC/IRQ integration.** The
[scheduler](docs/PHY_SCHEDULER_MODEL.md) preserves all sixteen required stages.
`phy_ac_chanmgr_init_chanspec` performs software stores, the optional radar
indicator, then `wlc_phy_chanspec_set_acphy`: a chanspec store followed by a
fourteen-entry module table (setup, TPC, radio, PHY, common, noise, RXGCRS,
radio/PHY/TX/RX tuning, firmware enable, cleanup, BT coexistence), then idle-TSSI
measurement and PHY51 ADC/AFE/core-to-core branches. Modules0-2 are implemented
([model](docs/PHY_CHANSPEC_MODEL.md)) up to module2's radio50e0 child. The radio20704 5GHz body
runs (channel tune-table rows are a private caller-supplied span). Its HND fixed-point helpers match the pinned hnd.ko
instructions; `pll_tune` (double-entry checked, not instruction-executed) and VCO
calibration now run. Next is module3 `chanspec_setup_phy` and VCO calibration, then modules3-13 in execution order so the reported terminal child keeps advancing. Noise mode-bit0-clear desense and disabled-preempt branches
also remain. Actual board ID/subrevision stays unknown.

The [packet/status bodies](docs/RX_PROCESSING_MODEL.md) now compose the identified
[registered release and SI switch/restore](docs/OFFLOAD_STATUS_MODEL.md) when
actual known callback and argument bindings match. Unknown registrations stay
dependencies. [Normal RX fill](docs/DMA_RX_FILL_MODEL.md) executes actual OS
service boundaries, tracking/descriptor writes and ST/producer/out ordering;
split/segmented/status fill, native allocation/coherency and physical ownership
remain. [Radar/HWOBSS](docs/PHY_CHANNEL_INIT_MODEL.md) closes target stage10's
actual child but full noise/channel setup still requires its real descendants.

The [BMAC audit](docs/DRIVER_BMAC_INIT_AUDIT.md) establishes actual initialization
order: core-unit word150 gates download calls; microcode-skip and SMC-loaded
bytes differ. SMC support is true for core130. The earlier reset lifecycle caller,
conditional higher initialization, BMAC header/status merge, IRQ/deferred work,
normal TX/completion and Linux delivery still require implementation. Reuse the
[PHY](docs/DRIVER_PHY_INIT_AUDIT.md) and [RX](docs/DRIVER_RX_PATH_AUDIT.md) audits
without treating a leaf or dispatcher as the whole driver.

The actual radio ID and subrevision are not established by the hardware
inventory. Preserve explicit runtime OTP/NVRAM/private-span/resource bindings;
never choose a radio/calibration branch from the BCM63178 name alone. Implement
known operations and expose reached gaps. A completed trace is not physical
readiness, DMA ownership or a successful packet transfer.

[SI selection](docs/SI_SELECT_MODEL.md) now preserves actual map/cache/index
stores and typed OS requests. [HND selection/IRQ/clock composition](docs/SI_CLOCK_MODEL.md)
uses real selectors and the existing CC body, including post-disable saved-index
reload and independent restore guards. The [WL caller](docs/WL_CLOCK_MODEL.md)
chooses real D11 or HND children, then stores the fallback cache byte before its
inline reason0x10 update. That fallback performs NO PSM wake-override call.

Actual resource discovery, OS mapping/PCI/IRQ/MMIO adapters, synchronization,
policy/metadata acquisition, SB selection, MBSS/fatal recovery and optional
higher-init branches remain dependencies. IRQ callbacks report actual post-call
SI state; missing state is explicit, not assumed unchanged. Reuse retained
Evidence and independent trace/ARM harnesses instead of repeating analysis.

Current priority is driver work first. Offline analysis/simulation remains
unblocked by pending UART/recovery work. No device upload, radio initialization
or modern OpenWrt RAM boot has been validated. The existing `TRANSFERRED` state
means a simulated software trace ended, not that firmware ran or became ready.

## Status board

| Milestone | State | Evidence / next condition |
| --- | --- | --- |
| Target and reference inventory | Established for inspected inputs | [Hardware](findings/HARDWARE_INVENTORY.json), [binary](docs/DRIVER_BINARY_AUDIT.md), [GPL](docs/GPL_AUDIT.md). Reverify drift-prone device state before device work. |
| D0 offline foundation | Implemented and host-tested within stated limits | Four C components, pinned object inspector, synthetic/fault tests and private-input comparisons. |
| D1 reset/start/acknowledgement | Partial implementation, host-tested | AI-wrapper/startup, PSM control, DMA reset, D11/ChipCommon clock, table/all-core TSSI, target PHY reset and BF cleaner implemented. SI/WL clock and enclosing core-reset composition are implemented offline; higher initialization, runtime/OS resources and MBSS/fatal recovery remain. |
| D2 complete register/SHM/context contracts | Partial implementation, host-tested | Effective-init now lowers through real physical object writers; addend acquisition, complete register map and SHM ABI remain. |
| D3 PHY 51/radio/channel/calibration | Partial implementation, host-tested | Full16 init order, table/SMC, radio OFF/register, explicit50e0 switch-on with regtbl, radar/HWOBSS and noise mode child implemented; eleven callbacks complete. Priority13 channel set/tune, callbacks16-21, calibration and runtime board inputs remain. |
| D4 DMA/interrupt/RX | Partial offline implementation | Current descriptors, slot/bulk/status-list/packet bodies, normal and status-pool RX fill, BMAC fill wrapper and identified status-release child implemented; native OS/coherency, BMAC parsing/IRQ and kernel resources remain. |
| D5 TX/Linux WLAN integration | Transport layer started; descriptor-only TX leaf | [Kernel transport layer](docs/KERNEL_INTEGRATION.md): relaxed-MMIO/barrier backend, platform driver with hardware access off by default, pinned warning-free ARM32 module build. Not loaded anywhere. IRQ handling, DMA rings, firmware delivery, mac80211, normal TX/completion remain. |
| D6 HE/features/stability | Not implemented | Requires validated baseline driver and hardware tests. |
| B0 flash/boot investigation | Partial | 21 vendor-declared regions and NOR rootfs corroboration; physical geometry/authentication/calibration coverage unresolved. |
| B1 console/recovery | Pending hardware evidence | Exact-board pins/voltage, console/command policy and return to stock not demonstrated. |
| B2 modern OpenWrt target/RAM boot | Not implemented | Reference SoC code exists; AX10 handoff, memory, SMP and recovery require validation. |
| B3 Ethernet/radio platform resources | Source audit only | No modern-kernel board/resource implementation verified. |
| W1 independent 2.4 GHz evaluation | Candidate support identified | Existing b43/BCMA path has limitations; no board test performed. |
| FW1 persistent firmware/release | Not implemented | No flashable image or validated replacement Wi-Fi driver. |

## Implemented components and validation

| Component | Implemented contract | Important limit |
| --- | --- | --- |
| `driver/ucode_select.c` | Core 130 minor 0/1 plus slice boardflags bit 0 selects primary/extended names and pinned sizes. | No input authentication, upload permission or readiness. |
| `driver/init_table.c` | Strict 8-byte LE record validation and raw ordered replay, offsets below `0x1000`. | Omits runtime adjustment at `0x130` and object-memory translation. Not a hardware-write emulator. |
| `driver/ucode_upload.c` | Explicit fresh chunk zero; main then extended; selector/readback/barrier and LE word-write trace; one event per step. | No MMIO, reset/start, SHM publication or hardware acknowledgement. Nonzero chunk state rejected. |
| `driver/effective_init.c` | Runtime `0x130` adjustment, width truncation and bounded semantic object-copy events; own-addend alias preserved. | Semantic layer; `init_io.c` supplies actual object-helper lowering, runtime addend acquisition remains. |
| `driver/object_memory.c` / `init_io.c` | Actual object COPY/COPY32/SET32/WRITE16 ports, discarded selector readbacks, lane/stride rules and physical init-record completion. | Explicit source/owner/window bindings and stricter bounded lengths; no runtime map or SHM ABI discovery. |
| `driver/phy_table_write.c` | Actual AC table wrapper, widths8/16/32/42/48/60/64 and packed PHY writes, with real PSM/PHY guards. | Borrowed private/synthetic data and explicit hardware state; no physical table proof. |
| `driver/si_pmu_gpio.c` | Actual PMU vreg/GPIO corereg, bus0 map/cache and PCI/ordinary SI/IRQ paths. | Explicit catalog/global reservation and actual post-IRQ state; no OS adapter or voltage proof. |
| `driver/phy_init.c` | Complete registered target51 table-init sequence with actual PHY/table/PMU/GPIO children. | Caller-known OTP/private spans, other registered callbacks and radio/calibration remain. |
| `driver/smc.c` | Actual loaded-byte wrapper, reset0/pulse/table47-width48/reset1, software flags only after child return. | Pinned private image provenance/runtime graph required; no SMC execution or readiness proof. |
| `driver/dma_descriptor.c` | Current sixteen-byte codec/formatter, address-extension/parity rules and bounded ring/range arithmetic. | Pure values; stricter host bounds, no DMA mapping/cache/lifetime ownership. |
| `driver/dma_tx_post.c` | Actual descriptor-only posting, fresh memory parity reads, out/producer/availability commitment. | Raw input differs from formatter; copied mutation and explicit resources, no normal packet TX/completion. |
| `driver/dma_rx_take.c` | Actual status/cached-current/force gates, slot clear, consumer/availability stores and mode-dependent sentinel skip. | Returns a packet label, not parsed packet or DMA/cache/allocator ownership. |
| `driver/radio_control.c` | Real radio read/write/modify, target63178 OFF and recovered50e0 ON prefix/private records. | Every ON remains dependency at its first unrecovered child; no board identity or radio-ready claim. |
| `driver/radio_power.c` | Real22-op logen and explicit50e0 switch-on per-core/PLL101/RCAL101/RCCAL2x10000/calibration operations. | Registered ON still reaches mandatory regtbl tail; private spans/runtime radio ID required. |
| `driver/phy_channel_init.c` | Actual PHY51 radar-init/MHF0-band0 and HWOBSS, real packed/register/object children. | Runtime thresholds/descriptors, full noise/channel/calibration and physical validation remain. |
| `driver/dma_rx_fill.c` | Actual normal allocation/account/pull/trim/translation/tracking/descriptor path, real idle failure leaf and ST/producer/out order. | Actual OS/coherency required; split/segmented and status fill remain dependencies. |
| `driver/si_switch.c` / `offload_status.c` | Real HND switch/restore plus registered bitmap/cursor release; known callback composes into status pool return. | No runtime pool acquisition or native OS backend; unknown post-IRQ state survives restart. |
| `driver/phy_scheduler.c` | Actual capacity23 insertion/ties, full16 target constructor, ordered dispatch and real closed PHY/radio/table/PSM/SI children. | Full target stops at required radio ON; immutable registry narrows live reloads, later noise/channel and fatal/bandwidth branches remain. |
| `driver/dma_rx_list.c` / `dma_rx_status_list.c` | Actual closed bulk leaves, discarded descriptor reads, distinct NEXT widths and software commit order. | Explicit coherent serialized memory/resources; no unmap/cache call occurs in these current leaves. |
| `driver/rx_packets.c` / `rx_status.c` | Actual length polling, trim/fragment/drop paths, status processing and inlined pool return with real list children. | Required OS adapter and unknown pool bindings explicit; identified release composes real children. Retained labels/counts are not validated frames or Linux delivery. |
| `driver/ai_reset.c` | AI primary/optional-secondary wrapper trace, bounded busy/release loops and readback-dependent control. | Used by actual lower reset composition; trace completion can include stuck reset. |
| `driver/psm_start.c` | Cached main/extended controls, exact ACK loops/final reads, physical SHM16 FIFO verification. | Startup-suspended predicate only; explicit fail-stop policy, no hardware backend. |
| `driver/psm_control.c` | Wake, suspend/enable, generic override set/clear, actual wake-control action and current-mask policy integration. | Reached unknown policy/public state or MBSS/fatal recovery remains explicit; stricter fail-stop policy, no backend. |
| `driver/dma_reset.c` | Current TX/RX reset, direct/indirect selector, status masks, bounds and cache semantics. | No queue mapping, buffers, reclaim or IRQ; returned success need not be an observed disabled state. |
| `driver/clock_control.c` | D11 clock selection, PLL and owned secondary-core traces, exact polling/sample distinctions and observation freshness. | Low-level fallback remains a dependency here; the separate WL job composes it. No lock/readiness proof. |
| `driver/chipcommon_clock.c` | Signed-revision/capability/mode clock body, typed PCI crystal path and exact polling. | Operation body only; SI/IRQ and WL caller now exist as separate jobs. No OS/backend proof. |
| `driver/phy_register.c` | Direct read/modify, analog/reset pulse and one/two-core low10 TSSI snapshot/restore. | No outer MAC/PHY-wake/core-flags lifecycle; high reads and three/four-core TSSI expose table dependency. |
| `driver/phy_table.c` | Complete table29/count1/width32 wrapper, actual PHY/PSM children, packed writes and partial state. | Required MAC context/callouts propagate; outer nesting is a separate job, with no physical table/timing proof. |
| `driver/phy_tssi.c` | PHY51 callback for all four core counts, repeated table reads and masked/no-op restores. | Four-slot buffer validity is explicit; outer idle-TSSI is a separate implemented job, not implied by callback completion. |
| `driver/phy_clock.c` | AI core-flags, enable/disable, bandwidth/reset preparation and RSDB workaround. | Typed owned wrappers; no mapper/IRQ execution or clock-ready proof; bandwidth recording is separate post-pulse work. |
| `driver/phy_guard.c` | Byte-depth PHY enter/exit and real reason-2 override child; partial depth retained. | Rejects nesting wrap; requires explicit MAC/policy/ownership. |
| `driver/idle_tssi.c` | Main suspend → PHY enter → all-core callback → PHY exit → main enable, with absent-callback no-op. | Enclosing reset now composes this child; no invented failure unwind. |
| `driver/wake_policy.c` | Ordered globals, actual RRM/association/override and three 16-BSS passes; explicit availability/access provenance. | Pure immutable input bindings; no runtime acquisition or hardware action in this component. |
| `driver/phy_reset.c` | Target AC/band PHY51 sequence with real outer TSSI, clock/pulse/analog and optional BF children. | Stable explicit inputs; enclosing reset uses this child, with no device/backend proof. |
| `driver/bf_clean.c` | Real main/extended MAC, ordered primary-inclusive/secondary-exclusive scans and core130 no-SHM leaves. | Reached metadata/bindings explicit; strict 256-slot cap, no acquisition or ownership discovery. |
| `driver/si_select.c` | Bounded core-ID/unit or index selection, map/cache/store order, PCI window requests and actual zero-return behavior. | Borrowed runtime catalog and OS adapter required; SB remains dependency; stricter count32 cap. |
| `driver/si_clock.c` | Fast/ordinary CC selection, real body, post-disable SI snapshot/index reload, independent restore guard and token. | Typed backend requests only; reached unknown/invalid state and owners explicit, no physical clock/IRQ proof. |
| `driver/wl_clock.c` | Complete clock-select dispatch, real D11/HND children and fallback byte-before-inline-reason10 stores. | No PSM helper on fallback or operating-system backend. |
| `driver/core_reset.c` | Actual enclosing clock/DMA/FIFO/low-reset/cache/PHY/PLL ordering and partial state. | Copied runtime facts, strict 72-queue cap, no hardware-ready result or backend. |
| `driver/core_reset_low.c` / `core_reset_low_ai.c` | Actual lower wrapper and generic auxiliary AI reset with real SI selection and existing D11 reset child. | Explicit registered callback/resource bindings; SB remains dependency. |
| `driver/si_core.c` | Actual selected-wrapper core-up and coreflags samples, masks and barriers. | Actual mapping/catalog ownership and MMIO adapter remain caller obligations. |
| `driver/mctrl.c` | Exact main-cache update/mirror and reset fields, preserving both depths. | Software snapshots and recorded writes, no physical commitment proof. |
| `driver/status_poll.c` | Caller-defined status predicate and monotonic timestamps; bounded steps, timeout and cancellation. | Synthetic configurable infrastructure; no recovered device-ready register/mask. |
| `scripts/radio_firmware_inventory.py` | Exact hash, ELF object bounds/relocation checks, size companions and metadata inventory. | Payloads remain private; different/older modules rejected. |
| `tests/check_phy_arm.py` | Exact selected stock ARM instructions versus C under 264 synthetic contexts; strict read queues and access bounds. | Requires private pinned input/Unicorn; three primitives only, no physical timing or device state. |
| `scripts/test_driver.sh` | Public ASan/UBSan suite; optional private stock comparison. | Host validation only. |

Earlier baseline validation (retained and still checked where relevant):

- 1,404 raw initialization records from four private stock tables replay exactly.
- Eight selector cases match array names/sizes; five unsupported identities fail.
- Four stock microcode pairs: 104,440 words and 208,912 callback events match an
  independent expected transfer trace.
- Synthetic tests cover malformed inputs, bank/order/width behavior, 15 injected
  upload failure points, timeouts, clock errors and cancellation.
- Callback-triggered cancellation regression fixed: a successful callback must
  not overwrite cancellation, including the final word or matching status read.
  Callback/read error wins if error and cancellation coincide. APIs are
  single-threaded; callbacks may cancel but must not reenter begin/step.
- All four C components compile for freestanding ARMv7 with strict warnings.
- Reviewed publication passed secret scans and selected known-secret checks;
  no proprietary payloads, calibration values or raw REA output were published.

These are historical validation results for the named baseline. Re-run relevant
checks when code or inputs change; do not imply a new hardware test occurred.

## Latest implementation and retained evidence

- [Explicit50e0 switch-on and remaining registered tail](docs/RADIO_POWER_MODEL.md),
  [normal RX fill](docs/DMA_RX_FILL_MODEL.md),
  [registered status release/SI composition](docs/OFFLOAD_STATUS_MODEL.md) and
  [PHY51 radar/HWOBSS](docs/PHY_CHANNEL_INIT_MODEL.md) add real required children.
  Native OS services remain typed boundaries; no opaque vendor callback reports
  arbitrary success. All new REA sessions were exported and closed.

- [Recovered PHY registry and execution](docs/PHY_SCHEDULER_MODEL.md) establishes
  all sixteen stages and real closed callback bodies. The complete target vector
  retains required radio7/table8 order; smaller isolated vectors are tests only.
- [Radio control](docs/RADIO_CONTROL_MODEL.md) and
  [selected observations/private-span metadata](findings/RADIO_CONTROL_EVIDENCE.json)
  distinguish the mandatory scheduler on-slot from BMAC's actually null optional
  init-slot and the still-unimplemented BT/common switch wrapper.
- [Current bulk/status list contracts](docs/DMA_RX_LIST_MODEL.md),
  [higher packet/status processing](docs/RX_PROCESSING_MODEL.md) and
  [four-body assembly index](findings/RX_PROCESSING_EVIDENCE.json) close actual
  source operations without importing older GPL cache/unmap branches.

- [Object writers and physical initialization](docs/OBJECT_MEMORY_MODEL.md)
  and [selected assembly](findings/OBJECT_MEMORY_EVIDENCE.json) close the real
  ports/readbacks and record-return boundary. The own-addend restart regression
  fixes a semantic-layer alias that previously cleared its input before copying.
- [BMAC order/SMC](docs/DRIVER_BMAC_INIT_AUDIT.md) and
  [selected current evidence](findings/DRIVER_BMAC_INIT_EVIDENCE.json) distinguish
  core-unit, microcode skip and SMC-loaded state. The inventory now includes
  separate SMC metadata alongside the original eight microcode/four init objects.
- [PHY51 callback/registry audit](docs/DRIVER_PHY_INIT_AUDIT.md),
  [table-write protocol](docs/PHY_TABLE_WRITE_MODEL.md),
  [PMU/GPIO paths](docs/SI_PMU_GPIO_MODEL.md) and
  [private-span metadata](findings/PHY_INIT_PAYLOAD_INVENTORY.json) describe the
  actual composed callback and required runtime bindings. The inventory tool
  resolves its exact current relocations and exports only to a fresh external
  private directory when explicitly requested.
- [DMA formatter](docs/DMA_DESCRIPTOR_MODEL.md),
  [descriptor-only posting](docs/DMA_TX_POST_MODEL.md),
  [receive slot take](docs/DMA_RX_TAKE_MODEL.md) and
  [enclosing RX audit](docs/DRIVER_RX_PATH_AUDIT.md) keep software arithmetic,
  descriptor publication, packet labels, DMA ownership and delivery distinct.
- [Radio identity](docs/RADIO_IDENTITY_MODEL.md) implements the actual high-first
  two-sample query, registered AC context binding and distinct AC/common decode
  stores. The actual board identity remains unknown; implementing its query
  contract is not an observation of its radio model.

- [Resource ledger](docs/DRIVER_RESOURCE_AUDIT.md) and
  [40 selected Evidence IDs](findings/DRIVER_RESOURCE_EVIDENCE.json) establish
  actual clock/PLL/PHY-reset/TSSI, wake-policy and DMA-reset contracts.
- [PSM-control implementation/policies](docs/PSM_CONTROL_MODEL.md),
  [DMA-reset implementation/policies](docs/DMA_RESET_MODEL.md),
  [prior lifecycle ledger](docs/DRIVER_LIFECYCLE_AUDIT.md) and
  [earlier reset/start boundaries](docs/LIFECYCLE_MODEL.md).
- [Clock/PLL implementation and limits](docs/CLOCK_CONTROL_MODEL.md),
  [actual HND clock fallback/mapping](docs/DRIVER_CLOCK_FALLBACK_AUDIT.md),
  [PHY wake/workaround closure](docs/DRIVER_PHY_WAKE_ADDENDUM.md) and
  [26 new plus eight reused Evidence records](findings/DRIVER_RESOURCE_DEPENDENCY_EVIDENCE.json).
- [Existing reverse-engineering attempts](docs/EXISTING_REVERSE_ENGINEERING.md):
  Quarkslab methodology, recent core-42 AC driver/harness/logs, D11 emulation
  tools and concrete ARM/core-130 compatibility gaps. No external code imported.
- [Published DMA64 source map](docs/DMA64_SOURCE_AUDIT.md) pins seven source
  members/hashes. It is older-version corroboration, not current queue support.
- [Effective-init semantics](docs/EFFECTIVE_INIT_AUDIT.md),
  [context/source notes](docs/D2_CONTEXT_SOURCE_NOTES.md) and earlier table/upload
  comparisons remain available; do not discard the independent raw recorder.
- [PHY primitives and stock-ARM comparison](docs/PHY_REGISTER_MODEL.md),
  [ChipCommon body/resource limits](docs/CHIPCOMMON_CLOCK_MODEL.md),
  [actual AC table-29 read](docs/DRIVER_AC_TABLE_READ_AUDIT.md) and
  [16 new plus six reused Evidence records](findings/DRIVER_AC_TABLE_READ_EVIDENCE.json).
  Table ID/index use packed W32, with bus-dependent counter/flush behavior;
  high-half reads cannot be skipped merely because the outer helper returns low16.

- [Composed ARM comparisons](docs/PHY_COMPOSED_ARM_CHECKS.md) run actual
  selected stock functions against table/TSSI C, with narrowly checked branch
  and intra-function dispatch relocations; no external PSM stubs.
- [Composed table model](docs/PHY_TABLE_MODEL.md),
  [all-core TSSI callback](docs/PHY_TSSI_MODEL.md) and
  [PHY-clock/core-flags model](docs/PHY_CLOCK_MODEL.md) implement retained exact
  contracts with explicit parent/child state and failure boundaries.

- [PHY nesting and outer idle-TSSI](docs/PHY_GUARD_IDLE_MODEL.md) compose the
  actual MAC/PHY children. [Pure wake policy](docs/WAKE_POLICY_MODEL.md),
  [assembly-backed policy audit](docs/DRIVER_WAKE_POLICY_AUDIT.md) and
  [actual ARM comparison](docs/WAKE_POLICY_ARM_CHECKS.md) close the recovered
  predicate, while its reached runtime input bindings remain explicit.
- [Reset/beamforming audit](docs/DRIVER_PHY_RESET_BF_AUDIT.md) and
  [12 new / eight reused records](findings/DRIVER_PHY_RESET_BF_EVIDENCE.json)
  establish exact outer snapshot/reset/restore ordering and real cleaner scans.
  The [wake-policy summary](findings/DRIVER_WAKE_POLICY_EVIDENCE.json) retains
  seven new / four reused records. All selected IDs were checked against retained
  raw records; the new session was exported/closed with unchanged input hash.

Latest validation (host/build and isolated synthetic ARM execution only):

- All48 runtime programs pass `sh scripts/test_driver.sh` with ASan/UBSan.
  Its optional pinned-input raw/effective-init, selector/upload and full SMC
  comparisons also pass. All49 C objects and the combined50-header probe pass
  strict freestanding ARMv7 compilation. No kernel module is linked or loaded.
- [Radio power](docs/RADIO_POWER_MODEL.md): complete1157-event two-core switch
  literal trace,101 PLL/101 RCAL/2x10000 RCCAL exhaustion and actual calibration
  width/division edge cases pass. All3471 callback failure/cancel/both boundaries
  and partial scheduler calibration stores pass; eight compiling mutants fail.
  Actual logen/read/write/modify composition matches146 ARM cases, with22 real
  call/tail bindings and exact mode relocations. Full power loops retain literal
  source coverage; registered ON still stops at its mandatory regtbl tail.
- [Normal RX fill](docs/DMA_RX_FILL_MODEL.md): allocation/account/pull/trim/
  translation, bulk/slot tracking, fresh descriptor parity and ST/producer/out
  ordering pass independent literal/fault checks; twelve compiling mutants fail.
  The real closed idle failure leaf matches144 ARM cases,3024 instructions and
  four rejection probes. Whole-fill OS implementations are not success stubs.
- [Status release/SI](docs/OFFLOAD_STATUS_MODEL.md): real bitmap/cursor and HND
  switch/restore composition, registered pool/index-clear tail, every principal
  callback error/cancel/both and unknown post-IRQ restart pass. Eight compiling
  mutants fail. Actual bus0 release matches220 ARM cases,997976 instructions
  and three rejection probes, no external stubs. Bus1 remains literal/source
  coverage. A defensive pre-callback selector copy is not a separately proven
  stale-state fix: after-step publication already covered those prior effects.
- [PHY51 radar/HWOBSS](docs/PHY_CHANNEL_INIT_MODEL.md): all eight runtime threshold
  selections and both enable branches pass literal traces with actual children,
  420 helper and54 scheduler callback exit boundaries, and eight mutants. No
  stock ARM comparison is claimed for these bodies. Scheduler selected callback
  exits now total1389; the full vector still cannot bypass required radio7.
- Existing [composed RX ARM check](docs/RX_PROCESSING_ARM_CHECKS.md) was rerun after
  typed nested callback replies:304 cases,74457 instructions,48 actual continuation
  refills, four real internal call sites, seven rejection probes, zero stubs.
- Independent selected-file source/publication reviews passed. New REA sessions
  were exported/closed; exact retained IDs and immutable input hashes are checked.
  Public artifacts contain no firmware arrays, calibration/NVRAM, raw records,
  private host paths, credentials or device operations.

Retained validation from checkpoint `e65db6c` (current runtime programs above
were rerun; optional comparisons below are historical unless named above):

- All 43 runtime programs pass the shared ASan/UBSan suite, including all six
  new components. All 44 C objects and the combined 45-header probe pass strict
  freestanding ARMv7 compilation. Optional private raw/effective-init, selector,
  upload and full SMC comparisons also pass. No kernel module is linked/loaded.
- [Both actual list leaves](docs/DMA_RX_LIST_MODEL.md) match3264 packet-bulk and
  408 status-bulk cases,952552 actual instructions and six negative probes.
  Current source has no helper calls or relocations; descriptor reads remain
  mandatory observations. All callback exits and eight private mutants pass.
- [Composed RX ARM execution](docs/RX_PROCESSING_ARM_CHECKS.md) matches304 cases
  across four actual functions,74457 instructions and48 real continuation bulk
  refills. All four reviewed internal CALL sites execute actual children; zero
  external success stubs are supplied. Seven negative probes reject unknown
  calls/fields and missing/unused reads, including actual reached OS calls.
  Timeout/nonlinear/free/pool branches remain literal C/source coverage, not
  stock OS execution. Twelve compiling RX-processing mutants are rejected.
- Actual radio READ/WRITE/MODIFY matches200 ARM cases with two exact real
  MODIFY branch bindings. Target OFF and unfinished ON have independent C
  traces/fault checks; those power sequences are not emulated as stock ARM.
  Six private mutants are rejected. No actual board radio ID is inferred.
- A separate private-input comparison validates the published220-byte span
  metadata and matches all54 register pairs through the complete245-operation
  ON prefix with independent literal PHY/control operations and a synthetic
  read sample. Its final state remains the required logen/PLL dependency.
  This is C/private-data coverage, not stock power-sequence or hardware execution.
- Scheduler tests check capacity/ties/full16 construction, real cache/analog/SI
  prefix, real412-event table child, nested antdiv PSM and closed software
  leaves, with1335 callback exit boundaries and seven private mutants. A full
  target vector stops at required radio7; no partial vector proves full init.

Retained component-specific results from checkpoint `16853c6` follow. Their
runtime programs are included in the current shared rerun; earlier optional ARM
comparisons are historical unless explicitly rerun above.

- New independent literal tests cover object lane/stride/readback ordering,
  physical initialization completion, all AC table widths/guards, SMC load/skip
  branches, PMU/GPIO mapping/IRQ restoration, the412-event PHY51 callback and
  all64 fresh-PHY0/OTP voltage cases. Every selected callback error, cancellation
  and coincident error preserves actual partial state. Private semantic mutants
  are rejected; no always-success external operation supplies the chain.
- Descriptor/TX/RX tests check raw words, current address/parity arithmetic,
  reserved-slot capacity, fresh parity reads, source-ordered software stores,
  forced/cached receive status and sentinel chains, including every callback exit.
- The full private SMC comparison matches1903 rows,5709 data halfwords and11460
  operations. Its two trailing source bytes are intentionally ignored, as in
  stock; changing only them leaves the trace unchanged. Pinned-source digest
  mismatches are rejected. This uses synthetic explicit read replies, not RF.
- Added strict actual-ARM comparisons match864 descriptor-format cases,
  456 cases through four real object writers, and1408 descriptor-post cases
  including240 full-ring returns, plus2304 receive-slot cases including864 empty
  rings. They reject unknown calls/relocations/memory
  and missing or unused reads; the object COPY calls bind the real WRITE16
  function rather than a success stub. Receive comparisons execute379379 actual
  ARM instructions and match per-callback cursor/availability/cache snapshots.
- All 38 portable C objects and the combined 39-header probe pass strict
  freestanding `armv7-none-eabi` compilation. These builds do not link a Linux
  module or execute code on the router.
- All 37 synthetic runtime programs pass the shared ASan/UBSan runner, including
  the added radio-identity package. Optional private comparisons also pass for
  1404 raw init records,4212 effective records, eight selector cases,104440 upload
  words and the complete SMC trace. Both public object inventories exactly
  reproduce the pinned module's current metadata.
- The radio-identity ARM check additionally matches68 actual query traces and
  264 distinct AC/common decode cases. Its registered-thunk relocation is
  excluded from emulator scope and covered by literal C binding tests. No
  actual board identity is inferred from these synthetic inputs.


- New independent reset tests cover source-ordered callbacks and software stores,
  original full fast byte, paired/null DMA rules, RX eligibility, real FIFO-clear
  policy, lower reset flags/auxiliary selection, two fresh version samples,
  no-delay AC flags, exact cache clearing, real target PHY reset and fresh PLL.
  Callback failure/cancellation/coincident errors preserve partial contexts;
  terminal calls are silent and each step enters at most one callback.
- Ten private coordinator mutants, six mctrl mutants and three optional-band
  binding mutants are rejected. The actual SI helper has separate literal and
  gate tests. These are synthetic trace checks; no whole-reset ARM execution,
  physical reset/readiness, calibration or radio behavior is proved.

- At reset checkpoint `788eee4`, the three retained Unicorn2.1.4 harnesses were rerun
  successfully: 264 primitive cases, 512 TSSI and 270 table/high-selector cases
  plus16 packed fixtures, and373 wake-policy decisions plus139 missing prefixes.
  This does not execute the enclosing core-reset chain as ARM instructions.
- At reset checkpoint `788eee4`, all twenty-seven synthetic test programs passed
  under ASan/UBSan; all twenty-eight portable C
  components compile for freestanding ARMv7 with strict warnings.
- The combined-header probe passes strict host and ARMv7 compilation, covering
  all29 then-current public headers and preserved AI event/phase/error numbers. The
  AI namespace migration is a mechanically verified rename; all runtime tests
  and optional private stock comparisons pass after it.
- SI selection covers literal map/PCI tuples, 64 cache/physical-presence cases,
  all eight zero-MAP result patterns, special core mapping size, duplicate IDs,
  actual bus/interconnect paths, aliases and every callback/internal boundary.
- HND wrapper tests cover signed early returns, fast original/derived-zero and
  nonzero-wrap paths, ordinary real selection/restoration, gate/mode skips,
  crystal PCI, actual null-CC dereference boundary and independent IRQ guards.
  IRQ tokens/post-state survive successful cancellation; errors leave SI state
  unverified. Eight private boundary/state/resource/guard mutants are rejected.
- WL clock tests cover both real branches and fallback cache-before-reason
  software commits, every selected callback error/cancel/both, unknown IRQ state,
  real selector/PCI effects and aliased restart. Direct D11 selection preserves
  wake reasons; fallback updates bit0x10 inline without mctrl/wake-policy calls.
- Target reset tests check independent region-tagged traces, snapshot before
  preparation, public/band revision distinction, late bandwidth store, absent
  callbacks and ordered BF gates. Per-event fault/cancel/both checks preserve
  child software state and pending dependencies without inventing unwind.
  Present idle-callback context/PHY binding is checked AFTER MAC suspend and
  PHY enter; missing late inputs preserve those earlier effects. Analog context
  must also bind its actual first-word PHY to this reset's resource.
  Counts1..4, active MAC and RSDB paths pass; six private order/gate/commit/
  context/policy mutants are rejected by the independent tests.
- BF tests check real main/extended MAC order, inclusive primary zero-bound
  iteration, exclusive secondary zero-count, bitmap/pointer/cubby short circuits,
  actual leaf return gates, reached metadata and binding/bounds failures.
  The explicit 256-slot cap rejects wrapping/oversized primary bounds at their
  reached post-entry load; it is a stricter model policy than stock.
- Generic override and actual wake-control tests cover reason zero, already
  absent clears, current post-clear masks, original physical sample versus cache,
  public-down/no-I/O and known/unknown/invalid reached policy. Actual input
  bindings take precedence over a separately supplied Boolean decision.
- Guard/outer idle-TSSI tests cover every core count, active/inactive MAC,
  optional callback absence, nested/overflow/underflow, partial saved state,
  semantic child failures, alias-safe restart and every-event error/cancel/both.
- Mirror-cache timing is corrected in startup/control: the mirror software
  store follows a successful primary write. Primary error/cancel retains the old
  mirror cache; mirror-barrier failure retains its already updated cache. Prefix
  regressions and private premature-update mutants cover both components.
- Actual ARM pure-policy comparisons match 373 decisions and complete ordered
  context-load traces, plus 139 missing-field prefixes. The longest coherent case
  performs 218 loads. Five actual functions run with four narrowly resolved
  branches; three unapproved call/relocation and eight null-pointer probes reject.
  These do not execute the MMIO action or runtime acquisition.
- Table traces cover 40 bus/threshold/guard combinations, packed32 header words,
  index truncation, pre-I/O counters/flushes, full data halves and valid-zero
  words. Real PSM suspend/enable/dependency/failure paths preserve per-step MAC
  context; every selected event covers error/cancellation/coincident errors.
- Four-slot TSSI callback tests cover all core counts, repeated tableffff reads,
  no-op extra-slot restores, fresh lower-slot masking, validity/partial buffer,
  table-word-versus-complete-wrapper commitment and parent/child cancellation.
  Three private mutants (early table commitment, late direct commitment and
  wrong repeated table index) fail the independent checks.
- PHY-clock traces cover enable/disable, reset bandwidth flags and separate
  post-pulse bandwidth recording, actual unit1 workaround, software state timing,
  first/final read distinction, owner dependencies, every-event exits and aliases.
- PHY literal fixtures cover read/modify, analog/reset pulse, one/two-core TSSI
  slot mapping, mask/truncation, original-A reuse and per-event bookkeeping /
  partial snapshot validity. Every-event errors/cancellation/coincident errors,
  alias-safe snapshot-to-restore, invalid inputs and table dependencies pass.
- ChipCommon literals cover signed revisions, capability/mode gates, three
  register generations, crystal PCI and ignored-return paths, both 1,500-delay
  poll directions, ownership dependencies before/after writes, observation
  freshness, alias-safe restart and callback exit boundaries.
- Composed ARM checks match 512 all-count TSSI and 270 table/high-selector
  C cases, plus 16 packed-write literal fixtures. Seven unapproved call/dispatch
  probes reject. The ten actual selected functions return through complete
  inactive-MAC wrappers; active PSM paths remain C-tested, not ARM-emulated.
- Actual pinned ARM instructions for three PHY primitives match C across 264
  deterministic synthetic boundary/random cases using Unicorn 2.1.4. Six C
  removal/width/mask/bookkeeping mutants and eight original ARM rejection probes
  fail as intended. No kernel module, radio microcode or physical MMIO executed.
- Clock/PLL literal traces cover all three commands, separate A/B samples,
  no-I/O guards, request-already-set shortcut, owned secondary directions and
  no-owner dependencies. Independent boundary expectations cover 1,500/no-final
  and 10,000/mandatory-final polling, partial masks, final-only ready and final
  loss. Per-success telemetry, every-event errors/cancellation/coincident errors,
  aliased context restart and terminal silence pass.
- PSM-control literal traces cover all five commands, forced-wake/mirror and
  cached/no-band paths, 40-us wake plus rounded u16 budget through 65,535,
  suspend's 8,300-delay limit/final confirmation, main enable's 1,001 reads/
  1,000 delays, separate nesting and all-ones checks. Every callback in selected
  normal/delayed traces covers errors/cancellation/coincident errors.
- DMA-reset traces cover zero-count/early-disabled returns, forced selector and
  low-byte cache, direct/indirect paths, separate 1,000-delay waits, suspend
  exhaustion followed by disable, TX-only 300-us timeout delay, return value
  versus actual observed disabled state, and every-event errors/cancellation.
- A real chaining bug was fixed: begin from `&job.context` previously cleared
  its own input before copying. Control, startup and AI-reset now snapshot first;
  chain/restart regressions pass and private removal mutants fail at those cases.
- The optional private-input suite still matches 1,404 raw records, 4,212
  effective-record cases under three synthetic addends, eight selector cases/
  five unsupported identities and 104,440 upload words across four stock pairs.
- No physical MMIO, device-firmware execution, calibration access, RF operation,
  Linux module linking or router test occurred. Private bytes remain outside this checkout.

The AC table-reader session was exported and closed with unchanged input digest;
its 16 new Evidence IDs and six reused records were verified against retained
raw records. Only reviewed descriptions/metadata are public.

The [clock wrapper re-review](docs/DRIVER_CLOCK_WRAPPER_REVIEW.md) reuses five
actual selected assembly records from closed HND/WL sessions. All five
[Evidence IDs](findings/DRIVER_CLOCK_WRAPPER_REVIEW_EVIDENCE.json) were checked
against retained raw records and both module hashes reverified; no new query
or router execution was needed. It closes post-disable index reload, independent
restore token0, generic/fast zero differences and inline WL reason0x10 updates.

The new [exact PHY-reset boundary review](docs/PHY_RESET_EXACT_PATH.md) closes
the actual analog dispatcher and bandwidth-store assembly. Its two new
[Evidence IDs](findings/DRIVER_PHY_RESET_EXACT_EVIDENCE.json) were verified
against retained raw records; five prior records are reused. The bounded session
was exported/closed and the module hash rechecked. This corrects the common
post-pulse delay, required late bandwidth PHY dereference and the analog
callback's context-to-PHY ownership. No VASIP/SMC call occurs in this reset body;
their higher initialization prerequisites remain separate.

Two earlier exact-HND/WL dependency sessions were exported and closed. Their
26 new Evidence IDs and eight reused records were checked against retained raw
records. Runtime mapping/backend, policy input acquisition and optional branches
remain open; body models do not imply surrounding callouts were implemented.

The earlier exact-WL resource session was exported and closed with unchanged input
SHA-256. Retained raw records live privately under `driver-private/rea-resources`;
older private directories are `rea-lifecycle`, `rea-hnd-reset` and `rea-fifo-shm`.
The public reports/JSON contain original descriptions and selected metadata, not
raw decompilation or payloads. Current `hnd.ko`: 366,060 bytes, SHA-256
`d05ff061b3a636630955a13fdd20a953f3dfb07ee92904a987dd4c77b648b45a`.
The private kernel-boundary proposal remains under
`board-audit/KERNEL_BOUNDARY_AND_INIT_TEST_NOTES.md`, as design input only.

Critical distinctions to preserve:

- Main enable ends `DEPENDENCY` for reached unavailable wake state or MBSS work. Required
  callouts are recorded, never silently executed as successful stubs. Failed
  wake/enable/suspend traces stop before some stock diagnostics/cleanup; exact
  policy differences are documented. Depth wrapping is rejected.
- Wake, suspend, enable, clock and DMA loops have different read/count/final-read
  contracts. A generic elapsed-time poller does not reproduce them automatically.
- Clock/PLL stock routines are void and can finish after timeout; PLL may skip
  ready polling when request bits are already set. Software state or helper
  return is not physical readiness. The eventual coordinator needs postconditions.
- PHY reset invokes TSSI save/restore through suspend/enable/PHY-register nesting.
  MAC run-bit guards can make those calls no-ops during initial reset; do not
  invent an unconditional cycle requiring already-running microcode.
- AI reset can finish stuck. DMA empty/indirect-no-poll success need not observe
  disabled state. No trace state proves valid RF calibration, RX/TX or safe reclaim.
- RSDB, capability words, core maps, PHY core count, callback presence and queue
  flags/resources are runtime inputs. Do not infer false/zero from the chip name.
- Runtime `hw + 0x260` adjusts hardware register `0x130`; these are separate
  address spaces. Init-object byte addresses remain u32; low16 narrowing is value.

## Exact next work package

1. Read this checkpoint, [resource ledger](docs/DRIVER_RESOURCE_AUDIT.md), relevant
   implementation notes and Git status. Preserve unrelated work; check remote
   main before publication. Reuse closed-session Evidence instead of repeating
   installation/analysis.
2. Follow the [actual BMAC initialization groups](docs/DRIVER_BMAC_INIT_AUDIT.md)
   and the earlier lifecycle caller that invokes core reset. Reuse the real
   reset/upload/start/object/SMC/table-init and full16 scheduler components.
   Radio-on with regtbl, the noise mode child and callback13 module0 now
   execute, through module2. Implement the remaining
   `wlc_phy_chanspec_set_acphy` modules3-13, remaining
   noise desense branches, callbacks16-21 and reached other-radio dispatches.
   Implement actual common BT/switch and channel-manager operations; acquire
   observed radio ID/subrevision and reached board inputs. Keep conditional
   VASIP, BMC, coexistence, offload and diagnostic operations explicit; no
   coordinator may skip required children as success.
3. Integrate actual runtime catalog/resource discovery and OS mapping, PCI and
   IRQ adapters. Preserve typed spaces, map ownership/lifetime, current index
   restoration and explicit post-IRQ SI state. SB selection remains a dependency;
   immutable model bindings do not establish actual runtime values or ownership.

4. Implement reached policy/metadata acquisition, synchronization and MBSS/fatal
   recovery. BF and pure wake inputs bind immutable external facts, not live
   acquisition. Runtime RSDB, clocks, bus/threshold and queues need actual data.
5. Preserve exact PHY reset scope and ordering. Broader PHY-revision paths are
   unsupported, not no-ops. Analog null checks occur AFTER the chip63178 bandwidth
   store's required PHY dereference. Core130 delay100 is a common post-pulse join.

6. Finish D2 runtime addend acquisition and needed SHM ABI. For D4, reuse the
   actual bulk/status/packet/fill/release jobs; acquire native RX-fill
   allocation/coherency resources and runtime pool registration, implement the
   distinct status-fill body, and recover BMAC
   header/status/RSSI/frame processing. The current bulk leaves have no cache/
   unmap call; do not import older optional source paths. Recover the real
   MAC ISR/deferred-work acknowledgement/masks. Descriptor formatting, descriptor-
   only posting and slot take do not establish normal packet TX/RX. Continue
   Linux integration and full features. Separate recovery/RAM-boot/board work remains
   required before exact-board hardware execution and persistent firmware.
7. Extend the original strict differential harness and common trace adapters:
   independent read queues, bank/core/space/width/barriers/delays, no implicit
   zero/write-echo, missing/exhausted/unconsumed read rejection and consumed-bit
   checks. ARM execution covers the pure wake predicate, primitives, object writers,
   DMA leaves, composed packet/status processing and selected table/TSSI
   inactive-MAC paths; extend to actual PSM/action/clock only with proven caller
   closure and strict contexts, not successful external stubs. Optional live ARM
   accessor coverage design does not replace implementation.
8. Add independent traces/boundaries/failure/cancel regressions, run affected
   ASan/UBSan/private/ARM checks. Reuse/export/close REA sessions, verify exact
   input identity and maintain this checkpoint with scope and remaining gaps.
9. Review/scan an exact public allowlist, commit/push and verify remote commit/
   tree. Keep proprietary bytes, calibration, credentials and local paths private.

**D1–D5 remain incomplete.** The named reset, initialization and DMA components
are implemented offline; actual OS/runtime resources, full initialization,
radio/channel/calibration, policy acquisition/MBSS/fatal recovery, packet data
path and Linux integration remain unfinished. Maintain the full
working-driver and modern-OpenWrt objective; host tests do not meet hardware gates.

## Input and tool identities

| Input/tool | Pinned identity / location of public reference |
| --- | --- |
| Current stock `wl.ko` | SHA-256 `8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c`, 5,803,056 bytes. Private caller-supplied input. |
| Driver and microcode version | Stock 17.10.121.37; live ucode 1570.159 as recorded in the hardware inventory. |
| Official GPL archive | SHA-256 `9670b07a1449dc7e338db9becbb019d154f0486569c1621a40f759843f655a87`; source URL and version distinction in [GPL_AUDIT.md](docs/GPL_AUDIT.md). |
| Stock upgrade image | SHA-256 `e3c78e25a23d661219b1934d3b94ba7d0844cd357fede8a830b90e60fd0a910c`; [boot audit](docs/BOOT_CHAIN_AUDIT.md). |
| Native analysis | Recorded working REA 6.5.0 / Ghidra 12.1.4 / Node 26.11.1 / JDK 21; [setup notes](docs/REA_SETUP.md). No global registration change required by the private stdio workflow. |
| Upstream source baseline | Exact inspected revisions and negative-search boundaries in [BOARD_SUPPORT_AUDIT.md](docs/BOARD_SUPPORT_AUDIT.md) and [DRIVER_BASELINE_AUDIT.md](docs/DRIVER_BASELINE_AUDIT.md). |

Private raw analysis, exported Evidence bundles, firmware/driver objects, vendor
archives, device observations and tool installations live outside this checkout.
They are not recoverable from the public summaries alone. Reuse an available
authorized private input; if it is absent, request the specific input or use its
official source. Never replace the pinned current module with an older archive
object without making that a separately identified analysis target.

## Run and inspect

From this repository:

```sh
git status --short
sh scripts/test_driver.sh
# Optional, using a separately supplied private input:
sh scripts/test_driver.sh /private/path/to/wl.ko
# Optional isolated ARM comparisons, using tests/requirements-emulation.txt:
python3 tests/check_phy_arm.py /private/path/to/wl.ko
python3 tests/check_phy_composed_arm.py /private/path/to/wl.ko
python3 tests/check_wake_policy_arm.py /private/path/to/wl.ko
python3 tests/check_object_memory_arm.py /private/path/to/wl.ko
python3 tests/check_dma_descriptor_arm.py /private/path/to/wl.ko
python3 tests/check_dma_tx_post_arm.py /private/path/to/wl.ko
python3 tests/check_dma_rx_take_arm.py /private/path/to/wl.ko
python3 tests/check_radio_identity_arm.py /private/path/to/wl.ko
python3 tests/check_radio_control_arm.py /private/path/to/wl.ko
python3 tests/check_dma_rx_list_arm.py /private/path/to/wl.ko
python3 tests/check_rx_processing_arm.py /private/path/to/wl.ko
```

For a new bounded named-function pass, configure the child process as described
in [REA_SETUP.md](docs/REA_SETUP.md), then use the existing wrapper:

```sh
python3 scripts/rea_native.py /private/path/to/wl.ko /private/new/lifecycle-analysis \
  --procedure wlc_bmac_enable_mac --procedure wlc_bmac_enable_macx
```

This returns private raw records, not a publication-ready report. Follow up with
assembly, typed references and descriptor reads as required by the evidence.

## Decisions and constraints to preserve

- Driver-first offline development is the active priority; do not restart the
  UART conversation as a prerequisite for every simulator change.
- Existing stock root/bootstrap/LuCI work is separate from a modern firmware
  port. No hardware driver or firmware image from this repository was loaded.
- b43's AC path is incomplete/broken for this target; adding PCI/core IDs does
  not create PHY 51 support. brcmfmac needs a different proven firmware protocol.
- Main and extended uploads share ports `0x160/0x164`, with selectors
  `0x03000000/0x03080000`. Selector readback is discarded. No final barrier is
  present in the recovered writers. See the exact audit before changing code.
- The wrapper's downloaded flag is software state, not hardware acknowledgement.
- Full chip geometry, active loader policy, calibration coverage, SMP handoff and
  recoverable RAM boot remain unresolved. Do not use legacy image addresses or
  an upstream reference board as a validated AX10 load/flash recipe.
- Maintain [the public/private boundary](docs/PUBLICATION_POLICY.md); no raw
  dumps, proprietary arrays, individual identifiers, credentials or local host
  paths in commits. Original implementation and reviewed metadata are public.

## Milestone history

| Commit | Result |
| --- | --- |
| `e6f4734` | Initial sanitized hardware/GPL/upstream/REA research publication. |
| `aecc211` | Boot chain, official image layout and RAM-boot constraints. |
| `71610f8` | Original selector, initialization decoder/replayer and object inventory. |
| `00e6fdb` | Exact upload trace simulator, generic status polling, fault/cancellation regressions and test runner. |
| `2a80787` | Effective initialization, current-HND AI-wrapper reset, PSM startup/FIFO trace and independent tests/evidence. |
| `ef8dfb0` | PSM/DMA control and clock/PLL models with exact evidence and external research. |
| `a50280e` | Direct PHY/ChipCommon models and actual ARM primitive comparisons. |
| `69f946b` | Composed table/all-core TSSI and PHY-clock jobs with actual ARM comparisons. |
| `86ab211` | PHY nesting, complete outer idle-TSSI, actual wake predicate/action and ARM load-trace comparisons; mirror-cache prefix fix and reset/beamforming evidence. |
| `af031a9` | Composed target PHY reset, real core130 BF cleaner and exact analog/bandwidth assembly closure. |
| `b9b2877` | SI selector, HND resource/IRQ/clock wrapper and complete WL clock-select composition with exact inline cache/reason stores. |
| `1deea16` | AI/PHY header namespace integration fix and combined-header compile gate. |
| `788eee4` | Enclosing/lower core reset, generic auxiliary AI reset, selected-wrapper SI helpers, exact mctrl reset and independent trace regressions. |
| `16853c6` | Actual object/init writes, mandatory SMC, PHY51 table initialization and PMU/GPIO, radio-identity query/decode, current DMA formatter/post/take, strict stock ARM comparisons and retained caller audits. |
| This checkpoint | Full16 PHY registry and actual closed children, target radio OFF/register operations and unfinished ON prefix, current bulk/status list leaves and higher packet/status processing with strict real-child ARM comparisons. |

At each future checkpoint, add a concise milestone entry and replace the active
next-work package with the actual next task. Keep unresolved defects visible.
