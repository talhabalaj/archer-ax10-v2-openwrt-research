# Verification loop toward a working driver

The exit condition is a working exact-board replacement driver and modern
OpenWrt port, as defined in [the roadmap](../ROADMAP.md). Component tests,
compilation, software flags and a completed synthetic trace are narrower
observations. The loop must advance the first required functional dependency,
then verify the composed path again. It cannot stop at a larger test count.

## Execute, implement, verify, repeat

1. Run the full-target functional probe below. Read its actual terminal state,
   completed required callbacks and first unresolved child or missing input.
   Preserve the complete sixteen-stage registry; no selected-stage shortcut
   establishes target initialization.
2. Recover that child's exact current source and runtime/resource inputs. Reuse
   retained evidence; pin any new analysis and export/close the session.
3. Implement the real operations and descendants. Legitimate typed OS services
   need actual adapters. An opaque vendor callback returning success does not
   close its required child.
4. Have an independent verifier check the instructions/contracts and expected
   trace. Exercise fresh reads, discarded observations, software commitment,
   malformed inputs, timeout, error, cancellation and partial state. Compare
   actual pinned ARM instructions where caller closure permits it. Unknown calls
   or unsupplied reads must fail, rather than receive implicit success or zero.
5. Rerun the full target. A new completed required stage is functional progress
   for the same fixture. More operations inside the same unresolved stage, a
   larger test count or a changed fixture does not prove that advancement.
6. At a reviewable checkpoint run the full sanitizer/build/private differential
   checks, inspect the exact source state and publish under the existing policy.
   Keep the first unresolved functional gate explicit and continue the loop.

If the fixture or oracle changes, independently justify that change against
source evidence. Do not adjust expectations just to turn an incorrect result
green. Error/cancel and unknown-state restart behavior are part of the contract.

## Commands and machine-readable results

The fast iteration command compiles the current C sources with ASan/UBSan and
executes the entire synthetic target PHY registry against an independent trace.
The baseline scenario explicitly supplies identity50e0,5GHz20MHz/channel36; it
does not infer the physical board's radio identity or bypass the radar band guard:

```sh
python3 scripts/verify_driver.py --scope target
```

For checkpoint verification, additionally run all runtime tests, strict ARMv7
compilation and optional caller-supplied stock/ARM comparisons:

```sh
python3 scripts/verify_driver.py --scope checkpoint \
  --module /private/path/to/wl.ko \
  --unicorn-python /private/path/to/venv/bin/python3 \
  --report-dir /private/path/to/new-verification-run
```

The interpreter must contain Unicorn2.1.4; the tool does not install anything.
Use an explicitly supplied pinned private module. The report directory must
be new and outside the checkout. Without `--report-dir`, an external temporary
directory is created. Binaries, private diagnostics and `report.json` stay there.
No target module, payload or board data is written into the public repository.

Pass `--previous-report /private/path/to/previous/report.json` to compare actual
gate observations. The report records the commit, dirty state and content
fingerprints of sources, fixtures and verification code. A change during checks
invalidates that run. Fixture changes are reported as requiring review, rather
than automatically counted as functional progress. A change of terminal child
inside the same stage is exposed for review; more callback events alone do not
count as a newly completed required stage.

Exit codes have explicit scope:

| Code | Meaning |
| --- | --- |
| 0 | The requested **offline** gates completed. This cannot establish working hardware or complete driver integration. |
| 1 | A compile, regression, independent trace, input identity or observation check failed. Inspect its private log and fix the cause. |
| 2 | The requested offline proof remains incomplete: a required child/input or optional proof is missing. A correctly detected dependency may coexist with passing regression tests. |

The report separates `checks`, `target_observation`, `functional_gates` and
`driver_working`. A dependency is an observation of incomplete functional work;
it is never promoted to success because a regression test expected it. Unknown
kernel or device behavior is not accepted from a user-supplied success flag.
The current offline tool always retains unverified kernel/device gates.

## Gates beyond the synthetic PHY vector

| Gate | Evidence required |
| --- | --- |
| Complete PHY vector | All sixteen actual callbacks return in order, full independent trace consumed, final return0 and slice software store reached. Still synthetic; not radio readiness. |
| Enclosing initialization | Actual attach, reset, upload, start and BMAC initialization compose all required children with real runtime bindings and ownership. |
| Receive delivery | Allocation/fill, DMA completion, status merge, BMAC/header parsing and a valid frame delivered through the chosen kernel networking interface. |
| Transmit completion | Normal packet transmit, hardware completion and correct reclaim/lifetime, beyond the descriptor-only leaf. |
| Kernel integration | Reproducible target-kernel build and verified resource acquisition, mapping, DMA/coherency, IRQ, locking, error and teardown behavior. |
| Exact-board operation | Actual identity/calibration inputs, physical reset/start, valid RX, calibrated TX/association/security, recovery and stability evidence. |

The existing instruction prohibits router/hardware/RF operations. This loop
performs none. Device evidence therefore remains unverified while offline
source closure, kernel implementation and isolated validation continue. A
future hardware loop requires an explicitly authorized exact-board test setup
and the actual recovery/resource/regulatory prerequisites; a host report cannot
substitute for that evidence.

The three concurrent implementation scopes remain radio power, PHY noise/channel
and receive resources. Each worker's output is checked independently and joined
into the same functional path. New public components must move that path closer
to the full objective; they cannot redefine the objective around an isolated
passing branch.
