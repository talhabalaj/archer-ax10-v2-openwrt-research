# Current progress and continuation checkpoint

Updated 2026-10-15. Implementation baseline:
[`00e6fdb`](https://github.com/talhabalaj/archer-ax10-v2-openwrt-research/commit/00e6fdbec3f96bd51768bc2907edfb258b51bd4f).
This is the operational checkpoint; [ROADMAP.md](ROADMAP.md) contains the full
plan and acceptance criteria. New work must update this checkpoint.

## Resume here

**Next milestone: D1 — recover the actual PSM/PSMx reset, start and hardware
acknowledgement path, then implement its offline lifecycle model.**

Current priority is driver work first. Offline analysis/simulation remains
unblocked by pending UART/recovery work. No device upload, radio initialization
or modern OpenWrt RAM boot has been validated. The existing `TRANSFERRED` state
means a simulated software trace ended, not that firmware ran or became ready.

## Status board

| Milestone | State | Evidence / next condition |
| --- | --- | --- |
| Target and reference inventory | Established for inspected inputs | [Hardware](findings/HARDWARE_INVENTORY.json), [binary](docs/DRIVER_BINARY_AUDIT.md), [GPL](docs/GPL_AUDIT.md). Reverify drift-prone device state before device work. |
| D0 offline foundation | Implemented and host-tested within stated limits | Four C components, pinned object inspector, synthetic/fault tests and private-input comparisons. |
| D1 reset/start/acknowledgement | NEXT; not implemented | Recover actual control/status operations and dependencies. Generic polling is infrastructure only. |
| D2 complete register/SHM/context contracts | Partial facts; implementation pending | Two object ports, selected descriptor fields and raw table consumer known; complete map, effective init and SHM ABI remain open. |
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

## Exact next work package

1. Read [the upload audit](docs/DRIVER_UPLOAD_AUDIT.md),
   [attach audit](docs/DRIVER_ATTACH_AUDIT.md) and
   [driver component notes](driver/README.md). Reuse their retained evidence.
2. Confirm the private input digest matches the pinned `wl.ko` below and inspect
   current REA session state. Use a new private output directory for new work.
   Do not redo tool installation or already completed queries without a reason.
3. Start bounded analysis with these confirmed symbol names:

   | Seed | Question; behavior still needs analysis |
   | --- | --- |
   | `wlc_bmac_enable_mac` | Which main-MAC control writes and completion dependencies are used? |
   | `wlc_bmac_enable_macx` | How does the extended PSM path differ? |
   | `wlc_bmac_suspend_mac_and_wait` | Which status/predicate/timeout establishes main suspension? |
   | `wlc_bmac_suspend_macx_and_wait` | Which extended-bank status and ordering apply? |
   | `wlc_bmac_wait_for_wake` | What is actually polled, and what constitutes wake/completion? |

   These names were checked in the pinned symbol inventory; they are analysis
   candidates, not evidence of their internals. Follow `wlc_bmac_corereset`,
   `wlc_bmac_core_reset`, `wlc_bmac_mctrl`/`mctrlx` or other helpers only when the
   actual call path requires them. Reuse the already analyzed `wlc_bmac_init`.
4. Resolve every relevant core-130 descriptor field and inspect assembly where
   pseudocode may omit barriers/readbacks. Identify actual start/ready code
   through callers; do not equate suspend/wake status with firmware readiness.
5. Write a short lifecycle operation ledger with prerequisites, bank, register,
   width, mask/value, order, barrier, status predicate, timeout/delay and failure
   behavior. Attach Evidence IDs and mark all unresolved fields explicitly.
6. Implement only the proven lifecycle in original C around the upload model.
   Add independent synthetic traces, failures, timeout boundaries and callback
   cancellation regressions. Keep unknown hardware behavior parameterized or
   unsupported rather than inventing defaults.
7. Run the relevant tests, private input comparison where applicable and strict
   ARMv7 compilation. Close any REA native session and verify input unchanged.
8. Update this file and roadmap status, review public-safe evidence, scan the
   selected publication files, commit/push the scoped change and verify the
   remote commit. Report host, build and hardware evidence separately.

**D1 is done only when** the implemented reset/start/acknowledgement contract has
evidence and passing simulator regressions. It will still not mean real hardware
startup has been tested. If its required path reaches PHY/SHM/clock dependencies,
record them and adjust D2/D3 ordering rather than bypassing them.

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

At each future checkpoint, add a concise milestone entry and replace the active
next-work package with the actual next task. Keep unresolved defects visible.
