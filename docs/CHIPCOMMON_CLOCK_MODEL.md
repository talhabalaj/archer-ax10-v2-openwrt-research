# ChipCommon clock operation model

`driver/chipcommon_clock.c` is original portable C for the clock-operation body
of the exact HND fallback in [the retained ledger](DRIVER_CLOCK_FALLBACK_AUDIT.md).
It records operations; it has no MMIO/PCI backend, SI mapper, interrupt manager,
clock provider, kernel binding or board default. Source-version comparisons are
corroboration only. The current retained HND assembly controls the trace.

## Resource boundary

The context carries signed ChipCommon revision, capability word, SI bus word,
bus-core identity/revision, and explicit ChipCommon/PCI ownership flags. These
are not D11 revision or physical addresses. For revisions above 5, the caller
must already own the correct ChipCommon mapping with exclusive, IRQ-safe window
lifetime and eventual mapping/interrupt restoration. The bus-core revision is
retained for that resource contract; this operation body does not select a map.

Missing ChipCommon ownership ends `DEPENDENCY_OWNER` before operation-body work.
This is an explicit model prerequisite, not the stock fast-mapping null/wrap
shortcut. The model does not fabricate a selected core index, IRQ token, restored
mapping or successful selection callback. A future coordinator must implement
the ledger's ordinary versus fast resource paths and actual unavailable-fast-map
return separately. A false capability gate or unsupported mode can skip body I/O
without proving that surrounding stock selection/restoration ran.

The old crystal helper requires separately owned PCI configuration access only
when its bus/core branch actually reaches PCI calls. Missing PCI ownership can
therefore arise after the old dynamic-mode ChipCommon write. The model preserves
that completed trace and performs no rollback. All operations distinguish
ChipCommon MMIO and PCI configuration regions; delay events have no register
region. PCI events carry explicit 32-bit PCI-read/write kinds, not MMIO aliases.

## Implemented trace

`begin` copies context before clearing the job, including an aliased
`&job.context`. Valid ownership flags are boolean. The mode remains a full u32
argument: values other than 0/2 are the stock no-body/false branch, not normalized
to dynamic mode. Signed revision at most 5 returns false before ownership or I/O.

For revision above 5, work requires capability `0x40000` or revision above 19.
The gate can skip all operation-body accesses and still return true for mode 0.
Only mode 0 (fast) and mode 2 (dynamic) execute the following bodies. `ST/SY` are
recorded store/full barriers, not host instructions:

| Revision | Mode 0 | Mode 2 |
| --- | --- | --- |
| 6–9 | Crystal-on helper; `ST,R32(0xb8),SY,W32((old&~0x3800)|0x1000)` | `R32(0xb8),SY`; clear `0x3800`, add `0x2000` if `(old&7)!=1`; `ST,W32(new)`; crystal-off helper if new bit `0x2000` is set |
| 10–19 | `ST,R32(0xc0),SY,W32(old|0x10)` | `ST,R32(0xc0),SY,W32(old&~0x10)` |
| 20+ | `ST,R32(0x1e0),SY,W32(old|2)` | `ST,R32(0x1e0),SY,W32(old&~2)` |

There is no extra store barrier between the ordinary RMW `SY` and write, and no
immediate write readback. The older dynamic branch deliberately places `ST`
after its read and `SY`.

The crystal helper is implemented only for the reached `what=1` contract.
Bus 2 returns zero without I/O; other non-1 buses and bus 1 core IDs `0x820` or
`0x83c` return -1 without I/O. Both results are recorded and ignored by the clock
body. Supported bus 1 reads PCI configuration `0xb0`, `0xb4`, `0xb8` in order,
with no fabricated ARM barriers. Crystal-on with input bit `0x40` already set
stops after those three reads. Otherwise it writes saved output OR `0x40` to
`0xb4`, saved enable OR `0x40` to `0xb8`, then delays 1000 us. Crystal-off clears
saved output bit `0x40`, sets saved enable bit `0x40`, writes in the same order,
and has no crystal delay. Arbitrary `what=2` helper branches are outside this API.
A backend error is distinct from the helper's ordinary ignored return and stops
the job.

After a reached body, capability `0x10000000` clear gives one delay of 150 us and
no status poll. Otherwise the model reads ChipCommon `0x1e0`, then `SY`, waiting
for `0x20000` set in mode 0 or clear in mode 2. There are at most 1500
`delay(10),R32,SY` repetitions after the initial read, with no separate final
confirmation. These are requested delay counts, not elapsed deadlines.

## Return, observations and interruption

`TRACE_COMPLETE` means the operation body ended. `return_value` is the stock
mode predicate, except the signed-revision early false return. It stays true for
mode 0 even when the PMU poll exhausts. `poll_exhausted` distinguishes that path;
it does not replace the return. The composite WL fallback must separately apply
the returned low-byte mode result to its cached fast-clock flag and set/clear
wake-reason bit `0x10`. This component does neither and alone does not complete
the WL fallback.

`last_read` includes PCI reads, while `last_cc_sample`/`last_cc_offset` record
only the last successful ChipCommon read. `cc_sample_observed` and
`cc_writes_since_sample` distinguish no observation from a later successful CC
write. `last_status`/`status_observed` track only reads of CC `0x1e0`, and
`clock_writes_since_status` records a later successful write to that register.
The last sample can be a pre-write RMW value; it is not automatically a readiness
observation. PCI reads do not refresh or overwrite either CC observation.

Each step invokes at most one synchronous callback or makes one bounded internal
transition. `attempted_ops` counts entered callbacks; `completed_ops` counts
successful returns. Successful reads, writes and delay-10 callbacks update
observations/counters even if they request cancellation. `poll_delays` counts
successful delay callbacks, including a cancelling one. A simultaneous error
wins and does not count success; an erroring backend might still have produced
external effects. Crystal return validity is set only when its ordinary body
returns, not on partial failure/cancellation. Callbacks may cancel but cannot
reenter or mutate job fields. Failure/cancellation is terminal with no further
I/O, invented cleanup or automatic retry. Neither counter proves hardware commit.

## Validation

`tests/test_chipcommon_clock.c` contains literal expected events independently
specified from the retained ledger/assembly. It checks signed revision guards,
capability/mode gates, revisions 6/9/10/19/20 and INT32_MAX, ignored crystal
returns, input-already-on and off paths, old/middle PMU branches, non-echo samples,
typed PCI/MMIO ordering, ownership dependencies before and after CC writes,
alias-safe restart and terminal silence. A successful-prefix oracle checks all
observation fields after each callback, including interleaved PCI reads.

Independent maximum traces cover both poll directions, 1500 delays / 1501
status reads, last-iteration readiness versus exhaustion, and immediate
readiness without extra confirmation. Selected literal traces inject error,
cancellation and both at every callback; maximum traces inject them at their
last delay/read/barrier. Strict ASan/UBSan execution and freestanding ARMv7
compilation pass. These are host/build checks, not Linux linkage, measured
timing, peripheral behavior, actual mapping ownership or router execution.
The public runner includes these tests; the [checkpoint](../PROGRESS.md) records integration and remaining work.
