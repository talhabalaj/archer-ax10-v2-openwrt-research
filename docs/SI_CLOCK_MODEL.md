# ChipCommon selection, IRQ and clock composition

`driver/si_clock.c` implements the actual HND resource wrapper around the
[ChipCommon clock body](CHIPCOMMON_CLOCK_MODEL.md), using real
[SI selector children](SI_SELECT_MODEL.md). Exact behavioral provenance is
the [retained assembly review](DRIVER_CLOCK_WRAPPER_REVIEW.md),
[earlier fallback ledger](DRIVER_CLOCK_FALLBACK_AUDIT.md) and
[selected Evidence](../findings/DRIVER_CLOCK_WRAPPER_REVIEW_EVIDENCE.json).
No native kernel, IRQ, mapping, PCI or MMIO backend is supplied.

## Actual branches

Signed ChipCommon revision at most 5 returns exactly zero before selection,
callbacks or checks of unused catalog/ownership inputs. It is not mode0=true.
All other normal returns are the full32 predicate `mode == 0`, including
skipped operations and exhausted polls. No return establishes a ready clock.

Fast mapping applies only to bus1 with bus-core ID `0x83c`/`0x820`, or `0x804`
with bus-core revision above12. It first checks the original current mapping.
Zero skips all work. Otherwise compute `u32(map + 0x3000)`; only a zero result
skips. Other wrapped nonzero results are accepted. The derived map is local:
no current index/map store, selector child or IRQ callback occurs on this path.

The ordinary path performs:

1. Save the initial current index. If the disable callback is present, compare
   the current enumerated core ID with the configured IRQ core ID. Null callback
   skips that guard's catalog access. Only equality invokes disable.
2. Capture the full returned token and actual post-callback SI state. Reload the
   saved restoration index from that state, because disable can change it.
   If disable did not run, token remains zero and the initial index is retained.
3. Call the real selector for core ID `0x800`, unit0. Preserve actual mapping,
   caches and index; do not equate a requested index with a successful switch.
4. Run the actual clock body for reached capability/mode cases. A capability or
   mode skip still proceeds to restoration. The exact body retains its older
   crystal/PCI branches, settle delay or bounded status poll.
5. Call the real by-index selector with the saved index. Its return is ignored;
   an out-of-range or unsupported selector can leave current state unchanged.
6. Independently check the restore callback and the **resulting** enumerated
   current core. Invoke only on equality, with the saved token even if disable
   was absent and token is zero. Capture actual post-restore SI state.

The catalog/configuration/callback identities are stable owner bindings. Mutable
mapping caches and current SI state are copied job snapshots. The model actually
re-evaluates current index/core at both guarded boundaries; it does not infer
that disable preserved the core or that restoration succeeded. Metadata
acquisition, arbitrary catalog/config mutation and SB selection remain outside
this interface. Reached catalog/index errors are explicit dependencies/failures,
not fabricated guard mismatches or board defaults.

## Typed backend and IRQ state

Backend operations preserve MAP physical/size/type, PCI config offset/width,
ChipCommon logical mapping plus register offset, barriers, delay and IRQ
callback/context identities. SI core/primary/secondary maps and ChipCommon/PCI
resources remain distinct. ARM32 mapping/identity values are labels resolved by
an actual adapter; no host function pointer is invoked by this model.

An IRQ operation exposes its read-only pre-call SI state. The backend must
return the actual post-call state with `irq_state_known == 1`, even when it is
unchanged, and disable's actual full32 token. This is an explicit adapter
contract for side effects, not an invented stock callback return signature.
It lets the model represent callbacks that change current core without allowing
callbacks to mutate a job. Successful callbacks with missing post-state end
DEPENDENCY. Entering an IRQ callback marks `state_valid` false; a known post-state
refreshes it. An error leaves the old diagnostic snapshot unverified and never
commits untrusted output. A missing backend enters no callback.

Generic selection has no stock zero-result guard. The model therefore does
not reject a null selected mapping before a capability/mode skip. It rejects
only a reached CC read/write through zero, under explicit safety policy. Store
barriers and the old crystal PCI helper can precede that first dereference.
This is separate from fast mapping's actual zero skip. Caller ownership remains
required at a reached operation body and its PCI branch.

## Interruption and validation

Each step performs at most one user callback or a bounded transition. Selector
caches/current state propagate on every child step, including cancellation and
error. A successful cancelling MAP caches its result even though later SI stores
do not run. A successful cancelling IRQ captures token and reported state but
does not run the later reload/selection/restore stage. Callback error wins over
cancellation. Terminal states are silent and invent no IRQ restore, core restore,
unmap, retry or rollback.

Completion flags describe returned child boundaries, including a real body
gate/mode skip. They do not prove mapping validity, restored interrupts or clock
readiness. Actual child observations/selection results remain separately
inspectable; `state_valid` only tracks current SI knowledge after IRQ calls.

Independent literal tests cover bus0 mapping order, cached/zero maps, ordinary
bus1 PCI/crystal paths, fast-map boundaries, signed revisions, capability/mode
skips, zero-token restore, changed post-disable index and rejected restoration.
Every selected callback covers error/cancel/both, partial state, ownership,
missing post-state and terminal silence. ASan/UBSan and ARMv7 compilation are
host checks; they establish no kernel linking, physical mapping or router test.

The WL caller's later byte-cache and inline reason0x10 update is a separate
[composed selection job](WL_CLOCK_MODEL.md). It calls no PSM wake helper on the
fallback branch; publishing that cache/reason is not proof of clock readiness.
