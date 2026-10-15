# Static bootloader REA audit

## Result

The supplied bootloader contains a conditional rejection of host boot, registered
RAM-loading command handlers, and a separate policy gate that can suppress those
commands. Their presence does not establish their availability on an installed
device. Active security state, console availability, and successful RAM execution
remain unverified.

This was offline static analysis only. No target code, UART command, router
action, firmware update, or flash operation was executed. The private REA session
was closed successfully after exporting its evidence bundle.

## Artifact and interpretation

- Derived artifact: decompressed `fs-uboot` payload, 341,024 bytes.
- SHA-256: `1a9ca1a9e8f52e6e5a6ef655fb255c63c08584b3972c3e33b33d9c12a4311487`.
- Caller-selected interpretation: `raw-binary`, ARM, little endian,
  `ARM:LE:32:v7`, compiler specification `default`, mapped entry `0x00f00000`.
- Tools: REA `6.5.0`, Ghidra `12.1.4`, native macOS ARM64 decompiler, JDK 21.
- REA independently verified complete original/modified source digests and the
  initialized, loaded, non-overlay mapping `0x00f00000..0x00f5341f` inclusive.
  This verifies the selected mapping; it does not independently identify a raw
  file's architecture, entry, or physical boot behavior.

Mapping evidence:
`ev_7636544e40daa9aaf63468bcf57d0f5861633602ac0b6f795fd5256ab2a21fd0`.

The supplied extraction provenance describes a BRCM header at firmware offset
`0x2014`, load/entry `0x00f00000`, flags zero, and a length-82,309 LZMA payload
starting 20 bytes after that header. REA analyzed the already-decompressed
artifact; it did not re-establish this extraction chain or identify the installed
router's loader. A live DTB version field alone does not prove that identity.

## Secure-mode host-boot rejection

The rejection text at file offset `0x3ccec` / mapped address `0x00f3ccec`
has a Ghidra reference at `0x00f0c1ec`, inside `FUN_00f0c104`.
The text states that secure mode disables booting from a host.

Observed pseudocode for this function distinguishes an `'f'` branch from other
source modes. The other branch calls `FUN_00f02cfc`; a nonzero result prints the
rejection and returns `-1`. A zero result permits the subsequent host-loading
branches. This is a conditional check in the boot path, not a determination of
the device's current state.

`FUN_00f02cfc` reads indices `0x11`, `0x12`, and `0x17` through
`FUN_00f02ac0` / `FUN_00f02af4`. It returns one only when the reads report success
and the values satisfy masks `0x8`, `0x38000`, and `0xffff`, respectively.
The reader accesses MMIO registers in the `0xff802800` region. Identifying the
physical fields and active fuse values requires an authoritative register layout
and device evidence; raw static values do not supply either.

| Observation | REA Evidence ID |
| --- | --- |
| Rejection string reference | `ev_42bae08beb533626edbdd973a634b9d8ff82b479c52479e7c38ddcab9d0fdf7d` |
| Host-boot function pseudocode | `ev_964de72c09ee03ed29ffe5c1eeb2562881309bc803e9ff22e73d3bb4722d390c` |
| Host-boot dossier with typed references | `ev_2da259f9dadbbf5b4256986d03dc77883b1b1e04cb01594f1d70f5a6b141080d` |
| Secure predicate pseudocode | `ev_19c54691e54af57ac0efbdeffed48de4eb76bcad2999b97d4802d366247fea0a` |
| MMIO reader pseudocode | `ev_1560b9eec995ee57dfec890060f13f5945cda54ba148634296884a5dcafe839d` |

## Command bindings and policy gate

References to the `r` help text at file offset `0x3d764` and `loadb` help text
at `0x3d854` occur inside the same registration function, `FUN_00f0e3a4`.
Its registrations bind:

| Command | Handler | Observed role |
| --- | --- | --- |
| `r` | `0x00f0c380` | Parses source options, then calls the host/flash run function `0x00f0c104` |
| `loadb` | `0x00f0de6c` | Selects serial, network, or boot-filesystem binary loading |
| `go` | `0x00f0e218` | Parses an address and enters an architecture handoff helper |

