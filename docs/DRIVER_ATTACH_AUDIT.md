# Stock driver attach and initialization audit

## Result and scope

The inspected driver contains a concrete path for D11 core revision 130,
PHY type `0xb`, and PHY revision 51: core-specific register descriptors lead to
a hardware PHY-version read, the decoded type selects `phy_ac_attach`, and
core 130 selects the AX51 MAC initialization tables. Microcode selection also
depends on the core minor revision and board flags. Chip number 63178 alone
does not establish the PHY revision, radio identity, or complete initialization
sequence.

This was bounded offline REA/Ghidra analysis of a stock module. No driver was
loaded, no router was contacted, and no MMIO or radio operation was performed.
Raw analysis and the exported evidence bundle remain private. `close_binary`
succeeded, and the source artifact digest was unchanged.

- Artifact: `wl.ko`, 5,803,056 bytes, ARM little-endian ELF32 relocatable object.
- SHA-256: `8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c`.
- Tools: REA `6.5.0`, Ghidra `12.1.4`, JDK 21, native macOS ARM64 decompiler.
- Comparison tuple from the existing board audit: wl0 chip `0xf6ca` / 63178,
  D11 core 130.1, PHY type `0xb`, PHY revision 51.0. Those are earlier stock
  observations; this offline run did not independently reread hardware values.
- Existing `wlc_attach` evidence was reused, not regenerated:
  `ev_d30632c25057d25aa2675b4054046652815edaaf6a4a605e3c532265732674aa`.

Recovered structure offsets and names below are analysis observations, not a
published C ABI. Relocated Ghidra addresses identify this analysis Program and
must not be used as physical device addresses.

## Hardware identity to PHY dispatch

1. The reused `wlc_attach` calls `wlc_bmac_attach` and takes its failure path on
   a nonzero result.
2. `wlc_bmac_attach` selects D11 core ID `0x812`, obtains its register base, reads
   `si_corerev` and `si_corerev_minor`, and stores the results in the recovered
   hardware object at offsets `0x14c` and `0x4de`. It passes the core revision
   to `d11regs_select_offsets_tbl`; failure returns attach error `0x23`.
3. The register selector explicitly supports core revisions 30, 42, and 130.
   Core 130 selects the descriptor at analysis address `0x003a3f88`.
   Its first pointer resolves to `0x003a487c`; the 16-bit field at nested offset
   `0x8e` is `0x03e0`.
4. BMAC attach passes the SI chip number from recovered SI offset `0x40`, core
   revision, board information, and the SI handle through `wlc_phy_shared_attach`.
   Chip number is carried as shared PHY data, rather than used here to invent
   a PHY revision. The observed PHY entry point is `phy_module_attach`.
   Literal procedure search for `wlc_phy_attach` returned only
   `wlc_phy_attach_nphy`; this image uses the newer module/dispatch framework.
5. `phy_module_attach` resets the core, then reads a 16-bit hardware PHY-version
   value through the descriptor-selected offset. For core 130 that read is
   from the D11 register base plus `0x3e0`. It decodes type from bits 8..11,
   revision from bits 0..7, and analog revision from bits 12..15. It normalizes
   raw type `0xd` to `0xb`; an observed normalized `0xb` therefore does not
   identify the raw type bits uniquely.
6. `phy_type_disp_attach` looks up a two-entry table. Independently read table
   bytes resolve type 4 to the N PHY attach callback and type `0xb` to
   `0x000460bc`, confirmed as `phy_ac_attach`. `phy_type_attach` invokes that
   selected callback through its table entry.

This establishes how a reported core-130/type-`0xb`/revision-51 tuple reaches the
AC PHY implementation. It does not mean every BCM63178 instance must supply that
tuple, nor that the function prefix implies an older WiFi generation.

