# Stock microcode upload writer contract

## Scope and result

The exact stock `wl.ko` upload writers expose a small, deterministic software
write sequence suitable for an original offline trace simulator. This establishes
register accesses and source-word ordering. It does not establish hardware bank
contents, automatic address increment, execution, or readiness.

- Artifact: `wl.ko`, 5,803,056 bytes, ARM little-endian ELF32 relocatable object.
- SHA-256: `8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c`.
- REA `6.5.0`; Ghidra `12.1.4`; native macOS ARM64 decompiler; JDK 21.
- Main writer: `wlc_ucode_write`, analyzed address `0x0019f8fc`, 164 bytes.
- Extended writer: exact symbol search confirmed `wlc_ucodex_write`, analyzed
  address `0x0019f9a0`, 164 bytes.
- Existing attach/descriptor/selector observations were reused. No router action,
  target-code loading, driver execution, or hardware access occurred.
- Raw evidence is private. Evidence export and `close_binary` succeeded; the
  source digest remained unchanged. This document contains original selected
  observations, with no microcode bytes or wholesale pseudocode.

## Core-130 register resolution

The earlier core selector selects descriptor address `0x003a3f88`. Its first
pointer is `0x003a487c`. Both writers obtain 16-bit register offsets from this
nested descriptor, using fields `0x34` and `0x36`. A new read of those fields
resolved them to `0x0160` and `0x0164`, respectively.

Thus, for this core-130 descriptor:

| Role | Offset from caller's D11 register base | Access width |
| --- | --- | --- |
| Object address/control | `0x160` | 32 bits |
| Object data | `0x164` | 32 bits |

Both upload writers use these same two ports. Main and extended selection differ
in the object-address value: `0x03000000` versus `0x03080000`, a difference of
bit 19 (`0x00080000`). Calling the latter the extended/PSMx bank follows the
writer's role; these functions alone do not establish the register bitfield's
complete hardware semantics or address-unit behavior.

Descriptor-field evidence:
`ev_c7a806df4445457fcf4e4146375b55a3c001032dd3862f9ebee30e0705cc8c72`.
Reused selector/pointer evidence:
`ev_9dbe57e9a416183dae3bc38ee22556593edd481aab909f720000b0c0246db4bb`
and `ev_1586c82b5f5725d0c4a6f51d3761fdbc2e1bb4a03454b82de0dfef1c8b2110af`.

## Exact writer sequence

Both writers first read the global `ucode_chunk` and compute
`word_count = unsigned_byte_length >> 2`.

If `ucode_chunk == 0`, the sequence is:

1. `DSB ST`.
2. Write 32 bits to register-base plus `0x160`: `0x03000000` for main microcode,
   or `0x03080000` for extended microcode.
3. Read 32 bits from that same address register.
4. `DSB SY`.

The readback value is discarded through a temporary stack slot. There is no
comparison, retry, or assertion against the selected value. This read is visible
in assembly and absent from the recovered pseudocode, so the assembly controls
the trace contract.

For each of `word_count` source words, in order:

1. `DSB ST`.
2. Load one native 32-bit source word and advance the source pointer by four.
3. Write that word with a 32-bit store to register-base plus `0x164`.

The software repeatedly writes the same data-port address; it does not increment
the MMIO address. There is no final barrier or readback after the last data-port
store in either writer. The loop contains no readiness poll or error check.

For this ARM little-endian Program, the source load consumes a little-endian
32-bit word. The inspected instructions contain no software byte swap or
alternative packing. A portable offline simulator can reproduce those word
values with an explicit little-endian decode. This is a software value contract,
not an independently measured physical bus byte-order specification.

Length behavior matters:

- The length argument is a 32-bit unsigned byte count.
- Only `floor(length / 4)` words are uploaded; one to three trailing bytes are
  ignored, without padding or a remainder error.
- With fresh-chunk state and length below four, address setup and readback still
  occur, but no source data word is loaded or written.
- There are no null-pointer, source-bound, alignment, or overflow validations in
  the selected writer bodies. A simulator may add validation, but it must identify
  that as its own policy.

