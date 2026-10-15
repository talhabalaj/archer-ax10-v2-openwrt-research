# Actual HND clock fallback and its resource selection

Original behavioral account from a new bounded REA 6.5.0/Ghidra 12.1.4 static
session on `hnd.ko`, 366,060 bytes, SHA-256
`d05ff061b3a636630955a13fdd20a953f3dfb07ee92904a987dd4c77b648b45a`.
The exact WL caller is covered by the earlier resource audit. Source digests
were verified before and after analysis. The HND and WL follow-up sessions
were separate, exported, and closed. No router or target execution was used.

The companion [dependency Evidence summary](../findings/DRIVER_RESOURCE_DEPENDENCY_EVIDENCE.json) gives actual operation
names and Evidence IDs. Full disassembly and pseudocode remain private. The
operations below are an original behavioral ledger, not reproduced source.
`ST/SY` mean ARM `DSB ST/DSB SY`; device read/write widths are explicit.

## Caller relationship and interpretation

The retained WL caller passes the same SI handle and mode into `si_clkctl_cc`
when capability word `si+0x18` bit `0x10000000` is clear. This HND fallback
operates on ChipCommon core ID `0x800`, unit zero, rather than the current D11
mapping. Its offset `0x1e0` is a separate typed resource despite matching D11's
clock register offset. No physical address, CC revision, bus mode, capability
word, or current-core metadata is assigned an invented board default here.

The wrapper at `0x32a64` tests the signed CC revision field at `si+0x14`.
Revision at most 5 returns zero immediately. Otherwise it tail-calls the
actual body at `0x324e0`, preserving the mode argument omitted by the
decompiler's wrapper prototype. For revisions above 5, the body's usual return
is exactly `mode == 0`, not an observed clock-ready result.

## Selecting and restoring the ChipCommon resource

A special fast mapping is used when bus word `si+4` is 1 and bus-core ID
`si+8` is `0x83c` or `0x820`, or is `0x804` with bus-core revision `si+0xc`
greater than 12. It uses current mapping `si+0xa0` plus `0x3000`. If the
original mapping is zero or the addition wraps to zero, it skips all clock
operations and returns `mode == 0`. This path does not save/switch/restore the
current index or invoke the interrupt callbacks below.

The ordinary path saves current index `si+0xa4`. Before selection, optional
callback `si+0x84` is invoked with context `si+0x80` only when the current
core ID in the runtime enumeration equals `si+0x7c`; its result is a saved
interrupt token. It then calls `si_setcore(si,0x800,0)`. After clock work or
a gate/mode skip, it restores the saved index with `si_setcoreidx`. Optional
restore callback `si+0x88` is guarded independently by the same core-ID
comparison at restoration time and receives the context and saved token.
If the first callback did not run, that token is zero. There is no general
null-result check after ordinary core selection before clock dereferences.
An offline backend should report invalid resources explicitly rather than
turn an absent mapping into a successful hardware operation.

## Exact clock operations and waits

Clock operations require capability `si+0x18` bit `0x40000` set or signed CC
revision greater than 19. When that gate is false, selection/restoration still
occurs but there is no clock MMIO, settling delay, or status poll. Mode zero
still returns true. Only modes 0 and 2 enter the clock-operation bodies;
other modes perform no clock MMIO and return false after restoration.

| CC revision | Mode 0 | Mode 2 |
|---|---|---|
| 6 through 9 | Call `si_clkctl_xtal(si,1,1)`, then `ST,R32(CC+0xb8),SY,W32((old & ~0x3800) | 0x1000)` | `R32(CC+0xb8),SY`, clear `0x3800`, set `0x2000` if `(old & 7) != 1`, then `ST,W32(new)`; if new bit `0x2000` set call `si_clkctl_xtal(si,1,0)` |
| 10 through 19 | `ST,R32(CC+0xc0),SY,W32(old | 0x10)` | `ST,R32(CC+0xc0),SY,W32(old & ~0x10)` |
| At least 20 | `ST,R32(CC+0x1e0),SY,W32(old | 2)` | `ST,R32(CC+0x1e0),SY,W32(old & ~2)` |

Every cell is conditional on the operation gate above. The old-revision mode-2
barrier placement differs from the other RMW sequences: its store barrier is
after its read and SY. There is no extra barrier/readback after a clock write.

After a mode-0/2 body, capability bit `0x10000000` clear causes exactly one
`delay(150)`, with no clock-status read or readiness poll. This is the retained
WL fallback condition. There is no D11-path 64-us delay in this HND route.

