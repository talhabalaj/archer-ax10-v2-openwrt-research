# Actual ARM wake-policy comparisons

`tests/check_wake_policy_arm.py` compares `driver/wake_policy.c` with the
actual pure stay-awake functions from the caller-supplied pinned private
`wl.ko`. It executes five real functions: `wlc_stay_awake`, the RRM wrapper
and body, the association getter, and the hardware wake-override getter.
No external function is replaced with a successful stub.

Run from the repository root with Python using Unicorn **2.1.4**:

```sh
python3 tests/check_wake_policy_arm.py /path/to/private/wl.ko
```

The script reuses `tests/check_phy_arm.py`'s bounded ELF32 little-endian ARM
ET_REL parser and enforces module SHA-256
`8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c`.
The existing primitive and composed PHY comparisons remain separate; the composed
bridge additionally links this evaluator for its PSM child's new dependency.
A temporary strict-C11 bridge checks C structure layouts/field enums and calls
the actual C evaluator. C source/header and module digests must stay unchanged
during the run. Extracted instructions remain in memory, with no payload output.

Recorded host checks:

- **373 normal comparisons:** exact Boolean decision and complete ordered
  policy-context load trace, including field, slot, pass and access width.
  Cases include each early flag, optional-object guards, full-word high bits,
  unsigned RRM/nested thresholds 3/4, 256 deterministic mixed-state cases,
  every BSS slot matching each of three passes, and primary-only pass-3 matches.
- **218-load longest consistent case:** all 16 entries refer to the same
  coherent primary BSS object; all optional global branches are traversed,
  all three BSS passes complete, and association state zero permits sleep.
- **139 missing reached fields:** the C dependency access prefix exactly
  matches the ARM emulator's rejection prefix, including the attempted missing
  load. Unreached missing fields also appear in normal short-circuit cases.
- **Three call/relocation rejections:** an unauthorized external branch,
  a denied selected relocation, and an unsupported executed relocation type.
- **Eight known-null required-pointer probes:** C returns its explicit
  `INVALID` result; ARM emulation fails closed at the stock's unguarded
  dereference. This is not a claim that stock returns an error for null input.

The comparison records every non-stack load by the selected ARM functions,
mapped to the C evaluator's explicit logical access trace. It verifies that
`W+0x1e0` is loaded once, all 16 array pointers are reread on each completed
pass, and `W+0x1e4` is reloaded only for qualifying pass-2 entries. No policy
loads are hoisted, fused or omitted in that comparison. Stack saves/loads are
checked for initialization but excluded from the policy trace. The C model's
availability metadata has no ARM field counterpart; this is not a comparison
of the host C compiler's machine-load instructions.

Synthetic W/public/HW, optional nested/RRM/association objects, BSS array,
primary identities and object state are explicit immutable inputs. Consistent
repeated identities share actual emulated object storage. Reached unknown
context, unexpected widths, state writes, barriers, uninitialized stack reads,
unknown code or indirect calls are rejected. Only four actual selected REL
`R_ARM_CALL`/`R_ARM_JUMP24` records are resolved, requiring matching named
function symbols, checked bounds/alignment, ARM branch encoding, addend `-8`
and valid displacement. No ABS32/global relocation resolver is enabled.

These are pure software-policy tests. They do not execute the public-up gate,
main-control MMIO, mctrl, wake wait, PSM lifecycle, runtime acquisition,
synchronization or hardware readiness. Public-up and current post-clear
wake reasons remain separate integration responsibilities. No device, kernel
module or radio code is run outside the isolated instruction emulator.

Behavioral provenance is in the [assembly-backed policy audit](DRIVER_WAKE_POLICY_AUDIT.md).
Tool and format references are the official [Unicorn 2.1.4 release](https://github.com/unicorn-engine/unicorn/releases/tag/2.1.4)
and Arm's [ELF32 ABI specification](https://github.com/ARM-software/abi-aa/blob/main/aaelf32/aaelf32.rst).
