# Pure stay-awake predicate

`driver/wake_policy.c` implements the recovered software decision in
`wlc_stay_awake`, including the actual RRM, association and hardware-override
getters. It performs no device I/O and changes no caller state. Its result is
`STAY`, `SLEEP`, `DEPENDENCY` or `INVALID`; none means a radio is ready.

The source contract comes from exact stock `wl.ko` SHA-256
`8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c`,
REA 6.5.0/Ghidra 12.1.4 assembly. The principal records are:

| Boundary | Retained Evidence ID |
| --- | --- |
| Ordered predicate and BSS passes | `ev_9f3bd576c4138d21428e8e10ec89e1c38d39b018bd7d047f8254ce3107e7e503` |
| RRM entry / body | `ev_aa367033c214c0d2f12d26f3102a9ba072e8d27d4dff8def3570b21e31ab48f8` / `ev_e6d32510ef0d8cdfb1d124cad88e618a38e98137a865bc8efd533d0a5f7200e2` |
| Association getter | `ev_9db07d1a4431318a11d370ceed78e81c05df70c3892ff191b36861b2abd90ee4` |
| Override getter | `ev_8075e96c293dc37d9750fe6340b17916400a527af075fd8ac95f301165e9f960` |

The C is an original implementation of those behavioral facts. It embeds no
vendor instructions, firmware, tables or runtime router state.

## Inputs and evaluation

Each byte/word/pointer binding has an explicit `known` flag. A reached unknown
binding produces `DEPENDENCY` at that field. A reached `known` value other than
0/1 produces `INVALID`. Later fields are not inspected after an earlier positive
predicate. Zeroed storage therefore represents unknown inputs, never an assumed
sleeping/reset configuration. Raw byte values may be any value from 0 to 255;
word tests use the full unsigned 32 bits, including threshold comparisons.

Pointers are supplied ARM32 identities. The evaluator never dereferences them.
The caller binds their corresponding object fields, preserves stable lifetimes
and synchronization, and supplies consistent fields for repeated identities.
Known null pointers that stock does not guard are rejected as `INVALID` at the
pointer load. Known null guarded pointers follow the actual skip. This rejection
is a model safety policy, not an emulation of a stock null-pointer access.

RRM first reads the public pointer and byte `+0x58`. Zero skips its chain;
nonzero requires the `W+0x530` / `+0x14` objects and their unsigned state `+0x0c`
greater than 3. Association requires the `W+0x22c` / `+4` objects and returns
their full word `+0`, rather than a guessed association-state classification.

`current_override` is a separate explicitly known full word. The PSM caller must
supply its current reason mask **after** clearing reason 2 or 4, where the actual
policy call occurs. An initial snapshot of that mask is insufficient. The
predicate checks this word at the actual getter position after `W+0x188`.

After all globals fail, three separate ordered passes visit exactly 16 BSS
entries. Pass 1 checks byte6/nonzero then byte4/nonzero. Pass 2 checks
byte6/zero, byte8/nonzero, then identity differing from the primary pointer.
Pass 3 checks byte6/zero, byte8/nonzero, byte10/nonzero, a present association
object, and full state neither zero nor `0x1c`. Pass 3 permits the primary
configuration. An earlier slot in pass 3 cannot outrank a later match in pass 1.

The result records the deciding or missing/invalid field, BSS pass and slot.
Its bounded trace records each attempted field access in actual order, including
repeated loads and the first missing input. This is software provenance, not a
hardware capture. `SLEEP` has reason `NONE`; global fields use slot 255/pass 0.
The 256-entry capacity exceeds the maximum 218 source-state loads. Input and
result storage must not overlap; there are no callbacks or cancellation points.

## Integration and validation boundary

The public-up gate belongs to `wlc_set_wake_ctrl` outside this predicate. Known
public-up false returns there without evaluating policy. Public-up true first
reads physical main control and executes SY, then evaluates this predicate.
The PSM action must still perform cached mctrl and, for STAY, wait when the
original physical wake bit was clear even if its cache suppressed the write.
The [PSM component](PSM_CONTROL_MODEL.md) supplies that action and passes current
post-clear reasons. This pure component does not acquire runtime inputs or
supply ownership, MBSS, fatal recovery or kernel binding.

Synthetic tests cover every global positive predicate and reached missing input,
guarded versus required objects, unsigned boundaries, all 16 slots in all three
passes, exact field-access order, primary acceptance in pass 3, full-word state
tests and short circuits that skip unavailable state. Strict ASan/UBSan and
freestanding ARMv7 compilation validate the original C locally; they do not
establish runtime state or target behavior.

The [actual ARM comparison](WAKE_POLICY_ARM_CHECKS.md) matches 373 decisions and
complete logical load traces, plus 139 reached missing-field prefixes. It does
not execute the surrounding MMIO action or supply runtime input acquisition.
