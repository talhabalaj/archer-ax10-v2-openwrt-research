# Current progress and continuation checkpoint

Updated 2026-10-15. This checkpoint adds physical object-memory initialization,
the mandatory core130 SMC path, the complete registered PHY51 table-init
callback, and current DMA descriptor/TX-post/RX-take components. It builds on
the enclosing/lower core-reset checkpoint `788eee4` and the earlier baseline
`1deea16`. These remain offline implementations, not a working radio driver.
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

## Resume here

**Next: finish the remaining radio/channel/calibration callbacks and receive
list/ownership path, using the recovered enclosing call graphs.** The
[BMAC audit](docs/DRIVER_BMAC_INIT_AUDIT.md) establishes actual initialization
order: core-unit word150 gates download calls; the separate microcode-skip and
SMC-loaded bytes have different meanings. SMC support is true for core130 and
cannot be skipped as an unsupported optional feature. `wlc_bmac_init` itself
does not call core-reset; recover its earlier lifecycle caller before composing
them. The [PHY audit](docs/DRIVER_PHY_INIT_AUDIT.md) distinguishes the now
implemented registered table-init callback from the remaining registry/radio
and calibration work. The [RX audit](docs/DRIVER_RX_PATH_AUDIT.md) distinguishes
slot removal from higher `dma_rx`, RX fill and Linux delivery.

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
| D3 PHY 51/radio/channel/calibration | Partial implementation, host-tested | Complete registered PHY51 table-init, generic AC table writer, SI PMU/GPIO and mandatory SMC path implemented; radio/channel/calibration, runtime board inputs and full registry remain. |
| D4 DMA/interrupt/RX | Partial offline implementation | Current descriptor formatting/ring arithmetic and actual RX slot-take leaf implemented; allocation/cache/list parsing, RX fill, IRQ and kernel resources remain. |
| D5 TX/Linux WLAN integration | Descriptor-only TX leaf implemented offline | Actual descriptor posting is implemented; normal packet TX/completion and Linux WLAN architecture/integration remain. |
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
   reset/upload/start/object/SMC/table-init components. Finish the PHY init
   registry ordering and remaining actual radio switch/on, channel-manager and
   calibration children; acquire the observed radio ID/subrevision and reached
   board inputs. Keep conditional VASIP, BMC, coexistence, offload and diagnostic
   operations explicit; do not publish a coordinator that skips them as success.
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

6. Finish D2 runtime addend acquisition and needed SHM ABI. For D4, follow
   `_dma_getnext_rxlist` and its actual cache/unmap/list ownership, then implement
   `dma_rx` bounded lengths/continuations/errors and RX fill. Recover the real
   MAC ISR/deferred-work acknowledgement/masks. Descriptor formatting, descriptor-
   only posting and slot take do not establish normal packet TX/RX. Continue
   Linux integration and full features. Separate recovery/RAM-boot/board work remains
   required before exact-board hardware execution and persistent firmware.
7. Extend the original strict differential harness and common trace adapters:
   independent read queues, bank/core/space/width/barriers/delays, no implicit
   zero/write-echo, missing/exhausted/unconsumed read rejection and consumed-bit
   checks. ARM execution covers the pure wake predicate, primitives, object writers,
   DMA leaves and selected composed table/TSSI inactive-MAC paths; extend to actual PSM/action/clock only with proven caller
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
| This checkpoint | Actual object/init writes, mandatory SMC, PHY51 table initialization and PMU/GPIO, radio-identity query/decode, current DMA formatter/post/take, strict stock ARM comparisons and retained caller audits. |

At each future checkpoint, add a concise milestone entry and replace the active
next-work package with the actual next task. Keep unresolved defects visible.
