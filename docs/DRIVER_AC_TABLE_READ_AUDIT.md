# AC table-29 PHY-register read contract

Original behavioral report from a fresh bounded REA 6.5.0/Ghidra 12.1.4
static session. Exact target `wl.ko`: 5,803,056 bytes, SHA-256
`8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c`.
Existing resource/TSSI evidence was reused; only missing table-reader callees
and the necessary packed PHY-write helper were expanded. Raw output remains
private. The new session was exported and closed, and the source digest
verified again. No router, target execution, or physical MMIO was used.

Actual IDs are in [the selected Evidence summary](../findings/DRIVER_AC_TABLE_READ_EVIDENCE.json). This report describes
stock normal-path operations and explicit inputs; it does not claim complete
PHY initialization or tested hardware behavior. `ST/SY` mean ARM `DSB ST/SY`.

## Call arguments and result

Retained `phy_utils_read_phyreg(phy, selector)` takes an unsigned selector.
For `selector >= 0xc000`, assembly calls
`wlc_phy_table_read_acphy` with table ID `0x29`, count 1, offset equal to the
original selector, width `0x20` bits, and a local output buffer. There is no
subtraction of `0xc000`, multiplication by four, sentinel rejection, or zero
substitution. The argument structure passed to the lower reader is:

| Byte offset | Field | Value in this path |
|---|---|---|
| `0x00` | Output pointer | Local buffer |
| `0x04` | Entry count | `1` |
| `0x08` | Table ID | `0x29` |
| `0x0c` | Table index/offset | Original selector |
| `0x10` | Entry width in bits | `32` |

The table reader writes ID and index through 16-bit values, truncating each
to its low 16 bits. In particular selector `0xffff` programs index `0xffff`.
Count denotes entries, not byte length; width 32 stores four output bytes per
entry. Index is sent directly to the index register, with no byte-address
conversion in this call path. Register programming alone does not establish
the hardware's automatic cursor behavior for later entries.

Both entry halves are read, assembled as `low | (high << 16)`, and stored as a
32-bit word. The outer helper loads only the low 16 bits from that local
buffer on this little-endian ARM artifact. It does not omit the high-half
read even though the returned value discards it.

## Wrapper ownership and temporary PHY state

`wlc_phy_table_read_acphy` first calls `phy_stf_get_data`; that actual helper
only loads its argument's word at `+8`, and the caller ignores its result.
The pointers still must be valid; there is no null check.

The wrapper obtains `R32(D11+0x120),SY`, samples bit `0x1`, and conditionally
calls retained `wlapi_suspend_mac_and_wait` when it is set. It then reads
PHY register `0x19e` through the retained direct read helper. If bit `0x2`
was already set it invokes the lower table reader directly. Otherwise it
sets that bit with `phy_utils_mod_phyreg(phy,0x19e,2,2)`, invokes the lower
reader, then clears it with `phy_utils_mod_phyreg(phy,0x19e,2,0)`. These are
fresh masked RMW operations preserving their respective new read samples;
they do not restore the entire original `0x19e` value.

If the original D11 bit `0x1` was set, the wrapper calls retained
`wlapi_enable_mac` after the table operation and optional bit clear. The
retained suspend/enable guards, caches, depth and failure policies apply.
This wrapper does not test a suspend return or establish a new readiness
predicate before continuing. It does not take a host lock, disable interrupts,
or call PHY-register enter/exit itself. TSSI's surrounding helper does invoke
the previously resolved PHY-register nesting/wake-override sequence. Other
callers require their own established context and serialization.

For PHY revision 51 the extended wrapper calls HWACI disable/restore helpers
around the physical reader. Their actual shipped assembly is respectively a
zero return and a plain return, with no MMIO, callback, delay, or state change.
This is proven behavior of these exact helpers, not a substituted success
implementation. The physical reader's special table-ID `0x14` workaround is
not entered for table ID `0x29`.

## Core-130 resources and physical transfer

Retained descriptor evidence resolves PHY address/data ports to D11 offsets
`0x3fc/0x3fe`, PHY version to `0x3e0`, and the new descriptor read resolves
the wrapper's main-control access to `0x120`. The extended reader passes
logical PHY selectors: table-ID `0x0d`, index `0x0e`, wide data `0x11`,
high data `0x10`, and low data `0x0f`. Width 32 uses low/high selectors only;
the independently inspected width-32 dispatch entry targets the assembly
block performing low read followed by high read.

