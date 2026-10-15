# Current progress and continuation checkpoint

Updated 2026-10-15. This checkpoint includes the effective-init, AI-wrapper
reset and PSM startup implementation. Previous driver baseline:
[`00e6fdb`](https://github.com/talhabalaj/archer-ax10-v2-openwrt-research/commit/00e6fdbec3f96bd51768bc2907edfb258b51bd4f).
Read [ROADMAP.md](ROADMAP.md) for the full objective and acceptance criteria.

## Resume here

**Next milestone: finish D1's higher lifecycle/resource contracts and implement
suspend/enable/wake around the tested reset/start subsets.** Reuse the completed
lifecycle evidence; do not restart the same decompilation queries.

Current priority is driver work first. Offline analysis/simulation remains
unblocked by pending UART/recovery work. No device upload, radio initialization
or modern OpenWrt RAM boot has been validated. The existing `TRANSFERRED` state
means a simulated software trace ended, not that firmware ran or became ready.

## Status board

| Milestone | State | Evidence / next condition |
| --- | --- | --- |
| Target and reference inventory | Established for inspected inputs | [Hardware](findings/HARDWARE_INVENTORY.json), [binary](docs/DRIVER_BINARY_AUDIT.md), [GPL](docs/GPL_AUDIT.md). Reverify drift-prone device state before device work. |
| D0 offline foundation | Implemented and host-tested within stated limits | Four C components, pinned object inspector, synthetic/fault tests and private-input comparisons. |
| D1 reset/start/acknowledgement | Partial implementation, host-tested | AI-wrapper reset and main/extended startup subsets implemented; higher clock/PHY/DMA reset, resource context and suspend/enable/wake remain. |
| D2 complete register/SHM/context contracts | Partial implementation, host-tested | Effective-init semantics and startup SHM16 FIFO read implemented; addend acquisition, physical init-object writes, complete register map and SHM ABI remain. |
| D3 PHY 51/radio/channel/calibration | Attach dispatch mapped; initialization pending | Radio-specific callbacks and required private board inputs remain to be recovered. |
| D4 DMA/interrupt/RX | Not implemented | Requires descriptor/status/resource contracts and later exact-board validation. |
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
| `driver/status_poll.c` | Caller-defined status predicate and monotonic timestamps; bounded steps, timeout and cancellation. | Synthetic configurable infrastructure; no recovered device-ready register/mask. |
| `scripts/radio_firmware_inventory.py` | Exact hash, ELF object bounds/relocation checks, size companions and metadata inventory. | Payloads remain private; different/older modules rejected. |
| `scripts/test_driver.sh` | Public ASan/UBSan suite; optional private stock comparison. | Host validation only. |

Recorded passes at the implementation baseline:

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

- [Effective-init implementation/evidence](docs/EFFECTIVE_INIT_AUDIT.md) and
  [published source corroboration](docs/D2_CONTEXT_SOURCE_NOTES.md).
- [Lifecycle ledger](docs/DRIVER_LIFECYCLE_AUDIT.md),
  [selected Evidence IDs](findings/DRIVER_LIFECYCLE_EVIDENCE.json), and
  [implemented boundaries/policies](docs/LIFECYCLE_MODEL.md).
- `tests/test_effective_init.c`, `tests/test_psm_start.c` and
  `tests/test_ai_reset.c` supply independent literal/synthetic expected traces.
- `scripts/test_driver.sh` runs all six synthetic test programs and optional
  pinned private-input comparisons. All seven portable C components compile
  for freestanding ARMv7 with strict warnings.

Latest recorded validation (host/build only):

- Effective-init: 4,212 private-table record comparisons under three explicit
  synthetic addends; every callback in a 13-event mixed trace tested for
  failure/cancellation/simultaneous error. Raw/selection/upload checks still pass.
- PSM startup: literal main/extended and cached/mirrored traces, preservation of
  outside-mask mirror bits, delayed/last-loop/final-only ACK and lost ACK,
  separate 100,000-delay budgets, FIFO predicates and every-event error/cancel.
- AI reset: independent wrapper traces, non-echo control readbacks, optional
  secondary-first ordering, 30-delay busy boundaries, nine release writes and
  stuck-reset diagnostics; every-event error/cancel and terminal silence.
- Synthetic tests pass under ASan/UBSan. No MMIO, firmware execution, calibration
  access, radio operation, Linux module linking or router test occurred.

The lifecycle's WL/HND/FIFO-wrapper analysis sessions were exported and closed;
exact inputs remained unchanged. Raw records are retained outside this checkout
under sibling private-workspace directories `driver-private/rea-lifecycle`,
`driver-private/rea-hnd-reset` and `driver-private/rea-fifo-shm`. Retain those
private bundles for follow-up; publish only reviewed summaries. The current
`hnd.ko` is 366,060 bytes, SHA-256
`d05ff061b3a636630955a13fdd20a953f3dfb07ee92904a987dd4c77b648b45a`.
The earlier kernel-boundary proposal is retained privately under
`board-audit/KERNEL_BOUNDARY_AND_INIT_TEST_NOTES.md`; it is a design input,
not implemented kernel support.

Critical distinctions to preserve:

- Startup waits for **suspended acknowledgement**, then verifies FIFO layout.
  It does not prove radio readiness. A separate final read is mandatory even
  after immediate ACK; exact stock loops are not generic deadline polling.
- The stock extended-timeout path continues after a diagnostic; the new startup
  model deliberately fail-stops. The outer stock BMAC init ignores the helper
  return. Do not claim identical stock failure traces or invent fatal cleanup.
- AI reset continues through busy exhaustion/stuck reset; inspect its diagnostics.
  `TRACE_COMPLETE` does not attest successful reset or clock/PHY readiness.
- FIFO SHM16's wrapper is now resolved to selector `0x10000`; its high-halfword
  access is implemented. The complete SHM ABI and init-object write helper are
  still unresolved.
- The runtime addend comes from **host `hw + 0x260`** and targets hardware
  register `0x130`; there is no established host field at `+0x130`. The object
  address remains u32; only the copied value is narrowed to 16 bits.

## Exact next work package

1. Read Git status, this checkpoint, the lifecycle ledger and implementation
   boundaries. Preserve unrelated changes; fetch/check remote main before
   publication. Reuse exported evidence instead of reinstalling/repeating queries.
2. Recover the current higher `wlc_bmac_corereset` dependencies using retained
   evidence: runtime interconnect/wrapper mapping, core unit, mode-dependent
   normal/reset bits, clock/PLL, DMA/PHY and auxiliary-core operations. Expand
   only missing actual callees. VASIP/SMC prerequisites must be resolved too.
3. Design explicit resource/context contracts and a lifecycle coordinator that
   composes the proven wrapper reset, initialization/upload and startup stages.
   Do not introduce successful stubs or infer clock/reset state from transfer
   completion. Bind runtime inputs to observations before hardware use.
4. Implement the already-recovered suspend/enable/wake paths with separate
   main/extended nesting depths, wake override state, SHM debug-state access,
   exact iteration/final-read behavior and explicit fatal/down callout policy.
   Reject unsupported or invalid nesting/state rather than inventing recovery.
5. Extend D2 object/SHM contracts where those paths require them. Resolve
   `hw + 0x260` stores/acquisition and physical init-object writes. Keep unknown
   board/runtime values explicit; preserve the independent raw-table recorder.
6. Add independent trace/error/cancel and boundary tests for each new behavior.
   Run relevant ASan/UBSan/private comparisons and strict ARMv7 compilation.
   Host validation cannot satisfy Linux integration or exact-board gates.
7. Export/close owned analysis sessions, verify input identity, update this file
   and roadmap, review/scan an exact publication allowlist, commit/push and
   verify remote commit/tree. Keep private bytes and local host paths out.
8. Continue D3 PHY51/radio/channel/calibration, then DMA/interrupt/RX, TX/Linux
   integration and full features. In the board workstream establish recovery,
   RAM boot and resources before exact-board hardware validation and firmware.

**D1 remains partial.** Its higher reset/resource/optional dependencies and
suspend/enable/wake implementation are still missing. Simulator completion does
not mean hardware startup. Follow recovered dependencies into D2/D3 rather than
bypassing them; keep the complete-driver objective unchanged.

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
| This checkpoint | Effective initialization, current-HND AI-wrapper reset, PSM startup/FIFO trace, independent tests and lifecycle evidence. |

At each future checkpoint, add a concise milestone entry and replace the active
next-work package with the actual next task. Keep unresolved defects visible.