If `ucode_chunk != 0`, both writers omit the initial address setup, readback, and
associated barriers while retaining the per-word loop. This analysis does not
establish the required preceding state for such a continuation. Direct reference
search found the two observed reads, one per writer. Neither writer changes the
global. A fresh-chunk-only simulator that requires an explicit zero value and
rejects nonzero values has a narrower, defensible scope.

| Observation | Evidence ID |
| --- | --- |
| Main writer pseudocode | `ev_7a136b0e58daa470b8ed8ad6993726e032a8965eb103dfb6fdae98d1c0a78ab1` |
| Main writer assembly, including readback | `ev_20aa19ccfd016073b6f893c31e353ec221858f52c76da22f0774901f1dd7edc2` |
| Extended writer pseudocode | `ev_b6e8debdd89d6770e35471d7dd46761271f33512c55d8d09ca302dd81e1fb0de` |
| Extended writer assembly, including readback | `ev_113fffd811c1d544eec02001cc9899a84cd5206c8ab25ff91a1e19eee3ca3a00` |
| `ucode_chunk` resolved references | `ev_776cf1db0cdb91bb461a03846c68366ee5f9f65f92c333bab5cd0610431b4b76` |

## Immediate wrapper ordering and flags

Fresh assembly of `wlc_ucode_download` confirms the previously recovered selector:
every core-130 image pair invokes `wlc_ucode_write` first and
`wlc_ucodex_write` second. This applies to the minor-1/non-minor-1 and
boardflags-bit-0 clear/set combinations identified in the attach audit.

If the byte at recovered hardware-object offset `0x186` is already nonzero, the
core-130 path returns zero before either writer. Otherwise, after the extended
writer returns, the wrapper sets that software byte to one, calls
`wlc_bmac_autod11_shm_upd`, and returns zero.

The SHM-update helper selects the standard shared-memory descriptor family and
publishes its pointer to common and PHY state. Its direct code contains no
hardware start or readiness loop. The inspected wrapper and writer sequence
contains no validated hardware-ready status. Setting the software byte therefore
does not attest that microcode was accepted or started. Downstream PHY setter
internals and later BMAC start/readiness paths were not expanded in this session.

Both writers return `void` and contain no recoverable failure result, rollback,
buffer clearing, flag clearing, or cleanup path. The wrapper has no writer-status
check. Injected callback failure in an offline simulator is an analyst-defined
behavior; preserving the already-recorded prefix without inventing rollback or
clearing is consistent with the absence of a stock rollback sequence, but it is
not a recovered stock exception-handling contract.

Wrapper assembly evidence:
`ev_8878a904df47294b4350735409eb1a530a950af82fb0d7f8ea4001806caf2c30`.
Post-upload SHM-update evidence:
`ev_ff5f05f5eb8af8b8787a12f9c6ce2c9e43b1b9a23f6b8728be02f559b8bba3c9`.
Reused image-selection pseudocode:
`ev_7f48cfafb1f084d3640a7f70a42b4f8be443dd4bc791c1e10fe551ed62a828ab`.

## Configurable model assumptions and limits

An offline implementation can faithfully record barrier, selector-write,
address-readback, and data-write events. It should keep the following separate
from those observed events:

- The supplied register base and descriptor are simulator configuration.
- Readback values are supplied by the fake bus; the stock writers do not compare
  them or require a particular value.
- Mapping repeated data-port stores into fake-bank words, cursor advancement,
  address units, capacity, and wrap behavior are model choices. The writers prove
  repeated stores and selector constants, not those hardware consequences.
- A generic status poll with caller-configured register, mask, expected value,
  and fake clock is a separate simulator feature. No default ready register,
  timeout, start bit, or ready mask is established by this audit.
- Error injection, input validation, allocation failure, partial-trace retention,
  and rollback policies belong to the original offline implementation. No state
  should be described as cleared by the stock writer on a failure path it does
  not expose.

The query boundary was the two writers, their direct references, core-130 port
descriptor fields, the image-selection wrapper's assembly, and its immediate SHM
update helper. Reset/start/calibration, DMA, interrupts, hardware acknowledgement,
and real radio operation remain unverified.
