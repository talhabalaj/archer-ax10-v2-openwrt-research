# Actual radio identity query and decode

`driver/radio_identity.c` implements the recovered AC radio identity query,
its registered AC-context thunk, and the distinct AC/common identity parsers for
core130 minor0/1. It uses the actual descriptor-resolved D11 ports. It performs
no native device access, radio-on operation, SI selection, wake operation or
PHY-register helper that the recovered query did not call. It does not infer
this board's radio model or claim that an actual radio ID was observed.

The source is the pinned current `wl.ko`, SHA-256
`8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c`.
[Selected Evidence metadata](../findings/RADIO_IDENTITY_EVIDENCE.json) links five
actual assembly records, a bounded descriptor read, and the reused registration
record. Full analysis output remains private.

## Exact query and registered binding

The actual direct AC query emits, in order:

1. ST, W16(radio-address,0), R16(radio-data), SY.
2. ST, W16(radio-address,1), R16(radio-data), SY.
3. Combine the **first** narrowed sample as high16 and the second as low16.

The pinned core130 common descriptor fields88/8a resolve these ports to3d8/3da.
They are different from the PHY-register address/data ports3fc/3fe. There is no
address readback, extra data sample, delay, restore-to-address0 or guessed
hardware cleanup. The function leaves its last address write at1.

The registered `_phy_ac_radio_query_idcode` thunk loads PI from the first word
of the supplied AC callback context and tail-calls that same direct query. The
reused AC registration record places this thunk in slot4/offset10 and the AC
context in slot8/offset20 of a nine-word common function block. Actual common
registration copies those nine words. The original common caller's complete
registry/lifetime acquisition is not implemented by this identity job.

The registered entry requires explicit known/non-null callback context and its
PI binding, matching the supplied target PI. The direct entry does not inspect
unused registered-context facts. The model composes the recovered actual body;
it never invokes an arbitrary callback that merely reports successful identity
acquisition. Resource ownership, descriptor identity and powered radio/D11
access remain caller requirements.

## Distinct parser contracts and partial state

The actual AC parser stores these semantic public fields, in source order:

- Public halfwordA: idcode low16.
- Public byteC: idcode bits16..23.
- Public byteD: zero.

The separate common parser stores bits12..27 to halfwordA, bits28..31 to byteC,
and bits0..3 to byteD. The model names these fields radio ID, revision and
version; these semantic snapshots are not stock ABI-compatible object layouts.
The common parser is not substituted for the AC parser.

Direct query and registered query return an identity observation without changing
those public fields. Query-plus-AC-parse composes the actual direct query and AC
parser used by attach. Standalone parser entries use a caller-supplied idcode.
The public pointer becomes required only when an actual parser is reached; an
unknown unused public pointer does not prevent a query. Unknown reached pointers
stop as dependencies; required null bindings fail rather than emulate an invalid
stock dereference.

Begin snapshots its context before clearing the job, including its own previous
context. Each step invokes at most one synchronous user callback. Reads and
barriers retain separate observation masks, including successful callbacks that
cancel the job. A callback error wins over simultaneous cancellation. Combining
the samples and each later public-field store are separate internal transitions;
they do not run after a terminal callback exit. Cancellation between parser stores
preserves the exact completed prefix. Terminal jobs are silent, with no invented
restore, retry, rollback or radio-ready result.

## Independent checks and execution boundary

`tests/test_radio_identity.c` uses a literal eight-event protocol with distinct
high/low supplied replies, including values above16bits to check narrowing.
It covers direct/registered/composed paths, distinct AC/common decode, aliases,
required and unused bindings, delayed public-pointer dependency, partial parser
stores, all callback failure/cancellation/coincident-error positions, one callback
per step and terminal silence. Five private semantic mutants for sample order,
parser fields and registered binding are rejected. Strict ASan/UBSan host tests,
combined-header compilation and freestanding ARMv7 compilation pass.

`tests/check_radio_identity_arm.py` separately executes the exact selected stock
instructions in pinned Unicorn2.1.4 with synthetic contexts and strict read
queues. It resolves function bytes by ELF section/symbol metadata, rejects an
incorrect module digest or selected-function relocations, restricts every
context/stack/MMIO access, and records actual barrier instructions. Across68
query traces and264 AC/common decode cases, the original C observations, full
I/O traces, public store widths/order and decoded fields match the stock leaves.
No firmware payload or radio identity value is printed or exported.

That ARM check covers the three direct closed leaves, not the registered thunk's
unresolved tail-branch relocation, full AC attach, radio switch/on, channel setup,
calibration or a complete driver. The registered context binding is covered by
the separate literal C suite. Emulator execution with synthetic resources does
not establish this router's radio identity, physical timing, hardware behavior,
OS ownership or a working Wi-Fi data path. The bounded native analysis session
was exported and closed successfully; no target process or router operation ran.
