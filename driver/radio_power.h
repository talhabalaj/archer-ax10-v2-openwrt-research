/* SPDX-License-Identifier: MIT */
#ifndef AXD_RADIO_POWER_H
#define AXD_RADIO_POWER_H
#include "radio_control.h"
struct axd_radio_power_context {
    struct axd_radio_control_context radio;
    /* Borrowed immutable private .rodata spans, original LE layout: mode18
     * bytes (three-byte row at0, six-byte rows at4 and12), RCCAL8 bytes
     * (u16 at0 and u32 at4). Outside all jobs until terminal. */
    const uint8_t *mode_data, *rccal_data;
    size_t mode_length, rccal_length;
    struct axd_wake_u32 ac_radio_pointer, ac_radio_data_pointer, clock_word;
    uint32_t rccal_multiplier;
    uint16_t rccal_saved6, rccal_saved8, rccal_saved_a;
};
enum axd_radio_power_command { AXD_RPOWER_LOGEN=1, AXD_RPOWER_SWITCH_ON,
    AXD_RPOWER_ON_REGISTERED };
enum axd_radio_power_phase { AXD_RPOWER_INVALID=0, AXD_RPOWER_RUNNING,
    AXD_RPOWER_TRACE_COMPLETE, AXD_RPOWER_DEPENDENCY, AXD_RPOWER_FAILED,
    AXD_RPOWER_CANCELLED };
enum axd_radio_power_error { AXD_RPOWER_NO_ERROR=0, AXD_RPOWER_BAD_ARGUMENT,
    AXD_RPOWER_BINDING, AXD_RPOWER_BOUNDS, AXD_RPOWER_CHILD_ERROR,
    AXD_RPOWER_IO_ERROR };
enum axd_radio_power_dependency { AXD_RPOWER_NO_DEPENDENCY=0,
    AXD_RPOWER_METADATA, AXD_RPOWER_PREFIX_CHILD, AXD_RPOWER_REGISTER_CHILD,
    AXD_RPOWER_DIVISION_BY_ZERO, AXD_RPOWER_REGTBL_CHANNEL_TAIL,
    AXD_RPOWER_REGTBL_ON_PWRON, AXD_RPOWER_OTHER_CHIP_TAIL };
enum axd_radio_power_field { AXD_RPOWER_NO_FIELD=0, AXD_RPOWER_MODE_DATA,
    AXD_RPOWER_RCCAL_DATA, AXD_RPOWER_AC, AXD_RPOWER_AC_RADIO,
    AXD_RPOWER_AC_DATA, AXD_RPOWER_CLOCK, AXD_RPOWER_CHANMGR,
    AXD_RPOWER_CHANMGR_DATA, AXD_RPOWER_CHIP };
struct axd_radio_power {
    enum axd_radio_power_phase phase;
    enum axd_radio_power_error error;
    enum axd_radio_power_command command;
    enum axd_radio_power_dependency dependency;
    enum axd_radio_power_field last_field;
    struct axd_radio_power_context context;
    struct axd_radio_control control_job;
    struct axd_phy_table_write packed_job;
    uint32_t difference, quotient, division_result;
    uint16_t sample, rccal_first, rcal_value;
    unsigned mode, stage, after_child, index, core_index, part, sequence;
    unsigned poll_count, pll_reads, rcal_reads, rccal_reads[2];
    unsigned pll_locked, rcal_done, rccal_done, rccal_iteration;
    unsigned logen_completed, switch_completed, prefix_completed, software_stores;
    unsigned metadata_accesses;
    uint64_t attempted_ops, completed_ops;
};
/* LOGEN executes the real22 radio modifications for mode0..2, using the private
 * original mode span. SWITCH_ON composes actual AC switch prefix and the full
 * explicit identity50e0 PLL/RCAL/RCCAL branch, then the chanmgr flag/chip tail.
 * Core130.0/1 AC PHY51 identity/resources are checked by the existing prefix.
 * A zero chanmgr flag and chip63178 permit actual switch completion; no radio
 * identity is inferred. Nonzero flag reaches real regtbl/channel dependencies.
 * ON_REGISTERED composes common slot1+AC early stores, real switch, then stops
 * at its mandatory post-switch regtbl-on-poweron child. It NEVER completes the
 * registered radio callback until that real remaining child is implemented.
 * All whole-ON entries use mode0 exactly; mode parameter must be0 for them.
 * Native poll limits101/101/10000 are preserved, including exhaustion fallbacks.
 * A reached zero divisor reports the unresolved ABI zero-divide dependency.
 * Caller pins private spans and supplies actual clock/object/ownership facts;
 * no vendor arrays, arbitrary successful helper or native MMIO backend exists.
 */
int axd_radio_power_begin(struct axd_radio_power *,enum axd_radio_power_command,
                            unsigned mode,const struct axd_radio_power_context *);
/* At most one user callback per step. Own context aliases snapshotted; partial
 * children publish before callbacks and after each transition. Error wins
 * coincident cancel, successful effects/pre-I/O stores and successful child-step
 * commits survive; later stages do not run. Terminals silent, no retries/rollback/cleanup/readiness. */
enum axd_radio_power_phase axd_radio_power_step(struct axd_radio_power *,axd_d11_io_fn,void *);
enum axd_radio_power_phase axd_radio_power_cancel(struct axd_radio_power *);
#endif
