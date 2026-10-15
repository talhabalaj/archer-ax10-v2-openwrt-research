# Current stock wireless binary audit

Checked 2026-10-15 using read-only acquisition and static ELF metadata inspection.
Target binaries stayed local and were not executed on the analysis host.

## Artifact identity

The current `wl.ko` has SHA-256
`8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c`
and is 5,803,056 bytes. It is ELF32 little-endian ARM EABI5, relocatable,
and not stripped. The live driver reports version 17.10.121.37; the module's
allowlisted metadata does not contain a version field.

Static metadata declares:

```text
license: Proprietary
depends: hnd,emf,cfg80211,archer,bcmlibs,pktflow,igs
vermagic: 4.1.52 SMP preempt mod_unload ARMv7 p2v8
```

The symbol tables contain 6,952 unique defined function names and 438 unique
undefined symbol names. Undefined names include kernel and vendor/BSP linkage;
this count does not establish the number of missing OpenWrt functions or the
amount of code needed for a port.

Named analysis entry candidates include `wl_cfg80211_attach`, `wl_ioctl`,
`wlc_attach`, and `wlc_iovar_op`. A symbol name establishes an analysis entry,
not its behavior. Ghidra function analysis is a separate evidence step.

The [module metadata inventory](../findings/MODULE_METADATA.json) covers the
wireless module, host controller, Ethernet and selected dependencies. It emits
only filenames, digests, sizes, ELF identities, selected build metadata and
symbol counts. It omits arbitrary strings, module parameters, runtime addresses
and complete symbol names.

## ABI and kernel boundary

The current binary depends on vendor packet acceleration and shared-library
interfaces in addition to cfg80211/kernel interfaces. The official GPL archive's
older `wl` has a different dependency set (`wfd` in place of several current
interfaces). Same-kernel vermagic is not proof that these releases can be mixed.

Loading this module into a modern kernel is not established. Kernel structures,
cfg80211 contracts, DMA/interrupt behavior, OS abstractions and vendor interfaces
need source or separately verified compatible implementations. Neither adding
a PCI ID nor suppressing version checks supplies that compatibility.

## REA status

REA 6.5.0 with a task-local Ghidra 12.1.4 engine and an existing JDK 21 is ready.
The native macOS ARM64 decompiler was built from Ghidra's bundled source, and
the provider-scoped readiness check passed. The first two completed native
function queries are documented in [REA_INITIAL_FINDINGS.md](REA_INITIAL_FINDINGS.md).
The metadata findings
above come from the standard-library ELF inspector, not from REA decompilation.

No new hardware driver, runtime radio behavior, RAM boot, current-kernel board
boot, or flashable image has been validated.
