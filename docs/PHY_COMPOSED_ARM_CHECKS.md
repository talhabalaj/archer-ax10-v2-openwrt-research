# Composed PHY instruction comparisons

`tests/check_phy_composed_arm.py` compares selected actual ARM instructions
from the caller-supplied pinned `wl.ko` against the original C components.
It reuses `tests/check_phy_arm.py`'s ELF32 little-endian ARM ET_REL parser;
that harness and its separate 264 primitive comparisons remain unchanged.

Run from the repository root using Python with Unicorn **2.1.4** installed:

```sh
python3 tests/check_phy_arm.py /path/to/private/wl.ko
python3 tests/check_phy_composed_arm.py /path/to/private/wl.ko
```

The script enforces module SHA-256
`8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c`,
compiles a temporary C bridge with strict C11 warnings, and keeps extracted
instructions in memory. It exports no vendor code, arrays or captures.

Recorded host checks:

- **512 TSSI C differential cases:** 64 snapshot and 64 restore cases for
  each explicit PHY core count 1 through 4; compare ordered operations,
  access widths/barriers, all four saved slots, and bookkeeping.
- **270 table C differential cases:** 150 direct table-wrapper and 120
  high-selector PHY-read cases. Exercise both guard-bit paths, shared bus
  words 0/1/2, thresholds 0/1/2/3/65535, index truncation, full-word assembly,
  returned low-halfword, and counter state. Every sampled MAC control has
  bit `0x1` clear.
- **16 packed-write literal fixtures:** actual packed-write instructions
  checked against independent expected traces for threshold/wrap/flush cases.
  These are not C differential comparisons of a standalone packed primitive.
- **Seven rejection probes:** unselected table-write call, MAC-enabled PSM
  call, unapproved external branch, unresolved call relocation, and invalid
  dispatch entry, section symbol, or target.

The comparison executes ten selected actual functions: the AC TSSI callback,
PHY read/modify/write, table wrapper and extended wrapper, physical table
reader, STF getter, and HWACI disable/restore helpers. The exact HWACI helper
instructions return without device effects; no successful external-function
stub is inserted. The run exercises 5,960 resolved branch-site visits across
the differential cases.

Selected REL `R_ARM_CALL` and `R_ARM_JUMP24` relocations require the actual
named function symbol, matching section/value/size, ARM branch opcode,
addend `-8`, aligned target and encodable displacement. The only permitted
`R_ARM_ABS32` data relocations are the reviewed table reader's 57 dispatch
entries, referencing its own unnamed section symbol. Each entry must occupy
the exact checked dispatch range and target one of six reviewed aligned
instruction blocks inside that function. No general ABS32/global linker is
provided. Unknown executed relocations, calls, code, MMIO widths/locations,
context reads, or uninitialized stack reads fail closed.

Both C and ARM receive explicit synthetic PI/public revision-51 context,
core count, save buffer, bus field, threshold and read replies. Table cases
cover inactive-MAC branches only. MAC suspend/enable and the enclosing TSSI
MAC/PHY-entry/wake wrapper are not emulated. Successful table comparisons
reach the whole wrapper return, including temporary guard clear; a valid
partial data word alone does not satisfy completion. Backend errors and
cancellation remain covered by separate C tests, not these normal-return
instruction comparisons.

The emulator validates instruction-level software traces, not device timing,
real barrier effects, resource ownership, RF behavior or hardware readiness.
Behavioral provenance is in the [table-read audit](DRIVER_AC_TABLE_READ_AUDIT.md)
and [resource audit](DRIVER_RESOURCE_AUDIT.md). Relocation semantics and tool
provenance refer to Arm's [ELF32 ABI specification](https://github.com/ARM-software/abi-aa/blob/main/aaelf32/aaelf32.rst)
and the official [Unicorn 2.1.4 release](https://github.com/unicorn-engine/unicorn/releases/tag/2.1.4).
