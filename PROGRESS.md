# Current progress and continuation checkpoint

Updated 2026-10-15. This checkpoint adds composed table-29 reads, the complete
PHY-51 one-to-four-core TSSI callback and AI PHY-clock/reset preparation jobs.
Previous implementation:
[`a50280e`](https://github.com/talhabalaj/archer-ax10-v2-openwrt-research/commit/a50280efd9e26edbd8a44e33e94339ce700281f8).
Read [ROADMAP.md](ROADMAP.md) for the full objective and acceptance criteria.

## Resume here

**Next: implement outer PHY-register/wake nesting and compose higher reset/clock
lifecycle around the tested table/TSSI/PHY-clock jobs and explicit resources.**
Table reads now use real PHY and PSM children; their pending wake-policy/MBSS
callouts remain visible. The four-slot TSSI callback handles all actual PHY core
counts, including repeatedffff reads and no-op restores. The outer idle-TSSI
MAC/PHY-entry/wake wrapper is still required, not implied by callback completion.
SI selection/restore and WL cached-mode/wake updates still surround the separate
ChipCommon body. Reuse closed-session evidence and extend existing trace/ARM
harnesses instead of repeating analysis.

Current priority is driver work first. Offline analysis/simulation remains
unblocked by pending UART/recovery work. No device upload, radio initialization
or modern OpenWrt RAM boot has been validated. The existing `TRANSFERRED` state
means a simulated software trace ended, not that firmware ran or became ready.

## Status board

| Milestone | State | Evidence / next condition |
| --- | --- | --- |
| Target and reference inventory | Established for inspected inputs | [Hardware](findings/HARDWARE_INVENTORY.json), [binary](docs/DRIVER_BINARY_AUDIT.md), [GPL](docs/GPL_AUDIT.md). Reverify drift-prone device state before device work. |
| D0 offline foundation | Implemented and host-tested within stated limits | Four C components, pinned object inspector, synthetic/fault tests and private-input comparisons. |
| D1 reset/start/acknowledgement | Partial implementation, host-tested | AI-wrapper/startup, PSM control, DMA reset, D11/ChipCommon clock, table/all-core TSSI, PHY-clock and direct reset subsets implemented. Higher reset/SI/wake composition, optional branches and runtime resources remain. |
| D2 complete register/SHM/context contracts | Partial implementation, host-tested | Effective-init semantics and startup SHM16 FIFO read implemented; addend acquisition, physical init-object writes, complete register map and SHM ABI remain. |
| D3 PHY 51/radio/channel/calibration | Attach dispatch mapped; initialization pending | Table/TSSI/PHY-clock subsets host-tested; radio-specific initialization/calibration and board inputs remain. |
| D4 DMA/interrupt/RX | Reset helpers only; data path not implemented | Current reset paths tested offline; common older source mapped. Current queue/resources, descriptors, IRQ and RX remain to establish. |
| D5 TX/Linux WLAN integration | Not implemented | Integration architecture and baseline feature implementation remain open. |
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
| `driver/effective_init.c` | Runtime `0x130` adjustment, width truncation and bounded semantic object-copy events. | Explicit synthetic/caller context; no object-helper port lowering. |
| `driver/ai_reset.c` | AI primary/optional-secondary wrapper trace, bounded busy/release loops and readback-dependent control. | No SI mapping or higher reset; trace completion can include stuck reset. |
| `driver/psm_start.c` | Cached main/extended controls, exact ACK loops/final reads, physical SHM16 FIFO verification. | Startup-suspended predicate only; explicit fail-stop policy, no hardware backend. |
| `driver/psm_control.c` | Wake, main/extended suspend and enable, depth/cache state and bounded SHM polling. | Main enable can end with unimplemented wake-control/MBSS dependency; stricter failure policy, no real callouts. |
| `driver/dma_reset.c` | Current TX/RX reset, direct/indirect selector, status masks, bounds and cache semantics. | No queue mapping, buffers, reclaim or IRQ; returned success need not be an observed disabled state. |
| `driver/clock_control.c` | D11 clock selection, PLL and owned secondary-core traces, exact polling/sample distinctions and observation freshness. | HND fallback/real SI ownership remain dependencies; completion does not establish lock/readiness. |
| `driver/chipcommon_clock.c` | Signed-revision/capability/mode clock body, typed PCI crystal path and exact polling. | Requires actual selected resource ownership; no SI/IRQ lifecycle or WL cache/wake orchestration. |
| `driver/phy_register.c` | Direct read/modify, analog/reset pulse and one/two-core low10 TSSI snapshot/restore. | No outer MAC/PHY-wake/core-flags lifecycle; high reads and three/four-core TSSI expose table dependency. |
| `driver/phy_table.c` | Complete table29/count1/width32 wrapper, actual PHY/PSM children, packed writes and partial state. | Required MAC context/callouts propagate; no outer PHY-enter/exit or physical table/timing proof. |
| `driver/phy_tssi.c` | PHY51 callback for all four core counts, repeated table reads and masked/no-op restores. | Four-slot buffer validity is explicit; outer idle-TSSI MAC/PHY-wake wrapper still required. |
| `driver/phy_clock.c` | AI core-flags, enable/disable, bandwidth/reset preparation and RSDB workaround. | Typed owned wrappers; no mapper/IRQ execution or clock-ready proof; bandwidth recording is separate post-pulse work. |
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

Latest validation (host/build and isolated synthetic ARM execution only):

- All fourteen synthetic test programs pass under ASan/UBSan; all fifteen portable C
  components compile for freestanding ARMv7 with strict warnings.
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

Two earlier exact-HND/WL dependency sessions were exported and closed. Their
26 new Evidence IDs and eight reused records were checked against retained raw
records. Runtime mapping/backend, complete wake policy and optional branches
remain open; the published body models do not implement surrounding callouts.

The earlier exact-WL resource session was exported and closed with unchanged input
SHA-256. Retained raw records live privately under `driver-private/rea-resources`;
older private directories are `rea-lifecycle`, `rea-hnd-reset` and `rea-fifo-shm`.
The public reports/JSON contain original descriptions and selected metadata, not
raw decompilation or payloads. Current `hnd.ko`: 366,060 bytes, SHA-256
`d05ff061b3a636630955a13fdd20a953f3dfb07ee92904a987dd4c77b648b45a`.
The private kernel-boundary proposal remains under
`board-audit/KERNEL_BOUNDARY_AND_INIT_TEST_NOTES.md`, as design input only.

Critical distinctions to preserve:

- Main enable ends `DEPENDENCY` if wake policy or MBSS work is needed. Required
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
2. Use tested table/TSSI/PHY-clock jobs. Implement outer PHY-register reason-2
   wake nesting from retained exact evidence: set may force cached mctrl/wake
   wait; clear may invoke full wake policy even when old reason was absent.
   Preserve outer MAC suspend/enable ordering and actual inactive-run guards.
3. Use tested D11/ChipCommon bodies to compose clock selection through actual
   SI ordinary/fast mapping and IRQ lifetime/restoration. Apply returned mode
   byte and wake-reason0x10 in the WL caller; mode result never establishes ready.
4. Implement remaining full wake-policy/MBSS/fatal callouts and actual input
   acquisition; current child dependencies are unfinished work. Runtime RSDB,
   ownership, clock state, bus/threshold and queue contexts require actual data.
5. Compose higher reset around clock/AI/DMA/PHY jobs and proven caller state,
   queue/resources, bandwidth flags, analog dispatch and TSSI save/restore.
   Resolve optional beamforming/VASIP/SMC and complete wake policy/MBSS/fatal
   callouts; no successful placeholders or invented failure cleanup.
6. Complete D2 physical init-object writes, addend acquisition and needed SHM
   ABI; continue D3 radio/channel/calibration, D4 descriptors/IRQ/RX, TX/Linux
   integration and full features. Separate recovery/RAM-boot/board work remains
   required before exact-board hardware execution and persistent firmware.
7. Extend the original strict differential harness and common trace adapters:
   independent read queues, bank/core/space/width/barriers/delays, no implicit
   zero/write-echo, missing/exhausted/unconsumed read rejection and consumed-bit
   checks. ARM execution covers primitives and selected composed table/TSSI
   inactive-MAC paths; extend to actual PSM/wake/clock only with proven caller
   closure and strict contexts, not successful external stubs. Optional live ARM
   accessor coverage design does not replace implementation.
8. Add independent traces/boundaries/failure/cancel regressions, run affected
   ASan/UBSan/private/ARM checks. Reuse/export/close REA sessions, verify exact
   input identity and maintain this checkpoint with scope and remaining gaps.
9. Review/scan an exact public allowlist, commit/push and verify remote commit/
   tree. Keep proprietary bytes, calibration, credentials and local paths private.

**D1 remains partial.** Higher reset/clock/wake composition, optional dependencies,
wake policy and actual resource ownership remain unfinished. Maintain the full
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
| This checkpoint | PSM control, current DMA reset, context-alias fix and clock/PLL/PHY/TSSI/wake-policy evidence. |

At each future checkpoint, add a concise milestone entry and replace the active
next-work package with the actual next task. Keep unresolved defects visible.
