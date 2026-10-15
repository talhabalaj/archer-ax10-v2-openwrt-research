# PHY51 initialization dispatch and remaining dependencies

This audit follows actual registered table/radio initialization in the pinned
stock `wl` 17.10.121.37. It extends the retained attach investigation with
bounded static Ghidra observations. The [selected metadata and Evidence IDs](../findings/DRIVER_PHY_INIT_EVIDENCE.json)
contain provenance without raw instructions, tables, calibration or host paths.
The generic AC table-write leaf is now [implemented and host-tested](PHY_TABLE_WRITE_MODEL.md).
`driver/phy_init.c` also composes the complete target51 table-initialization
callback with actual PHY, table, SI PMU and GPIO children. This is one registered
stage. The complete PHY/radio scheduler, calibration and working Wi-Fi driver
remain open.

## Registered table path

AC registration calls `phy_ac_tbl_register_impl` when the common PHY table
module exists. The table implementation registers `phy_ac_tbl_init` in function
slot0 and its implementation context in slot6. The common `phy_tbl_register_impl`
copies the nine-word function/context block into the common table module.

`phy_tbl_attach` registers `phy_tbl_init` as an initialization callback at
priority8, with the common table module as context, and a down callback at
priority2. Actual `phy_tbl_init` loads the registered function from slot0 and
its context from slot6, then tail-dispatches to it. Direct-caller searches for
the AC function can therefore be empty without establishing that it is unused.

The AC wrapper calls `wlc_phy_init_acphy` and returns zero. The surrounding
initialization scheduler calls its ordered callback array, stops on a nonzero
callback result, and only after the array returns successfully marks slice-up.
Registration sorts priority bytes and function/context entries; complete
registry acquisition and all other registered stages remain separate work.

The resulting observed chain is:

`phy_init_invoke_init_fns` → registered priority8 `phy_tbl_init` → copied
slot0/slot6 binding → `phy_ac_tbl_init` → `wlc_phy_init_acphy`.

None of these software registrations establish calibration, successful table
programming or hardware readiness.

## Actual target51 table initialization

`wlc_phy_init_acphy` has a bounded 1,828-byte disassembled instruction body plus
one four-byte literal slot; its ELF function symbol spans all 1,832 bytes.
The private-span inspector pins the complete symbol, including that relocated
literal source pointer. Following revision51 yields this ordered work package:

1. Clear PHY19e bit1 with a real RMW, set PHY1b0 bit8000, then read PHY0.
2. Invoke actual SI PMU voltage control for register0/mask01f00000. Its value
   depends on PHY0 low8 and an OTP-derived u16 retained in the table context.
   The inputs choose the recovered zero/subtract-three/default-five encoding:
   PHY0 low8 nonzero selects max(OTP5bits minus3,0); zero selects the nonzero
   OTP5bits value or the binary's explicit5 branch. Shift the resulting units
   left20. No board voltage or OTP value is assumed.
3. Invoke actual SI GPIO control with maskffff/value0. Additional PMU/GPIO
   branches in the generic body apply to explicitly named other chips, not
   chip63178. Their existence does not justify importing those chip defaults.
4. Set PHY16e masks2,1,10 in order with actual RMWs, then direct packed writes
   to PHY16f and170. Read PHY19e afresh, set its bit1, and clear bits in
   PHY10f7(mask10) and PHY1120(mask4).
5. Revision51 skips the older revision register block and writes three table16
   spans: count8/offset1000/width32, count64/offset1040/width32, then
   count1/offset1020/width32. Their source bytes remain private inputs; this
   report does not reproduce arrays or insert zero payloads.
6. Restore PHY19e bit1 from the fresh saved sample, then clear PHY400 bit1.

The direct PHY primitives, actual table-write wrapper and real SI PMU/GPIO
helpers are composed as C children. PMU register-address658 and data65c execute
actual corereg reads, barriers, writes and readbacks, with real SI mapping/IRQ
paths. GPIO6c retains its actual reservation-mask rule. No successful callout
stands in for those operations. Runtime OTP/state acquisition and exact private
payload binding remain caller requirements; completing this portable callback
does not establish that its hardware inputs were acquired or executed correctly.

## Implemented coordinator and payload boundary

`axd_phy_init_context.hardware` is canonical PHY/PSM/table state; `.control`
is canonical SI/PMU/GPIO state. Both propagate partial child effects, including
successful cancelling callbacks. `control_state_known` carries IRQ-state
freshness into subsequent calls and alias restarts. Unknown reached SI state
stops before a new control child; it is not assumed unchanged.

The caller supplies actual OTP word10 with an explicit known flag. The
coordinator derives only bits8..12, at the real voltage decision after the fresh
PHY0 sample. An unknown word stops there after the earlier real PHY effects.
The three ordered table bindings are also explicitly known and externally
immutable. Their minimum byte lengths are32/256/4; unknown bindings stop when
that table is reached, and missing/short data fails before that child writes.

