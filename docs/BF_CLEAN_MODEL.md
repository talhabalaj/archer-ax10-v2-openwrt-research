# Core-130 beamforming cache cleaner

`driver/bf_clean.c` is an original bounded model of the cleaner body recovered
in the [reset/beamforming ledger](DRIVER_PHY_RESET_BF_AUDIT.md), with
[selected Evidence IDs](../findings/DRIVER_PHY_RESET_BF_EVIDENCE.json).
It runs real PSM children and traverses explicit software metadata. It emits
no beamforming SHM writes for core 130 because the actual two leaf helpers
return before their older-core SHM branches. Completion establishes a software
trace, not hardware cache contents, radio readiness or a complete reset.

## API, identity and acquired state

The reset coordinator owns its preceding ordered gate: hardware flag mask `0x1`,
public byte `+0x10e`, selected TXBF pointer and its byte `+0x12`. It binds that
selected pointer to `context.txbf_identity` before starting this cleaner.
The cleaner requires explicit nonzero TXBF, WLC and hardware identities and
checks reached `T->WT` and `WT->HW` against the supplied WLC/hardware resource.
A different binding fails rather than applying another object's MAC state.
This is an explicit ownership policy; stock does not perform those equality
checks. Known null required public/object bindings also fail.

Metadata and bitmap/slot arrays are immutable caller-owned storage outside all
jobs. No ARM32 identity is dereferenced. The `peer` binding is the full word
obtained by the owner at `slot_pointer + cubby_offset`; the model still reaches
the actual cubby-offset and peer accesses in order. The owner must establish
valid addresses, cubby resolution, pointer arithmetic, object lifetime and
synchronization. Repeated identities, including aliases across lists, must
describe the same coherent objects. Values may not change during a job.

Each reached raw byte/word binding requires `known==1`. Unknown bindings and
missing reached model storage produce `DEPENDENCY` at that field. Other known
flags fail. Skipped bitmap entries require no slot/peer data, null slots require
no cubby data, and null peers require no leaf state. Reached array bounds fail
before the model reads outside supplied storage. Those explicit bindings do
not acquire device runtime state or imply that unknown state is zero.

## Actual ordered operation

1. Load `T->WT`, then run main MAC suspend with the supplied current context.
2. Visit primary index zero unconditionally. Test its bitmap bit, then optional
   slot and cubby peer. A valid peer calls invalidate then enable-bit helper,
   passing the former's returned byte as the latter's argument.
3. Reload the unsigned primary last index after **every** visited entry. The
   incremented index continues while at most that value: bound zero visits one
   entry. The strict limit is 256 entries, rejecting bounds at least 256. This
   check occurs at the actual reload, after index-zero peer work; it is not a
   preflight that suppresses earlier effects. Unsigned wrap is never simulated.
4. Run main MAC enable, then read the actual public extended-feature byte.
5. When nonzero, run extended MAC suspend, sample secondary byte count, scan
   indexes strictly below that count, then run extended MAC enable. Count zero
   skips entries but still executes both extended children. Secondary count is
   reloaded only after a valid peer's two helper calls; skipped entries retain
   the previous sample. The immutable snapshot supplies the same value on each
   actual reload, without eliminating those accesses.

Invalidate first reads WT byte `+0x21`. Zero returns zero immediately; nonzero
then checks the public core revision and returns the original byte for core
130. Enable-bit separately rereads WT byte `+0x21`, then conditionally public
extended-feature byte and core revision; core 130 returns before SHM access.
Raw bytes remain full bytes, not Boolean substitutes. A reached public revision
other than 130 is unsupported by this target-specific model. No opaque callback
or fabricated successful SHM operation replaces either actual leaf path.

`invalidation_calls` and `enable_calls` count helper entries. The invalidate
result becomes valid only at its actual return branch; missing reached core
state cannot publish a returned value. `enable_value` records the argument
passed to the subsequent helper. Iteration counters advance at completed loop
tests, separately from current index or partial helper entry.

## Progress and failure

Each step makes at most one logical metadata access **or** one user backend
callback. `metadata_accesses` and `last_access` expose field/list/index/value
provenance, including the first attempted unavailable input. This is a trace
of supplied behavioral bindings, not an execution of actual ARM loads or proof
of live pointer acquisition. It contains no unbounded per-entry log allocation.

MAC children use the existing policy, original physical wake sample, caches,
depth and bounded polling. Child context and pending callouts propagate after
every step, including errors and cancellations. Fatal/dependency exits stop the
cleaner under the documented PSM fail-stop policy. Simultaneous callback error
wins over cancellation; successful callback effects remain. Terminal calls are
silent and never invent MAC enable, cleanup, retry or rollback. Resource mapping,
locking, metadata acquisition and hardware backends remain external.

Synthetic tests specify independent literal metadata/physical traces for active
and inactive MACs, all peer skip cases, exact bound/count reloads and both leaf
paths. They cover reached dependencies, identity mismatch, invalid core/bounds,
the full 256/255 entry limits, post-index-zero cap rejection, alias-safe restart,
semantic child failures/dependencies, every physical callback fault/cancel pair
and cancellation at every literal metadata boundary. Strict ASan/UBSan tests
and freestanding ARMv7 compilation pass; no device execution was performed.
