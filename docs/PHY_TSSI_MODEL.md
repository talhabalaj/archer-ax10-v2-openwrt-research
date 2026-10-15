# PHY-51 idle-TSSI callback composition

`driver/phy_tssi.c` implements the recovered AC idle-TSSI **callback** for
explicit runtime PHY core counts one through four. It composes direct PHY
read/modify and the [complete table-29 wrapper](PHY_TABLE_MODEL.md). The outer
[idle-TSSI wrapper](PHY_GUARD_IDLE_MODEL.md) now implements MAC suspend,
PHY-register enter/exit and final MAC enable through actual children. Calling
this callback API alone does not imply those ran. Caller ownership, power/clocks and serialization remain prerequisites.
Exact target behavior is documented in the
[resource ledger](DRIVER_RESOURCE_AUDIT.md) and
[table/TSSI addendum](DRIVER_AC_TABLE_READ_AUDIT.md).

## Slots and operations

SNAPSHOT visits active slots in descending order. Slots three and two, when
present for PHY 51, each invoke the actual table-ID `0x29`, index `0xffff`,
count-one, width-32 wrapper. Four-core snapshot repeats the same logical table
read; it does not advance a held index or replace its value with zero. Slot one
reads PHY selector `0x845`; slot zero reads `0x645`. Each completed value stores
only low10 bits in its corresponding saved slot.

RESTORE visits the same descending slots. The actual `modify(0xffff)` calls for
slots three/two are no-ops and emit no events. Slots one/zero use fresh masked
modifies with mask `0x3ff`, preserving the other six bits from their respective
new hardware samples. High bits in supplied saved values are masked. This
snapshot/restore asymmetry is recovered behavior, not a reason to omit reads.

`saved[4]` and `valid` in this coordinator represent the original eight-byte
callback buffer and explicit validity policy. The lower PHY model's two-slot
convenience buffer remains unchanged by READ/MODIFY children; it is not a second
copy of this callback's results. The earlier standalone one/two-core API remains
available as a smaller comparison primitive.

## Partial buffers and failure policy

SNAPSHOT clears active validity bits on its first ENTRY step, preserving inactive
slots. Each direct-read slot commits after its successful final SY, including
cancellation at that SY. A table-backed slot commits only when the **complete
table wrapper** reaches `TRACE_COMPLETE`. A completed table data word followed
by failed/cancelled guard clear or MAC enable does not commit a TSSI slot.
The table job retains its partial word and diagnostics for inspection.

RESTORE preflight requires all active saved slots valid, including the extra
slots whose stock modifies are no-ops. This is an explicit stricter model policy
than the unchecked stock buffer. Invalid core identity/count, context, command
or validity bits reject before callbacks. An aliased `&job.context` is copied
before resetting the job.

All hardware contexts and pending MAC callouts propagate after every child
step. Missing context, wake-policy/MBSS dependency and semantic failure remain
visible; they are not successful placeholders. Each parent step invokes at most
one user callback or performs a bounded transition. Successful cancellation
preserves effects; callback error wins over cancellation. Terminal states emit
no I/O and invent no retry, buffer restoration, guard cleanup or MAC enable.

## Validation scope

Independent literal tests cover all core counts, both commands, repeated
table reads, fresh masked restores, active/inactive valid slots and untouched
lower convenience state. Every selected callback covers error, cancellation
and coincident error/cancellation with partial buffer/context checks, including
direct final-SY commit and table-word-versus-wrapper completion. Missing saved
slots, invalid inputs, alias-safe restart and terminal silence are included.

The [composed ARM harness](PHY_COMPOSED_ARM_CHECKS.md) also compares all core
counts/commands with actual selected stock instructions under inactive-MAC
synthetic contexts. Three deliberately broken C models fail the independent
literal tests. These are offline behavioral checks with supplied inputs. They do not establish
outer PHY-entry/wake ordering, physical table contents, TSSI calibration accuracy,
kernel integration or RF behavior.