| Observation | Evidence ID |
| --- | --- |
| BMAC attach and input dependencies | `ev_9c5169ec2aebea30cb32004f1cf079127c180b4a9f7438b0b14ce36ce1ff45c4` |
| Register descriptor selector pseudocode | `ev_eb39047223e4c5c6c3885948ef64887d11a5b53da88c25a43298c285f37d23c6` |
| Register selector assembly | `ev_9dbe57e9a416183dae3bc38ee22556593edd481aab909f720000b0c0246db4bb` |
| Core-130 descriptor pointer | `ev_1586c82b5f5725d0c4a6f51d3761fdbc2e1bb4a03454b82de0dfef1c8b2110af` |
| Core-130 PHY-version offset | `ev_51f5b2477147507e801f1530fc48d5c6af77aed91924f01b127edd4ddd96480d` |
| Shared PHY object construction | `ev_c3a33d97fe694f2e340fd37f2225ac09cacd1a2bdc1fd442d321a98b9a7ecccd` |
| PHY module attach and PHY-version decode | `ev_11b3698b4de85d747fef84a5e1debc11a54fdebe0aa87da70bdc13059efcb2d8` |
| PHY dispatch logic | `ev_f3c35421cce13d16eceecbdc0ad4d2126fcf8f23b9f73a38ae1be0e7f93eaef8` |
| Resolved dispatch table bytes | `ev_308ae58b9fa159eb7ad285b89eb26c0f637c477e9eadbec91041fb8e6e52c3d5` |
| Indirect attach callback invocation | `ev_1ed4455f8abf12783669d6f5c86a95276cc9d95712e51290ff0e3f3021b015b5` |

## AC PHY attach and next initialization callbacks

`phy_ac_attach` queries and parses the radio ID, then validates the radio. Its
behavior depends on actual radio identity and additional PHY registers. For
revision 51, the initial revision test takes the path reading PHY register 2
for its low-nibble subrevision. The function later has an explicit
`phy_rev == 0x33` branch setting a capability flag. The actual register values
were not observed in this offline run.

AC attach registers callbacks including `phy_ac_register_impl`,
`phy_ac_unregister_impl`, and `phy_ac_reset_impl`. The registration implementation
installs separate analog, radio, table, power-control, calibration, and other
subsystem implementations, with allocation/registration failures propagated.
`phy_ac_reset_impl` initializes four state bytes to `0x30` for PHY revisions at
least 47, including 51; older revisions use `0x40`.

The table subsystem registers `phy_ac_tbl_init` as its initialization callback.
Its registration already consumes board-dependent voltage, TX gain, board-flag,
radio, crystal-clock, and OTP-related information. The common PHY attach also
adds `_phy_init` to an ordered initialization callback list. Recovering the
complete PHY-51/radio-specific initialization program therefore requires those
callbacks, register sequencing, calibration, channel setup, and selected board
data; MAC initialization records alone do not implement them.

| Observation | Evidence ID |
| --- | --- |
| AC attach, radio query, revision-51 handling | `ev_b460e6004aecc02d4d2cd39c829cc96c79f2654e7cc1d36afb99741b94f48505` |
| AC subsystem registration | `ev_630970e9e6170b3c1d25d0667553fbb1e945e82a9f875b3b33a0d1e87313ccd4` |
| AC table callback and data dependencies | `ev_e3e2d2b9774fab02aa1d061e8d3df29f85d225105437d8dc265773480ec7bf5e` |
| AC reset implementation | `ev_66edc688cc039a898f6882983e74db1eb87024ce3b29a9d0b2cc2e56d76b96c5` |

## MAC initialization and microcode selectors

`wlc_bmac_init` calls `wlc_ucode_download` on the core-unit-zero path. Later,
`corerev == 130` selects `d11ax51initvals130` followed by
`d11ax51initvalsx130`. `wlc_bmac_bsinit` similarly selects
`d11ax51bsinitvals130` followed by `d11ax51bsinitvalsx130` before invoking band
initialization. These table branches test core revision 130; they do not contain
an additional explicit PHY-revision-51 test. The `51` in a symbol name is not
itself a runtime condition.

The microcode selector's core-130 branch checks the core minor byte and bit zero
of `boardflags`. BMAC attach obtains that word with the slicespecific boardflags
lookup. The selector uses these image families and byte lengths:

| Core minor | `boardflags & 1` | Main image / length | Extended image / length |
| --- | --- | --- | --- |
| 1 | 0 | `d11ucode_mu130_1` / `0xfda0` | `d11ucodex_mu130_1` / `0x92d0` |
| 1 | 1 | `d11ucode_btcxmu130_1` / `0x10c10` | `d11ucodex_btcxmu130_1` / `0x9218` |
| Other | 0 | `d11ucode_mu130` / `0xfef8` | `d11ucodex_mu130` / `0x92d0` |
| Other | 1 | `d11ucode_btcxmu130` / `0x10d68` | `d11ucodex_btcxmu130` / `0x9218` |

