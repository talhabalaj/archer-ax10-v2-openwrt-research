/* SPDX-License-Identifier: MIT */
#ifndef AXD_PHY_CLOCK_H
#define AXD_PHY_CLOCK_H
#include "d11_io.h"
#include <stddef.h>

enum axd_phy_clock_command {
    AXD_PHY_CLOCK_ENABLE = 1, AXD_PHY_CLOCK_DISABLE,
    AXD_PHY_CLOCK_RESET_PREPARE, AXD_PHY_CLOCK_RECORD_BANDWIDTH
};
enum axd_phy_clock_region {
    AXD_PHY_CLOCK_NO_REGION = 0,
    AXD_PHY_CLOCK_CURRENT_WRAPPER, AXD_PHY_CLOCK_UNIT1_WRAPPER
};
struct axd_phy_clock_operation {
    enum axd_d11_kind kind;
    enum axd_phy_clock_region region;
    uint32_t offset, value;
};
struct axd_phy_clock_context {
    uint16_t core, minor, phy_type, phy_revision, chanspec, bandwidth_state;
    uint32_t chip_id;
    uint8_t interconnect, rsdb, workaround_done, phy_clock;
    uint8_t current_owned, unit1_owned;
};
enum axd_phy_clock_phase {
    AXD_PHY_CLOCK_INVALID = 0, AXD_PHY_CLOCK_RUNNING, AXD_PHY_CLOCK_TRACE_COMPLETE,
    AXD_PHY_CLOCK_DEPENDENCY, AXD_PHY_CLOCK_FAILED, AXD_PHY_CLOCK_CANCELLED
};
enum axd_phy_clock_error {
    AXD_PHY_CLOCK_NO_ERROR = 0, AXD_PHY_CLOCK_BAD_ARGUMENT,
    AXD_PHY_CLOCK_UNSUPPORTED, AXD_PHY_CLOCK_IO_ERROR
};
enum axd_phy_clock_dependency {
    AXD_PHY_CLOCK_NO_DEPENDENCY = 0, AXD_PHY_CLOCK_CURRENT_OWNER, AXD_PHY_CLOCK_UNIT1_OWNER
};
struct axd_phy_clock {
    enum axd_phy_clock_phase phase;
    enum axd_phy_clock_error error;
    enum axd_phy_clock_dependency dependency;
    enum axd_phy_clock_command command;
    struct axd_phy_clock_context context;
    uint32_t bandwidth_flags, mask, value, initial_sample, write_value, last_read;
    uint32_t current_sample, unit1_sample;
    unsigned current_observed, current_writes_since_sample;
    unsigned unit1_observed, unit1_writes_since_sample;
    unsigned current_updates, unit1_updates;
    unsigned stage, resume, region, queued, cursor;
    struct axd_phy_clock_operation operations[2];
    uint64_t attempted_ops, completed_ops;
};
typedef int (*axd_phy_clock_io_fn)(void *context,
                                  const struct axd_phy_clock_operation *operation,
                                  uint32_t *read_value);
/* Explicit identity: D11 core130 minor0/1, PHY type0xb/rev51, chip63178(0xf6ca),
 * AI interconnect1/3/4. Owners supply selected wrapper resources and stable,
 * exclusive mapping/window lifetime/restoration; no mapping or IRQ calls run.
 * RESET_PREPARE emits bandwidth core-flags+delay2 then ENABLE, not PHY pulse,
 * TSSI, analog reset or bandwidth-state recording. RECORD_BANDWIDTH is the
 * separate software store required after pulse+100us in the actual coordinator.
 * ENABLE/DISABLE set phy_clock at their entry before the owner dependency,
 * as stock sets hw+210 before coreflags. PREPARE sets it after preparation.
 * begin copies context first, including an aliased &job.context.
 */
int axd_phy_clock_begin(struct axd_phy_clock *job, enum axd_phy_clock_command command,
                        const struct axd_phy_clock_context *context);
/* At most one synchronous callback or bounded internal transition per step.
 * Core-flags: R32(408),SY,ST,W32((first&~mask)|value),R32(408),SY.
 * Final read is an observation, never a readiness predicate. Successful final
 * SY commits update count and unit1 workaround_done, even on cancellation.
 * Error wins over cancellation and records no successful callback/state commit.
 * Missing owner is DEPENDENCY at its reached point, without invented cleanup.
 * No retries/rollback, mapping/index/IRQ events, or physical clock-ready result.
 * Single-threaded; callbacks may cancel, never reenter or mutate job fields.
 */
enum axd_phy_clock_phase axd_phy_clock_step(struct axd_phy_clock *job,
                                           axd_phy_clock_io_fn io, void *context);
enum axd_phy_clock_phase axd_phy_clock_cancel(struct axd_phy_clock *job);
#endif
