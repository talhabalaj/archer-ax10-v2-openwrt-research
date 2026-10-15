# Main MAC control and control-reset model

`driver/mctrl.c` exposes the actual main-control cache/update helper and its
software-reset wrapper as reusable offline jobs. It shares canonical
`axd_psm_context` state with PSM operations. The surrounding
`axd_mctrl_context` adds the nine suspended-FIFO bitmap bytes, count byte and
reset halfword that the PSM job does not otherwise own. A caller propagates
the resulting context after every child step, including partial failure.

These jobs have no hardware backend, kernel binding, reset-readiness claim or
RF operation. Their scope is core 130 minor 0/1 with descriptor-resolved D11
ports already established by the existing lifecycle evidence.

## Recovered order

The main helper computes `(cached & ~mask) | value`; it does not narrow value
to mask. An unchanged cache returns without examining or repairing overrides
or mirrors. Otherwise the logical cache commits before the primary store
barrier. A nonzero wake-reason word adds bit `04000000` to the physical write;
a nonzero mute predicate clears bit `00040000` and sets `00020000`. Those
physical adjustments do not change the logical cache. The physical sequence
is `ST,W32(120,effective)` with no reads or readbacks.

Mirror handling requires its enabled predicate and nonzero
`mask & cfc72706`. Its candidate is
`(mirror & ~(mask & cfc72706)) | (value & cfc72706)`. A changed candidate
commits to the mirror cache after the primary write returns and immediately
before `ST,W32(324,candidate)`. Failures or cancellation in the primary
sequence therefore preserve the old mirror cache; failures in the mirror
sequence preserve its already committed new cache. Overrides never alter
the mirror candidate.

The reset entry first clears the main logical cache, exactly nine bitmap
bytes, suspended count, wake word, mute predicate and reset halfword. It then
executes the actual main helper with mask `ffffffff`, value `04000400`.
These software clears precede the first I/O and survive any subsequent
failure. Main and extended suspend depths, extended control cache, and all
other PSM state remain unchanged. The enclosing reset owns any later
extended-control store/write.

The stock mute location is a **word**, read and cleared with word operations.
The existing PSM API represents it as a normalized Boolean predicate rather
than a raw vendor-object image. This job preserves that explicit abstraction;
it does not reinterpret the original operation as a byte store. Inputs to
mute/mirror predicates must be normalized to zero or one. Context structures
are semantic state, not ABI-compatible layouts for stock objects.

## Execution and tests

Begin snapshots inputs before clearing its own job, including an aliased
previous context. Software mutations occur in the first step, so cancellation
before that step preserves the entire input. Each step invokes at most one
synchronous I/O callback. Callback error takes precedence over simultaneous
cancellation; a successful cancelling callback retains completed counters and
software effects. Terminal states are silent and provide no rollback, cleanup,
retry or invented extra write. The callback may cancel but cannot reenter or
mutate its job.

`tests/test_mctrl.c` uses independent literal event queues covering override
adjustments, unmasked values, zero and excluded mirror masks, unchanged main
or mirror caches, the lower-reset `ffffffff/400` update, all nine reset bytes,
both preserved depths, own-context aliases and pre-step cancellation. Every
callback in mirrored direct/reset paths is tested with error, cancellation,
and both together; callback assertions check cache timing before each event.
The test passes strict C11 warnings under ASan/UBSan; the component compiles
freestanding for ARMv7. This is synthetic host validation, not execution of
the stock instructions or a device test.

## Evidence

The exact stock input is `wl` 17.10.121.37, SHA-256
`8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c`.
Retained static Ghidra 12.1.4 observations:

- `ev_00eaeb8439237d4720e1b8f092424dceecb355b626647fcf242902c0e8c3ab6f`:
  complete main-control assembly, cache equality, override and mirror stores.
- `ev_64702f5aed65c2175f3869f9bd9daf0f721b3e301d4d5d6e30c5e970b12add81`:
  complete control-reset assembly, nine-byte clear, word mute clear, halfword
  clear and final branch to the real main-control helper.

These observations establish static instruction order. They do not establish
runtime object acquisition, concurrency safety, actual device state or readiness.