The normal table-ID/index programming uses `phy_utils_write_phyreg`, whose
direct branch is a **packed 32-bit device write**, not two 16-bit writes.
For a logical selector below `0xc000`, it ends with:

`ST,W32(D11+0x3fc, selector | (value16 << 16))`.

Thus this call programs table ID using packed value `0x0029000d`, followed
by index using `(index16 << 16) | 0x000e`. There is no final readback or SY
after either packed write, except for the conditional preceding flush below.

Each data-half read uses the retained direct helper:

`ST,W16(D11+0x3fc,logical_selector),R16(D11+0x3fe),SY`.

It also clears the software 16-bit counter at PHY context `+0x104` between
address write and data read. Width-32 therefore reads selector `0x0f`, stores
the low half in the output word, reads selector `0x10`, then ORs/stores the
high half. There is no table-ready poll, intervening delay, checksum, or final
confirmation read. No explicit index increment occurs in this count-1 path.

## Packed-write counter and conditional flush

The packed-write helper has a necessary bus-dependent prelude. It tests the
word at `*(phy+8)+0x0c`; this is an actual PHY shared-context field, not a
default inferred from the chip name or equated to another SI field here.
When it equals 1, each packed write increments the 16-bit counter at
`phy+0x104` modulo 65,536. If the incremented count is at least the 16-bit
threshold at `phy+0x106`, it clears the counter and performs
`R16(D11+0x3e0),SY`, discarding the result, before the usual `ST,W32`.
When the shared-context word differs from 1, this prelude does not touch the
counter or perform that read. Assembly is authoritative: the decompiler
omitted this conditional PHY-version read while retaining its barrier.

For the complete table-29 wrapper, the preceding mandatory direct read of
PHY `0x19e` clears the counter before the two packed header writes. With no
out-of-contract callback mutation, this implies the following bus-word-1
cases: threshold 0 or 1 flushes before both writes; threshold 2 flushes only
before the index write; threshold at least 3 flushes before neither. These
are derived consequences of the exact counter and wrapper sequence. The
later low/high direct reads clear the counter again. A standalone packed
write requires its own supplied initial counter and threshold instead.

## PHY-51 TSSI three/four-core cases

The retained AC TSSI callback uses PHY revision 51 and runtime core-count
byte to select snapshot reads in descending core order:

| Runtime PHY core count | Snapshot selectors, in order |
|---|---|
| 1 | `0x645` |
| 2 | `0x845`, `0x645` |
| 3 | `0xffff`, `0x845`, `0x645` |
| 4 | `0xffff`, `0xffff`, `0x845`, `0x645` |

Each `0xffff` invokes the complete table-ID `0x29`, index `0xffff`, width-32,
count-1 call above. Four-core snapshot repeats that logical read; it does not
advance a caller-held table index between those calls. Each returned low
16-bit value is masked to `0x3ff` for the save buffer. Actual core count is
not established merely by chip or PHY revision.

Restore uses the retained modify helper, which returns immediately on
selector `0xffff`. It therefore does not table-write those saved three/four
core entries, while the `0x845/0x645` masked restores remain ordinary direct
operations. This asymmetry is stock behavior, not justification for replacing
snapshot's table reads with zero.

## Validation and failure boundaries

The investigated read functions return no success/error status and have no
pointer, buffer-length, table/index-range, ownership, or width validation for
this call. Count zero in the generic reader still programs ID/index before
skipping entry reads; the investigated outer path always supplies count one.
There is no hardware-error recovery or timeout loop in this table-29 body.
The low output half is stored before the high read completes. A partial trace
must not be labeled a complete valid entry or defaulted to zero.

An offline component may reject unsupported widths/counts, require explicit
ownership/resources, or stop on backend error/cancellation. Those are explicit
model policies. The stock normal return clears a temporarily set `0x19e`
bit and conditionally enables MAC, but these observations do not establish
an error-unwind/rollback protocol; inventing automatic cleanup after a failed
callback would add behavior. Read/modify, MAC suspend/enable, and PHY-register
wake contracts remain separately typed dependencies.

The packed helper's own selector-at-least-`0xc000` table-write branch was not
expanded, since the read path only writes logical selectors `0x0d/0x0e`.
Other table widths/IDs, physical table automatic cursor behavior, runtime bus
and threshold acquisition, host ownership, and later calibration/radio paths
remain outside this bounded read contract. This report does not implement the recovered table-reader path.