All three registrations pass a final flag of zero to `FUN_00f183dc`.
That wrapper registers a command only if `FUN_00f183a0()` is false or the final
flag is nonzero. `FUN_00f183a0` wraps `FUN_00f017f0`, which tests whether
`DAT_00f5342c == 1`. The tested global lies beyond the file-backed mapping.
Consequently this analysis cannot determine whether these commands are actually
registered during an installed device's boot, how that flag is initialized, or
whether a console can be reached or interrupted.

| Observation | REA Evidence ID |
| --- | --- |
| `r` help reference | `ev_21d297f904502fca60fca5f917dbce714c7407ea8cdff0d86ebddb153a3b566f` |
| `loadb` help reference | `ev_80e81f57865b843523a5ed08df3fa7d4c9966c8e6f0c25f83ca1659f4298c7b6` |
| Command registrations | `ev_b7260a390c590b2d420fe340670fb9d5b428958481071a83e501750e86448c3c` |
| Registration wrapper gate | `ev_2a1856059e405d5cceda217e931dfeda7f50c2b9c16e9d71eb31ba22126e3c65` |
| Policy flag test | `ev_b9d324e90ecdc5764cd33c1c2ad81f75bed295e7af1cf67e21a3899eec9368d7` |

## RAM-load and execution paths

The selected `loadb` handler compares its option with `'s'`, `'n'`, and `'f'`:

- The serial option prints that a KERMIT build must be enabled and returns an
  error. The presence of a serial option in help does not establish a working
  serial loader in this image.
- The network option builds a host/file selector, chooses a caller-parsed or
  default destination, and calls `FUN_00f0adb4`. That wrapper selects the literal
  `tftp` loader, supplies destination and size information with flag `4`, and
  invokes `FUN_00f1850c`. The dispatcher contains a chunked read loop that
  advances its destination and accumulates the loaded byte count.
- The boot-filesystem option obtains a file buffer, copies its bytes to the
  selected destination, and conditionally performs a cache-related operation.

No call to the host-boot predicate is visible in this handler. This observation
does not establish that the command is reachable or that all lower layers permit
the operation in an installed device's security state.

The `r` handler reaches the separate secure-checked run function described above.
The `go` handler passes a parsed address to `FUN_00f00e74`. ARM assembly shows
that helper preserving the value in `r4` and entering `FUN_00f22dfc`, where an
`SMC #0` occurs. The selected pseudocode does not reliably model that monitor
transition or establish its eventual execution target and authorization. No
secure-boot bypass or successful arbitrary RAM execution has been demonstrated.

| Observation | REA Evidence ID |
| --- | --- |
| `r` handler pseudocode | `ev_b4fac25413be67d31c077669c531cfe67e14969f12c9d2701b07a45359a8dbc1` |
| `loadb` handler pseudocode | `ev_c30096df94a54a760ca3763029c455c332bcc84aabeef7d9fdc6897d9892a4ec` |
| TFTP load wrapper pseudocode | `ev_4d51fdde5d576b027375235685f8609451373b2ff7cc82e38f356dab7430dbb7` |
| Loader dispatcher pseudocode | `ev_2ab197d07a4b957735b39d6888ce72902f607492b2d7dddde85519e0f706a9b5` |
| `go` handler pseudocode | `ev_1dc6596c9679ee40935842478c00a1da1d1e4415ae40a3f55c344a37bf6d24ab` |
| Handoff helper assembly | `ev_680c234160cd2b1a84ab4ea5486d4c3f0ee6ccc3a2cb7e1ee1251e54eaf6e0f4` |
| Monitor-transition assembly | `ev_c0d9a59d5cebaf6f3242a3eac74fa2cf5a7b239ac20dae5a542eaf2e7ab6bb96` |

## Limits and next evidence

Function names and types are Ghidra recovery results, not original source.
Runtime globals, MMIO, monitor transitions, console dispatch conditions, and
installed-image identity remain outside this bounded static inspection.
The header's selected addresses do not establish a safe RAM placement plan for a
kernel, root filesystem, DTB, or reserved-memory regions. No translation of
legacy image-tag addresses into modern physical load addresses is inferred.

Further progress requires bounded evidence of the actual boot console and
installed loader identity, plus platform memory/boot conventions. Any UART or
RAM-boot experiment is a separate action that has not been performed here.
Raw evidence and proprietary payloads remain private; this document contains
selected original analysis notes and evidence identifiers only.
