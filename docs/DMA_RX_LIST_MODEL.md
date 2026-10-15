# Current DMA receive list leaves

`driver/dma_rx_list.[ch]` implements the complete current `_dma_getnext_rxlist`
leaf. `driver/dma_rx_status_list.[ch]` implements the separate complete current
`_dma_getnext_sts_rxlist` leaf. Both are original portable C state machines,
recovered from the pinned 17.10.121.37 module. Neither leaf calls another
function in this build. In particular, neither calls the separate packet-slot
`dma_getnextrxp.part.1` leaf or performs cache, DMA unmap, packet free, allocation,
or Linux delivery. Optional branches in older 17.10.86.5 public source do not
establish those operations in the current binary.

The runtime owns serialized packet/status nodes and descriptor/channel memory.
Typed callbacks represent actual fresh loads, actual stores and the full memory
barrier. Resource identities and node labels are opaque 32-bit values; the model
does not convert them to host pointers. A descriptor observation is required for
every traversed index even though its loaded value is discarded by the binary.
A backend must supply a fresh observation; write echoes and default zero reads
are not an execution backend.

| Branch or operation | Packet bulk | Status bulk |
| --- | --- | --- |
| Direction count zero | Return zero with no I/O | Return zero with no I/O |
| Nonzero force argument | End at software `out`; skip status and budget | End at software `out`; skip status |
| Normal status refresh | Always R32 at channel+0x10, then SY, including budget zero | Refresh only when cached index equals `in` |
| Normal end | `in + min(unsigned budget, modular completed distance)` | Cached completed index |
| Node NEXT field | Byte 4 | Byte 0x1c |
| Per-node order | NEXT R32, primary head store, descriptor-low R32 at index*16+8 | Same |
| Detach final NEXT | One WRITE32 of zero | Four separate WRITE8 operations at 0x1c..0x1f |
| Final software order | `in`, available, optional linked `in`, linked available, optional empty tail clear | `in`, optional empty tail clear, available |
| Return | Original head | Original head |

Packet bulk's linked mode tests the entire byte for nonzero. Its linked context
binding and owner are required only after the primary index and available count
have committed. Status bulk has no linked mirror or budget. The available count
is `count - ((out - in) & (count - 1)) - 1` after the new index store.

Begin snapshots caller inputs, including aliases into the same job, before
clearing state. Steps invoke at most one callback and separate software stores
from surrounding callback operations. A callback error wins simultaneous
cancellation. Successful cancelling callbacks retain their read sample or
completed physical store; later software stores do not execute. Terminal calls
are silent. This makes a descriptor fault retain the earlier head advance, while
leaving the ring index and available count unchanged. A fault on status detach
byte three retains earlier byte clears without claiming a complete detach.

The portable model deliberately narrows unchecked stock behavior. It supports
the current core130 revisions 0/1 and existing strict power-of-two ring/mask,
alignment and range checks. Status bulk also bounds a reached cached index.
Absent ownership or an unknown reached linked context is a dependency; explicit
null nodes or null linked bindings fail instead of dereferencing invalid
addresses. Descriptor capacity is checked when the observation is reached, after
the preceding head store. Runtime metadata is snapshotted and must remain
coherent; stock's subsequent live metadata reloads are not independently mutable
inputs. There is no claim of concurrent ring safety, host ownership transfer,
physical reception, DMA mapping correctness, or a usable network driver.

`tests/test_dma_rx_list.c` and `tests/test_dma_rx_status_list.c` derive independent
literal wrap fixtures, fresh status and descriptor queues, source store order,
force/cache/budget gates, reached bindings, aliases and every callback's error,
cancel and simultaneous error/cancel prefixes. Both pass strict C11 and
AddressSanitizer/UndefinedBehaviorSanitizer. Eight private source mutations were
rejected, covering status/cache gates, budget choice, descriptor/node lanes,
linked availability, detach byte count and final index.

`tests/check_dma_rx_list_arm.py MODULE` requires the immutable pinned module,
Unicorn 2.1.4 and a host C compiler. It verifies both complete leaf hashes and
absence of relocations, builds the C comparison bridge temporarily, then runs
the actual selected ARM instructions over synthetic contexts and memory. It
compares exact I/O kind, identity, width, offset and value, context snapshots at
each I/O, physical node detach stores and all final primary/linked values.
The checked run passed 3,264 packet cases and 408 status cases, executing 952,552
instructions. Six negative probes rejected unknown context memory, missing
reads and unused read replies. Unknown instructions, calls and memory accesses
fail. No target hardware or helper success stubs are supplied.
