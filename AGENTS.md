# Continuation instructions

## Read before changing this repository

1. Read [PROGRESS.md](PROGRESS.md): current status, baseline and exact next task.
2. Read the relevant milestone in [ROADMAP.md](ROADMAP.md).
3. Read the linked evidence and [driver/README.md](driver/README.md) before driver
   changes. Read [PUBLICATION_POLICY.md](docs/PUBLICATION_POLICY.md) before staging.

The active priority is offline BCM63178 driver development. The immediate next
milestone is D1 reset/start/hardware acknowledgement unless PROGRESS.md has
advanced it. Pending UART/recovery does not block offline analysis/simulation.

## Preserve scope and evidence

- Target AX10 hardware v2.0, BCM63178, core 130.1/PHY 51. Do not transfer claims
  or firmware across AX10 revisions. Keep 2.4 GHz BCM43217 work separate.
- Treat the exact input hashes and tool versions in PROGRESS.md as provenance.
  Reuse retained analysis; do not repeat installation or decompilation without
  a changed question, missing evidence or a concrete tool failure.
- Keep observations, inferred contracts, configurable model choices and hardware
  test results distinct. A simulated `TRANSFERRED` state or a software downloaded
  flag does not prove radio readiness.
- Keep operations bounded and failures/cancellation terminal as documented.
  Preserve callback-triggered cancellation regressions and independent trace
  oracles. Never invent hardware cleanup or silently substitute board defaults.
- Current components have no MMIO backend or kernel binding. Hardware changes,
  module loading, reboot/recovery and flashing are separate work with their own
  technical prerequisites. Use the user's existing authorization; do not request
  repeated permission for ordinary authorized offline work or scoped publication.
- Preserve unrelated work. Inspect Git status and fetch/check remote main before
  publication; never overwrite a concurrent change or force-push by default.

## Required checkpoint updates

For every substantive investigation or implementation change, update PROGRESS.md
in the same change with:

- What changed and the milestone state actually reached.
- Evidence/report links and exact validation performed, including limitations.
- Unresolved defects/dependencies and the next concrete work package.
- Input identity or architecture decisions that changed.

Update ROADMAP.md when scope, dependencies or acceptance criteria change. Do not
mark hardware work complete from host tests, or mark a milestone complete just
because a stub compiles. Documentation-only changes need link and consistency
checks; unchanged driver tests do not need to be rerun solely for prose edits.

## Testing and publication

Run `sh scripts/test_driver.sh` for synthetic ASan/UBSan checks after relevant C
changes. Use its optional private module argument for affected stock-data paths.
Cross-compile changed C components for ARMv7 when applicable; report compilation
separately from Linux integration or device execution.

Only original code/tests, research, reviewed Evidence summaries and portable
metadata belong here. Keep firmware, arrays, calibration/NVRAM, raw captures,
credentials/keys, tool installations and local host paths outside this public
checkout. Use caller-supplied private inputs; public summaries do not replace raw
evidence. Stage an exact allowlist, inspect the diff, run publication checks and
verify the pushed commit. Continue to maintain the checkpoint after publication.
