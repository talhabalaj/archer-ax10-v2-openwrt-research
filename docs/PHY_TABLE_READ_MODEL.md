# Actual AC width8 table reader

The original [reader](../driver/phy_table_read.c) closes the current generic
AC table-read wrapper for width8 on core130.0/1, PHY51. It supplies arbitrary
table ID, count and offset for the real six-byte MCLIP comparison, alongside
the existing separate table29/width32 primitive. It does not infer table
geometry, ownership, hardware mapping or a usable radio.

The actual sequence is physical maincontrol read/SY, optional real PSM suspend,
fresh PHY19e read and conditional mask2 set, packed PHYd/PHY e headers, actual
PHYf reads, conditional19e clear and real PSM enable. The STF getter is a pure
pointer projection; the current disable/restore HWACI trigger functions are
source-proven no-ops. They are not replaced by arbitrary successful callbacks.
The selected width8 dispatch entry resolves to the actual byte-store case.
PHY51 does not enter the legacy table20/revision6..11 or13..15 workaround.

Header ID/offset use low16 bits. Count0 still executes the real wrapper and
both headers. Other widths and insufficient/wrapping/own-job output spans are
rejected before I/O as explicit bounded-host policies. The caller owns mutable
output storage outside this reader job until termination; a composing parent
may own that buffer. Context and request aliases are snapshotted at begin.

Each output byte is stored only after its actual PHY read returns, as a separate
software step after the final SY. A successful cancelling SY preserves the
child's observed sample but cannot execute that later output store. Earlier
bytes, physical effects and counter changes remain. Canonical PHY/PSM state
is published before callbacks and after every child step. Each step forwards
at most one synchronous callback; error wins cancellation and terminals remain
silent without cleanup, retries or rollback.

[Literal tests](../tests/test_phy_table_read.c) independently cover existing-set
and clear/set/restore guards, zero count, distinct supplied samples, low8 byte
stores, every callback error/cancel/both boundary, cancellation between SY and
the first output store, canonical bookkeeping, output bounds, aliases and the
reached missing-MAC binding. Strict C11 ASan/UBSan and ARMv7 compilation pass.
The [noise composition](PHY_NOISE_MODEL.md) additionally exercises matching and
changed six-byte tables and actual active-MAC suspension/PHY nesting. Selected
source identities are recorded with [noise evidence](../findings/PHY_NOISE_EVIDENCE.json).
This new reader has not itself been run as a composed stock ARM execution;
literal C checks and retained actual instructions are distinct evidence.
