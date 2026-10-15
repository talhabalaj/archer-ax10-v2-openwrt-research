# Assembly closure of the stay-awake policy

Original sanitized behavioral ledger for exact `wl.ko`, 5,803,056 bytes,
SHA-256 `8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c`.
REA 6.5.0/Ghidra 12.1.4 expanded four missing bounded policy questions in
the already open PHY-reset/BF session. A fifth assembly record follows the
RRM entry's single tail branch to its actual body. Existing wrappers/mctrl/
wake-poll evidence was reused. The session was exported and closed; the exact
artifact digest was checked again. No target/device execution or public edit.

Actual Evidence IDs are in [selected Evidence summary](../findings/DRIVER_WAKE_POLICY_EVIDENCE.json). This closes
the pure decision algorithm at its real code boundaries. Acquisition, validity,
ownership and initialization of policy state remain runtime integration work.
Raw pseudocode/disassembly and host paths are excluded from this report.

## Distinct outer gate and hardware action

Retained `wlc_set_wake_ctrl(W)` first checks public byte `*(W+0)+0x1c`.
Zero is a real wrapper return with no policy evaluation, MMIO or cache change.
A valid public pointer is still required; absent context is not a zero byte.

Nonzero enters the retained action body: read D11 main control `0x120`, SY,
then evaluate `wlc_stay_awake(W)`. True calls mctrl with wake mask/value
`0x04000000` and calls retained wait-for-wake only when the original hardware
sample lacked that bit. False calls mctrl with the same mask and value zero,
without a wake wait. Changed-cache, mute/override and mirror rules still
apply. The policy is not inferred from cached wake state, and an unchanged
cache does not eliminate the original-sample test for a subsequent wait.

## Exact global short-circuit order

The new assembly of `wlc_stay_awake` at `0x149608` confirms the following
ordered positive predicates. Any positive predicate returns 1 before later
loads/calls or BSS scanning. Unnamed host fields retain neutral offset names;
they are not MMIO, invented feature names, or default reset values.

| Order | Positive predicate |
|---|---|
| 1 | byte `W+0x19c` nonzero |
| 2 | byte `W+0x19b` nonzero |
| 3 | pointer `W+0xb4` exists and its byte `+0x0c` is nonzero |
| 4 | pointer `A=*(W+0x2fc)` exists; load `C=*(A+4)`; unsigned word at `C+0x0c` is greater than 3 |
| 5 | actual `wlc_rrm_inprog(W)` returns nonzero |
| 6 | actual `wlc_assoc_get_as_cfg(W)` returns nonzero |
| 7 | byte `W+0x199` nonzero |
| 8 | byte `W+0x19a` nonzero |
| 9 | byte `W+0x198` nonzero |
| 10 | byte `W+0x300` nonzero |
| 11 | byte `W+0x336` nonzero |
| 12 | word `W+0x568` nonzero |
| 13 | word `W+0x188` nonzero |
| 14 | actual `wlc_hw_get_wake_override(*(W+0x18))` word nonzero |
| 15 | word `W+0x1a0` nonzero |

The nested pointer in predicate 4 is not separately null-guarded once A
exists. All byte predicates accept any nonzero byte; word predicates test
the full 32-bit word. The threshold is unsigned: 3 fails, 4 and high-bit-set
values pass. No scalar field should silently default to false because the
operation is initial reset. Later state need not be acquired for an earlier
positive short circuit.

## RRM helper: exact argument, fields and guards

Stay-awake passes W itself to entry `0x22b418`, a tail branch to actual body
`0x22b3e8`. The body first reads public byte `*(W+0)+0x58`. If zero it
returns zero without reading the RRM pointer chain. Otherwise it loads:

- `R=*(W+0x530)`;
- `S=*(R+0x14)`;
- unsigned word `S+0x0c`.

It returns 1 when that word is greater than 3, otherwise 0. There are no null
guards on R or S, no additional callees, no state stores, no MMIO and no
barriers/delays. Thus a pure model can represent these actual state inputs
instead of injecting a final unexplained RRM Boolean. It must distinguish
the public-byte-zero short circuit from missing reached objects.

## Association and wake-override getters

`wlc_assoc_get_as_cfg` at `0x19ebf4` is exactly three 32-bit loads:
`A=*(W+0x22c)`, `B=*(A+4)`, then return `*(B+0)`.
It does not scan configurations, classify association states, or check either
pointer for null. Stay-awake only tests the returned full word for nonzero.
The model therefore needs valid reached pointer bindings and the selected
word/object identity, rather than a guessed association-in-progress algorithm.

`wlc_hw_get_wake_override` at `0x1e8d40` only returns the 32-bit word at its
hardware-context argument `+0x1f8`. This is the retained PSM wake-override
word; no transformation, low-byte truncation, new I/O or side effect exists.

## Three separate 16-entry BSS passes

If every global predicate is false, assembly reads the array pointer at
`W+0x1e0`, visiting its 16 pointer entries at offsets `0x00` through `0x3c`
inclusive. The array itself has no null guard. Each pass restarts from entry
zero and returns 1 at its first matching entry. An entry B is matched as below:

| Pass | Ordered matching requirements |
|---|---|
| 1 | B nonzero; byte `B+6` nonzero; byte `B+4` nonzero |
| 2 | B nonzero; byte `B+6` zero; byte `B+8` nonzero; B differs from the full pointer at `W+0x1e4` |
| 3 | B nonzero; byte `B+6` zero; byte `B+8` nonzero; byte `B+0x10` nonzero; pointer `A=*(B+0x98)` nonzero; 32-bit state `A+8` neither zero nor `0x1c` |

Pass 3 does not exclude the primary pointer. A primary B can fail pass 2 and
still select awake through pass 3. Missing pass-3 association subobject skips
that entry; it is not state `0x1c`. The state tests use the whole 32-bit word.
Only after all three passes have no match does the function return zero.

These passes must not be fused if exact state-access order is part of the
contract. Valid array storage, optional-entry presence, pointer identities,
object lifetimes and synchronization are explicit caller requirements.

## Closed algorithm versus remaining integration

The four recovered policy functions and the RRM tail target perform only
software reads and pure branches; no helper callbacks, hidden scans beyond
the three passes, register reads, writes, timing loops or new dependencies
remain inside this decision algorithm. The full original action still has
the separately recovered main-control read, mctrl and conditional wake wait.

This evidence permits a pure policy evaluator from explicit raw fields and
optional objects. Missing reached state must remain a dependency/error rather
than an invented zero result. Public-down is a separate established no-op
that does not require these policy inputs. Integration must bind policy's
override getter to the current full PSM reason word, preserve short circuits
and primary identity, and retain actual state/ownership provenance.

No hardware-ready claim follows from policy true, a mode/cached flag, or host
simulation. This pass did not acquire runtime BSS, association, RRM or public
state and does not complete the full driver.
