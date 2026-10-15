# Current progress and continuation checkpoint

Updated 2026-10-15. Implementation baseline:
[`00e6fdb`](https://github.com/talhabalaj/archer-ax10-v2-openwrt-research/commit/00e6fdbec3f96bd51768bc2907edfb258b51bd4f).
This is the operational checkpoint; [ROADMAP.md](ROADMAP.md) contains the full
plan and acceptance criteria. The baseline above is the last published driver
implementation; the working-tree checkpoint below includes newer, uncommitted
work. New work must update this checkpoint.

## Resume here

**Next milestone: D1 — finish the recovered reset/start operation ledger and
implement its offline lifecycle model. First preserve and review the pending D2
initialization change described below.**

Current priority is driver work first. Offline analysis/simulation remains
unblocked by pending UART/recovery work. No device upload, radio initialization
or modern OpenWrt RAM boot has been validated. The existing `TRANSFERRED` state
means a simulated software trace ended, not that firmware ran or became ready.

## Status board

| Milestone | State | Evidence / next condition |
| --- | --- | --- |
| Target and reference inventory | Established for inspected inputs | [Hardware](findings/HARDWARE_INVENTORY.json), [binary](docs/DRIVER_BINARY_AUDIT.md), [GPL](docs/GPL_AUDIT.md). Reverify drift-prone device state before device work. |
| D0 offline foundation | Implemented and host-tested within stated limits | Four C components, pinned object inspector, synthetic/fault tests and private-input comparisons. |
| D1 reset/start/acknowledgement | Analysis in progress; model not implemented | Main/extended startup, suspend/wake and wrapper reset paths recovered; finish assembly review and publish the operation ledger before implementation. |
| D2 complete register/SHM/context contracts | Partial; effective-init code tested locally, uncommitted | Runtime adjustment/object-copy semantic model exists in the working tree; physical object-helper expansion, complete map and SHM ABI remain open. |
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

## Working-tree checkpoint — preserve before resuming

The following newer implementation is **local and uncommitted** at this
checkpoint. Do not discard it or treat the published baseline as containing it:

| Files | Current result | Remaining work |
| --- | --- | --- |
| `driver/effective_init.c`, `driver/effective_init.h` | Semantic core-130 initialization state machine: explicit runtime addend for register `0x130`, width truncation, object-copy arguments, bounds, terminal failure/cancellation. | Review and publish its evidence/README alongside the implementation. No physical object-port backend exists. |
| `tests/test_effective_init.c` | Independent mixed trace and every-event failure/cancellation tests. | Preserve the independent oracle when extending the implementation. |
| `tests/check_stock_firmware.py`, `scripts/test_driver.sh` | Integrate effective-init checks; compare 4,212 table records across three explicit synthetic runtime contexts. | Publish with the C component; retain private payloads outside Git. |

Recorded local validation: the complete synthetic ASan/UBSan suite and optional
pinned private-input comparisons passed; `effective_init.c` also compiled for
freestanding ARMv7 with strict warnings. Existing raw-table, selection and upload
comparisons continued to pass. These are host/build results, not device tests.
The three runtime addends are synthetic test inputs, not recovered active board
values. Object-copy events stop at the helper-call boundary.

New D1 analysis has followed the real main/extended start, suspend, enable and
wake paths and the imported reset into the exact current `hnd.ko`. Private raw
records are retained in the sibling private workspace under
`driver-private/rea-lifecycle` and `driver-private/rea-hnd-reset`; do not copy
them into this public checkout. Preserve/export/close any remaining owned native
analysis sessions and retain a sanitized report before further implementation.
The exact current `hnd.ko` is 366,060 bytes, SHA-256
`d05ff061b3a636630955a13fdd20a953f3dfb07ee92904a987dd4c77b648b45a`.

Critical distinctions from that analysis to carry into the reviewed ledger:

- Startup polls for **suspended acknowledgement**, not a generic radio-ready
  state. Main and extended paths have different error propagation.
- The recovered loops contain separate final confirmation reads. The generic
  timestamp poller does not automatically reproduce their exact trace.
- The stock caller does not propagate every helper failure; an offline fail-stop
  policy must be labeled as a deliberate stricter model policy.
- Reset has clock/control and potentially secondary-wrapper dependencies.
  SMC/VASIP/PHY prerequisites must be resolved or explicit, never bypassed by
  assuming that a completed microcode transfer is sufficient.

## Exact next work package

1. Inspect Git status and preserve all five pending implementation/test files
   listed above. Fetch/check remote main before publishing; do not overwrite
   another change. Read the relevant evidence and driver README.
2. Finish the bounded D1 assembly review. Export/close owned REA sessions and
   write the public-safe lifecycle report plus Evidence summary. Resolve actual
   descriptor fields, reset inputs, optional paths and caller error handling.
   Reuse retained analysis instead of repeating installation/decompilation.
3. Review the pending effective-init implementation against the existing
   `wlc_write_inits` evidence. Document the runtime addend source, byte-address
   formula, low-16-bit **value** and semantic object-helper boundary. Update the
   driver README and publish the scoped D2 subset with its tests/evidence.
4. Build the D1 operation ledger: prerequisites, bank, register, width, mask/value,
   barriers/readbacks, exact loop counts, delay/final-read order and failures.
   Keep startup/suspend/wake predicates separate. Distinguish observed behavior
   from additional validation/fail-stop policy.
5. Implement original bounded lifecycle C with no real MMIO backend. Add
   independent traces, immediate/last-poll/timeout cases, injected operation
   failures, main/extended differences and callback cancellation regressions.
   Do not shoehorn exact stock polling into the generic deadline semantics.
6. Run relevant ASan/UBSan tests and private-input comparisons; compile changed
   C for ARMv7. Record exactly what passed and what remains untested.
7. Update this checkpoint and roadmap in the same substantive change. Review
   the exact publication allowlist, scan it, commit/push and verify the remote
   commit/tree. Preserve unrelated dirty files and keep private raw inputs out.
8. Continue remaining D2 register/object/SHM contracts, then D3 PHY 51/radio and
   required calibration inputs. Follow the roadmap through DMA/RX, TX/Linux
   integration, board bring-up and exact-board validation.

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
