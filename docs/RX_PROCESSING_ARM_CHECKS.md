# Current composed receive processing ARM comparison

`tests/check_rx_processing_arm.py MODULE` compares the original portable
`rx_packets` and `rx_status` processors, including their actual packet/status
list children, with the complete selected ARM bodies in the pinned current
17.10.121.37 `wl.ko`. It requires a caller-supplied immutable module, Unicorn
2.1.4 and a host C compiler. The host comparison library is built temporarily.
No executable payload is copied into the repository or printed.

The checker validates the module digest, four complete function digests and
all selected relocation records. Only the following actual ARM CALL bindings
are resolved, using their ELF symbol identities and original REL addends:

| Caller | Actual relative CALL site | Actual callee |
| --- | --- | --- |
| `dma_rx` | 0x1c | `_dma_getnext_rxlist` |
| `dma_rx` | 0x13c | `_dma_getnext_rxlist` |
| `dma_rx` | 0x1c8 | `_dma_sts_rx` |
| `_dma_sts_rx` | 0x14 | `_dma_getnext_sts_rxlist` |

These calls execute the selected callee instructions. Reached external or
unbound CALL relocations fail before execution; unknown direct calls and
registered indirect calls fail as well. There are no external helper success
stubs. The module is never loaded as a kernel driver.

Every synthetic context, output, packet/status field and data sample is typed
and explicitly initialized. Stack reads require prior writes. Descriptor-low
and channel-status loads consume independent explicit fresh reply queues;
missing, repeated or unused replies fail. Physical packet/status reads observe
the current initialized serialized memory after earlier real stores. The C
adapter uses the same independently constructed starting objects and fresh
reply queues, then applies its typed requests to a separate memory instance.
There is no implicit zero read or write echo.

The comparison checks the exact ordered memory/barrier requests, including
resource identity, width, byte offset and store value. At every request it
compares both DMA contexts, linked DMA state, error counters and both output
summaries. Final checks compare every initialized packet/status/data byte as
well as final DMA and output state. This includes descriptor observations that
are discarded by the binary, status's four byte detach writes, fragment links,
linear packet length/tail stores, and output count increments before final NEXT
loads. Internal software steps may be finer than ARM instructions; the physical
I/O snapshots and final states must still agree.

The checked run passed 304 cases and 74,457 actual ARM instructions. Coverage
includes normal empty/one/two packet results, wrapped indices, nonzero linked
mode byte 0x80, positive status-node processing with both cached-index gates,
standalone empty/status processing, count overflow and several receive offsets.
Fragment cases cover one or two continuations already in the returned list and
48 real budget-one bulk refill calls. All four reviewed CALL sites executed:
296 initial packet list calls, 48 refill list calls, 200 packet-to-status calls
and 208 status list calls.

Seven rejection probes passed: unbound selected CALL, unknown packet field,
missing descriptor observation, unused reply, and reached external delay, trim
and free calls. The last three probes demonstrate rejection at the actual call
boundary; they do not claim to execute those OS helpers.

This comparison deliberately selects positive receive lengths and linear
packets, avoiding OS delay/trim/free calls. It does not execute status timeout
pool return or an unknown registered pool callback. Those processor branches,
callback faults/cancellation, dependency states and strict policy failures are
covered by the separate original literal C suites and source audits. Passing
this checker proves agreement for the selected offline synthetic executions;
it does not validate Linux packet ownership, DMA mapping, concurrency, IRQs,
BMAC delivery, physical reception or any hardware/RF operation.