A nonzero byte at recovered hardware offset `0x186` skips downloading on the
core-128-and-newer path. For the earlier observed minor revision 1, the suffix
`130_1` is selected, but the exact MU/BTCXMU choice remains conditional until
the selected slicespecific boardflags value is established. No microcode bytes
are reproduced in this document.

`d11shm_select_ucode_std` separately branches on core revisions 30, 42, and 130
to choose standard shared-memory descriptor families. Its recovered anchor names
are not independently identified here as more specific named data symbols.

| Observation | Evidence ID |
| --- | --- |
| Main initialization selectors | `ev_bff33b73bdefa3d52a4daf134ad0ccd9f13c5fd6037bde943a4880fe9242f6e4` |
| Band initialization selectors | `ev_db9d64e0b5dd05b68dea42bc357541570d91aef926c5f25de55e03bd9a5154a2` |
| Microcode selector and image lengths | `ev_7f48cfafb1f084d3640a7f70a42b4f8be443dd4bc791c1e10fe551ed62a828ab` |
| Standard shared-memory family selector | `ev_2ba9d250ed23c9d28a36a5e935e2819400c00ea28e24fdeef05fe3c3a4b0036f` |

## Initialization record consumer semantics

`wlc_write_inits` pseudocode and assembly independently corroborate an 8-byte
little-endian record layout: 16-bit offset, 16-bit width, 32-bit value. The
consumer tests the offset for `0xffff` before processing and advances by 8 bytes.
For offsets below `0x1000`, width 2 performs a 16-bit register write and width 4
performs a 32-bit register write, following a store barrier. Other widths in this
range are skipped by the observed consumer; an offline decoder may choose to
reject them explicitly as a stricter validation rule.

Two additional semantics matter for a recording bus or later implementation:

- Before writing, if the record offset matches the nested register descriptor's
  16-bit field at `0x22`, the consumer adds the hardware object's word at `0x260`
  to the record value. For the core-130 descriptor, that compared offset resolves
  to `0x0130`. The value of the adjustment word is runtime state, not a constant
  recovered here.
- Offsets at least `0x1000` take an object-memory path regardless of the record's
  width field. The consumer passes transformed offset
  `((offset - 0x400) >> 1) << 2`, the adjusted value's low 16 bits, byte length 2,
  and selector `0x30000` to `wlc_bmac_copyto_objmem`.

Consequently a flat sequence of raw MMIO writes is an incomplete model. A safe
offline recorder can preserve original record fields, adjustment context, and
object-memory operations separately, leaving unresolved runtime values explicit.

Consumer evidence:
`ev_5b7779d029093445701be0f68e1de8b4ac9e5bfeaf2f8bca9d47015237fb1b67`
and assembly
`ev_1238784e268a149f826b7819cf3f3ee1cc094d848c1075722c28c506f0388386`.
Core-130 adjusted-offset evidence:
`ev_cb33e86ef10716e3f43721f11e81b303b339505151b5ba940887f4376ed7e179`.

## Remaining implementation gap

The bounded result supplies a driver-derived selection and record-consumption
contract. It does not supply a working replacement driver. The next missing
implementation is the PHY-51-specific initialization and radio/channel/calibration
path reached through the registered AC PHY callbacks, together with the chosen
board data, core/reset/clock access, object-memory interface, microcode lifecycle,
DMA, and interrupt behavior. Dependency presence, function names, and successful
decompilation do not establish that any of those operations work on new code.

All conclusions are static observations or explicitly conditional deductions.
No runtime success, radio compatibility, or modern-kernel port has been verified.

The query boundary was attach/dispatch, descriptor fields, selected MAC/ucode
selectors, AC registration/reset callbacks, and the initialization-record consumer.
`phy_ac_tbl_init` internals, the full calibration/channel sequence,
`wlc_ucode_write`, and `wlc_ucodex_write` upload formatting were not inspected.
A raw offline replay that rejects object-memory rows and
omits the runtime fixup at offset `0x130` must retain that limited scope explicitly.