For completeness, direct HND calls with capability `0x10000000` set instead
perform `R32(CC+0x1e0),SY`. Mode zero waits for bit `0x20000` set, mode two
for it clear. If initially unmet there are at most 1,500 repetitions of
`delay(10),R32(CC+0x1e0),SY`; no separate final confirmation read follows.
Exhaustion changes neither the return predicate nor an exposed failure flag.
The returned mode predicate and WL's resulting cached byte/wake override do
not establish readiness.

## Immediate crystal helper

The actual `si_clkctl_xtal` is at `0x32378`. It returns zero with no I/O for
bus word 2, returns `0xffffffff` for other non-1 buses, and likewise returns
`0xffffffff` for bus 1 with bus-core IDs `0x820` or `0x83c`. These return
values are ignored by the fallback body.

For supported bus 1 it issues ordered 4-byte PCI configuration reads at
`0xb0`, `0xb4`, `0xb8`. With the fallback's `what=1,on=1`, input bit
`0x40` already set causes no writes or delay; otherwise it sets bit `0x40`
in both saved output and output-enable values, writes configuration `0xb4`
then `0xb8`, then delays 1,000. With `what=1,on=0`, it clears saved output
bit `0x40`, sets output-enable bit `0x40`, writes `0xb4` then `0xb8`, and
does not delay. The wrapper has no explicit ARM barriers around these PCI
API calls. PCI API/backend ordering is a separate OS contract.

The helper also supports `what` bit 2, which the clock fallback does not use.
That bit sets output-enable `0x80`; on disable it sets output `0x80` before
the two writes. On enable it can first combine `0xc0` with output while bit
1 is also requested; then it writes output with bit `0x80` clear to `0xb4`
and delays 2,000. Those additional branches are not BCM63178 defaults.

## Actual selection helpers and resource inputs

`si_setcore(si,id,unit)` searches the runtime core-ID array starting at
metadata `+0x80`, counting matching IDs until the requested unit. No match,
a selected index above 31, or an unsupported interconnect returns zero.
Interconnect 0 delegates to SB (not followed here); interconnect 1, 3, or 4
delegates to AI. `si_setcoreidx` uses the same interconnect dispatch.
`si_d11_switch_addrbase(si,unit)` tail-calls `si_setcore(si,0x812,unit)`;
there is no separate D11 address arithmetic in that wrapper. `si_coreunit`
counts earlier entries matching the current core ID, using current index
`si+0xa4` and metadata pointer `si+0xb4`, with no MMIO.

The actual AI selector wrapper at `0x10fcc` invokes its body at `0x10024`
with wrapper selector zero. The body rejects indexes at least
`min(runtime_core_count,32)` before changing context. For entry `i`, its
runtime metadata resource fields are:

| Meaning | Metadata offset, plus `4*i` |
|---|---:|
| Cached core virtual mapping | `0x000` |
| Core ID | `0x080` |
| Core physical address | `0x100` |
| Cached primary wrapper mapping | `0x380` |
| Primary wrapper physical address | `0x400` |
| Cached secondary wrapper mapping | `0x480` |
| Secondary wrapper physical address | `0x500` |

For bus word 0, a nonzero physical address with absent cached mapping invokes
`__arm_ioremap(physical,size,0)` and saves the result. Core size is `0x1000`,
except core ID `0x50b` uses `0xf000`; each wrapper size is `0x1000`.
The order is core mapping, primary wrapper mapping, secondary wrapper mapping.
Existing cached mappings are reused. It stores the core map at `si+0xa0`
and primary wrapper map at `si+0xac` for selector zero. Actual return values
are used without a new success/error check in this body.

For bus word 1 the body issues PCI configuration write32 at `0x80` with the
selected core physical address, then write32 with the selected primary
wrapper physical address at configuration `0x70` for bus-core ID `0x83c`,
otherwise `0xac`. It retains current virtual core mapping, rather than
allocating a new mapping; this branch does not change `si+0xac`. There are
no explicit ARM barriers or delays in this selector around the PCI API calls.
Other bus words select a zero core map. Every in-range body path finally
stores the selected current mapping and current index. Thus an index change
or non-error-returning wrapper is not evidence that resources exist.

The runtime metadata's acquisition, actual address values, Linux mapping/API
implementation, exclusive ownership, and SB interconnect path remain outside
this bounded closure. This pass establishes real calls and typed resource
requirements; it does not supply a board mapping or a working kernel backend.
