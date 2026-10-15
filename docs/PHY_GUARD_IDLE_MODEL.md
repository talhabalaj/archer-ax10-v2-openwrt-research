# PHY nesting and the enclosing idle-TSSI wrapper

`driver/phy_guard.c` implements PHY-register enter/exit nesting and their actual
reason-2 wake override. `driver/idle_tssi.c` composes the complete outer
idle-TSSI wrapper around the previously implemented all-core callback. They
reuse real PSM jobs, preserve partial contexts and expose unresolved inputs;
they have no resource mapper, lock, MMIO backend or hardware-ready result.
Provenance is the [PHY wake addendum](DRIVER_PHY_WAKE_ADDENDUM.md),
[resource ledger](DRIVER_RESOURCE_AUDIT.md) and
[exact reset ordering](DRIVER_PHY_RESET_BF_AUDIT.md).

## PHY enter/exit

Enter increments the byte nesting depth before override work. An inner enter
does nothing further; the outer transition to one executes generic override
SET with reason `2`. Exit decrements before override work; only the outer
transition to zero executes CLEAR with reason `2`. The actual SET may update
cached MAC wake and run its bounded wake wait. CLEAR may invoke wake control
even if that reason was already absent. The original full reason mask and
cached controls are preserved, rather than substituting a physical wake clear.

The model rejects enter at 255 or exit at zero on the first ENTRY step, before
wrapping or callbacks. This is an explicit stricter policy than the stock
unchecked byte arithmetic. Software depth changes already made before a child
failure/cancellation remain visible; no unwind silently decrements them again.

## Outer wrapper ordering

With a known absent optional callback, the wrapper returns without MAC/PHY I/O,
buffer changes or checks of unused PHY/MAC/saved state. A present callback uses:

1. Main MAC suspend through the actual PSM child.
2. PHY enter through the actual nesting/override child.
3. All-core TSSI snapshot or restore through the composed callback child.
4. PHY exit through the actual nesting/override child.
5. Main MAC enable through the actual PSM child.

MAC run-bit guards remain active. These calls do not imply an unconditional
requirement for running microcode during initial reset. The explicit supplied
context determines whether suspend/enable are no-ops or real sequences.

A present callback requires valid matching MAC/PHY context. RESTORE validates
active saved slots before any I/O, a stricter buffer policy than stock. Nesting
overflow is checked later at the actual guard transition, so a bad depth can
stop after MAC suspend with that partial MAC context retained. Callback absence
skips these unused validations.

An optional explicit callback-binding check now runs at the actual callback
point after MAC suspend and PHY enter. A known nonnull callback context must
contain the expected PHY identity in its first word. Unknown reached bindings
end DEPENDENCY; null, invalid or mismatched bindings fail with the entered depth
and partial MAC state retained. Callback absence skips these unused fields.
The [target reset](PHY_RESET_MODEL.md) enables this check and separately binds
the dispatch module/table before its callback null gate. Legacy normalized
standalone contexts keep their explicit owner/resource-coherence obligation;
they do not claim raw pointer-graph validation.

`entered`, `exited` and `callback_completed` indicate whole child completion.
They do not hide earlier partial buffer, depth, cache or reason changes. For
example, cancelled callback data can remain in a valid direct-read slot while
`callback_completed` is false and PHY exit/MAC enable have not run. A table data
word also remains distinct from completed table-wrapper/TSSI-slot commitment.

## Policy and failure boundaries

Policy facts, including an optional immutable [actual predicate input snapshot](WAKE_POLICY_MODEL.md),
flow through the shared table/MAC context. Unknown public-up or reached policy
state is a dependency; an explicitly known public-up zero follows the real
stock no-I/O return. Predicate evaluation receives the **current** reason word,
after clearing reason 2 or 4. Stable external state and resource ownership
remain caller obligations, not inferred defaults.

At most one user backend callback occurs per outer step. Child contexts and
pending callouts propagate after every step, including failed/cancelled ones.
Successful cancelling callbacks retain their effects; simultaneous callback
error wins. Parent counters count actual user callbacks, not adapter invocations.
Terminal states are silent and invent no guard cleanup, buffer restoration,
MAC enable, retry or rollback. Callback-triggered cancellation reaches the
active descendants. Callbacks may not reenter or mutate job fields.

## Tests and remaining reset work

Independent literal tests cover nesting boundaries, inner no-I/O paths,
reason-mask/cache/wake behavior, known/unknown policy and every callback exit.
Outer tests cover snapshot/restore for all four core counts, inactive/active
MAC, actual table children, optional absence, partial saved data/depth/context,
semantic failures, alias-safe restart and terminal silence.

The [target PHY reset coordinator](PHY_RESET_MODEL.md) places the **outer snapshot before**
`RESET_PREPARE`, then pulse/delay100, bandwidth recording, analog reset,
outer restore and optional beamforming cleaner. The
[new beamforming/order audit](DRIVER_PHY_RESET_BF_AUDIT.md) establishes that
cleaner's core-130 no-SHM child paths but preserves its real main/extended MAC
operations and metadata scans. The [cleaner](BF_CLEAN_MODEL.md) now preserves those real operations; higher
device-reset integration remains unfinished. Host checks do not establish device
timing, physical wake/PHY state, calibration or RF operation.
