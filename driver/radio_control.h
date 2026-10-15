/* SPDX-License-Identifier: MIT */
#ifndef AXD_RADIO_CONTROL_H
#define AXD_RADIO_CONTROL_H
#include "phy_table_write.h"
#include "wake_policy.h"

struct axd_radio_control_context {
    struct axd_phy_table_context hardware;
    struct axd_wake_u32 phy_pointer, public_pointer, shared_pointer;
    struct axd_wake_u32 radio_info, dispatch, callback_context, callback_pi;
    struct axd_wake_u32 ac_pointer, chanmgr_pointer, chanmgr_data_pointer;
    struct axd_wake_u32 radio_id, radio_revision, radio_read_offset;
    struct axd_wake_u32 chip_id, shared_bus_revision, init_callback;
    uint8_t pi47e, chanmgr_data10;
    /* Borrowed immutable private prefregs_20704_rev0 records, LE u16 pairs
     * ending with selectorffff. Storage stays outside every job and immutable
     * until terminal. No proprietary records are embedded. */
    const uint8_t *prefregs;
    size_t prefregs_length;
};
enum axd_radio_control_command {
    AXD_RADIO_CTL_AC_SWITCH_OFF=1, AXD_RADIO_CTL_AC_SWITCH_ON,
    AXD_RADIO_CTL_AC_ON_REGISTERED, AXD_RADIO_CTL_COMMON_INIT,
    AXD_RADIO_CTL_READ, AXD_RADIO_CTL_WRITE, AXD_RADIO_CTL_MODIFY
};
enum axd_radio_control_phase { AXD_RADIO_CTL_INVALID=0, AXD_RADIO_CTL_RUNNING,
    AXD_RADIO_CTL_TRACE_COMPLETE, AXD_RADIO_CTL_DEPENDENCY,
    AXD_RADIO_CTL_FAILED, AXD_RADIO_CTL_CANCELLED };
enum axd_radio_control_error { AXD_RADIO_CTL_NO_ERROR=0,
    AXD_RADIO_CTL_BAD_ARGUMENT, AXD_RADIO_CTL_BINDING, AXD_RADIO_CTL_UNSUPPORTED,
    AXD_RADIO_CTL_BOUNDS, AXD_RADIO_CTL_CHILD_ERROR, AXD_RADIO_CTL_IO_ERROR };
enum axd_radio_control_field { AXD_RADIO_CTL_NO_FIELD=0, AXD_RADIO_CTL_PHY,
    AXD_RADIO_CTL_PUBLIC, AXD_RADIO_CTL_SHARED, AXD_RADIO_CTL_RADIO_INFO,
    AXD_RADIO_CTL_DISPATCH, AXD_RADIO_CTL_CALLBACK_CONTEXT,
    AXD_RADIO_CTL_CALLBACK_PI, AXD_RADIO_CTL_AC, AXD_RADIO_CTL_CHANMGR,
    AXD_RADIO_CTL_CHANMGR_DATA, AXD_RADIO_CTL_ID, AXD_RADIO_CTL_REVISION,
    AXD_RADIO_CTL_READ_OFFSET, AXD_RADIO_CTL_CHIP, AXD_RADIO_CTL_BUS_REVISION,
    AXD_RADIO_CTL_INIT_CALLBACK, AXD_RADIO_CTL_PREFREGS };
enum axd_radio_control_dependency { AXD_RADIO_CTL_NO_DEPENDENCY=0,
    AXD_RADIO_CTL_METADATA, AXD_RADIO_CTL_OTHER_ON_DISPATCH,
    AXD_RADIO_CTL_LOGEN_PLL_CONTINUATION, AXD_RADIO_CTL_OTHER_OFF_TAIL,
    AXD_RADIO_CTL_COMMON_CALLBACK };
struct axd_radio_control {
    enum axd_radio_control_phase phase;
    enum axd_radio_control_error error;
    enum axd_radio_control_command command;
    enum axd_radio_control_field last_field;
    enum axd_radio_control_dependency dependency;
    struct axd_radio_control_context context;
    struct axd_phy_table_write packed_job;
    struct axd_phy_register phy_job;
    uint16_t selector, mask, value, sample, result, write_value;
    unsigned result_valid, stage, after_child, after_radio, radio_stage;
    unsigned radio_command, index, core_index, registered_entry;
    unsigned metadata_accesses, software_stores, prefreg_writes, flush_reads;
    size_t prefreg_cursor;
    uint64_t attempted_ops, completed_ops;
};
/* Actual core130.0/1 AC PHY51 operations. OFF completes the chip63178 path,
 * including actual optional2069 rev-major2 per-core radio modifications. ON
 * closes only the identity50e0 prefix, delay, private prefregs loop and88b
 * modification, then DEPENDENCY_LOGEN_PLL_CONTINUATION. Other identities stop
 * before their first unimplemented actual child. Never reports radio-ready.
 * AC_ON_REGISTERED composes the recovered scheduler slot1 context binding and
 * chanmgr-data10/PI47e stores; it cannot bypass that remaining ON dependency or
 * execute the later regtbl-on-poweron tail. COMMON_INIT completes only the
 * actual known-null slot3 case of this AC registration; other callbacks are
 * dependencies. These are separate native entries, not interchangeable.
 * Caller supplies actual powered resource identities and private span provenance;
 * no radio model/revision, metadata default, payload or successful callout is
 * inferred. Unknown reached metadata stops; required null pointers fail.
 */
int axd_radio_control_begin(struct axd_radio_control *,enum axd_radio_control_command,
                              const struct axd_radio_control_context *);
/* Actual radio READ/WRITE/MODIFY leaves, including public read-address OR,
 * selectorffff behavior, bus1 u16 counter/READ32(maincontrol)/SY flush and
 * bus2 revision<=3 READ16(version)/SY flush. WRITEffff still executes reached
 * counter work. MODIFYffff composes actual ffff read result and write no-op.
 */
int axd_radio_register_begin(struct axd_radio_control *,enum axd_radio_control_command,
                               uint16_t selector,uint16_t mask,uint16_t value,
                               const struct axd_radio_control_context *);
/* Own-context aliases are snapshotted. At most one callback per step. Canonical
 * child state is published before callbacks. Error wins coincident cancel;
 * successful effects and pre-I/O stores survive cancellation/failure. Terminals
 * silent; no retries, cleanup, native MMIO backend or hardware operation. */
enum axd_radio_control_phase axd_radio_control_step(struct axd_radio_control *,axd_d11_io_fn,void *);
enum axd_radio_control_phase axd_radio_control_cancel(struct axd_radio_control *);
#endif