`scripts/phy_init_inventory.py` pins the stock module, resolves its ELF source
relocations for the32/256-byte spans and decodes the one-word ARM immediate
construction for the4-byte span. It prints metadata only by default. Optional
extraction requires a new private directory outside the checkout. The
[published inventory](../findings/PHY_INIT_PAYLOAD_INVENTORY.json) contains only
source positions, lengths and hashes. Private extraction was independently
checked against all three size/hash records; an unknown input was rejected.

Begin snapshots own-context aliases. Each step invokes at most one user I/O
callback; callback error wins cancellation, and successful observations remain
available even on cancellation. Real children publish software stores before
their associated callbacks and retain partial state after return. Whole-table
completion flags wait for each child to return normally. Terminal jobs perform
no invented restoration, GPIO cleanup, retry or rollback.

`tests/test_phy_init.c` uses an independent412-event typed literal chain under
synthetic cached SI mappings and an inactive MAC. Its64 explicit voltage cases
cover both PHY0 predicates and all32 OTP encodings. It checks every callback
error, cancellation and both together, sampled-value freshness, late table
completion, canonical partial PHY state, unknown OTP/SI/table inputs, short data
and own aliases. Strict C11 ASan/UBSan checks and ARMv7 compilation pass. Six
private mutants were rejected: invented zero-voltage default, missing initial
clear, lost saved guard bit, omitted real PMU child, lost partial PHY state and
wrong table count. These are host checks, not stock-function or hardware runs.

## Board and radio dependency graph

The retained table-registration path obtains voltage, gain and board-flag
inputs through NVRAM/slice-specific helpers and reads OTP. Those are runtime
dependencies requiring actual board evidence. Default arguments observed in
the vendor API are not selected calibration values for this router.

The AC radio registration separately installs `phy_ac_radio_switch`,
`phy_ac_radio_on` and `_phy_ac_radio_query_idcode` in the common radio function
block, with its AC context. It also reads board/OTP fields and multiple PHY
registers, and calls the actual radio-off implementation during attach. A
registration trace alone does not close any of those radio operations.

The remaining graph at this boundary is explicit:

- The full ordered initialization registry requires acquisition of every
  callback and its coherent context, failure rules and priority.
- The recovered target table callback now composes actual SI PMU voltage/GPIO
  protocols and the table writer. Actual OTP/NVRAM/board acquisition, private
  payload provenance and physical resource execution remain required.
- [The radio identity query/decode contract](RADIO_IDENTITY_MODEL.md) is now
  implemented offline. Actual observed ID/subrevision, switch/on dispatch and
  register/channel-tuning inputs still require closure.
- Channel setup, power control, calibration manager and its registered TX/RX IQ,
  VCO, TSSI/PAPD and other applicable callbacks require their own exact target
  contracts and board-derived data. Their attachment names do not prove their
  execution order or compatibility.
- The device still needs ownership/mapping/MMIO/IRQ adapters, synchronization,
  DMA RX/TX and Linux WLAN integration, followed by exact-board hardware proof.

This graph records reached and named remaining dependencies; it does not claim
that all descendants of the still-unrecovered radio/calibration callbacks are
already known. No success stub, guessed calibration value or copied vendor array
is used to hide an open edge.

## Scope of evidence and execution

The new native session was exported and closed. Existing registration,
packed-write and HWACI records were reused rather than decompiled again.
New queries answered missing table dispatch, writer and target initialization
questions. Targeted jump-table reads were four bytes each. All source payloads
and raw records remain private. The selected public metadata contains only
original summaries, identities, function names and Evidence IDs.

Host tests and ARMv7 compilation validate the original generic writer and this
target table callback. They do not establish complete initialization, physical
PMU/GPIO behavior, radio operation, calibration or firmware readiness. No router
access, module load, reset, RF operation or new hardware execution was performed.

## Completion gates from this boundary

Offline work can continue by recovering the complete ordered initialization
registry and closing its actual radio switch/on, channel and calibration
children. Each needs source-derived operations, real dependency composition,
independent traces and explicit partial-state/error rules. Merely registering
their names or providing an always-successful callback does not meet this gate.

Board binding requires actual radio ID **and subrevision**, OTP word10 and other
reached OTP/NVRAM/calibration inputs, coherent PHY/SI objects and verified private
payloads. Radio identity is not derived from chip63178 or assumed present in the
hardware inventory. Existing private evidence may satisfy individual metadata
requirements; missing values remain unknown, not zero defaults.

Physical validation additionally requires actual resource ownership, OS/kernel
adapters, synchronization, an appropriate recovery route and staged exact-board
tests. Calibration, interrupts, DMA RX/TX and Linux WLAN integration must be
implemented and validated before a replacement Wi-Fi driver or OpenWrt image
can be called complete. No host-test count or trace-complete flag meets that gate.
