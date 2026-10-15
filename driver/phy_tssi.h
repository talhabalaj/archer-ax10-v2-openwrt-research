/* SPDX-License-Identifier: MIT */
#ifndef AXD_PHY_TSSI_H
#define AXD_PHY_TSSI_H
#include "phy_table.h"

enum axd_tssi_command { AXD_TSSI_SNAPSHOT = 1, AXD_TSSI_RESTORE };
struct axd_tssi_context {
    struct axd_phy_table_context hardware;
    uint16_t saved[4];
    uint8_t valid;
};
enum axd_tssi_phase {
    AXD_TSSI_INVALID = 0, AXD_TSSI_RUNNING, AXD_TSSI_TRACE_COMPLETE,
    AXD_TSSI_DEPENDENCY, AXD_TSSI_FAILED, AXD_TSSI_CANCELLED
};
enum axd_tssi_error {
    AXD_TSSI_NO_ERROR = 0, AXD_TSSI_BAD_ARGUMENT, AXD_TSSI_MISSING_SNAPSHOT,
    AXD_TSSI_PHY_ERROR, AXD_TSSI_TABLE_ERROR, AXD_TSSI_IO_ERROR
};
struct axd_phy_tssi {
    enum axd_tssi_phase phase;
    enum axd_tssi_error error;
    enum axd_tssi_command command;
    struct axd_tssi_context context;
    struct axd_phy_register phy_job;
    struct axd_phy_table table_job;
    unsigned stage, slot, pending_callouts;
    uint64_t attempted_ops, completed_ops;
};
/* Actual PHY51 idle-TSSI callback, all explicit core counts1..4. Outer caller
 * must already own/suspend/enter PHY and satisfy wake/power/clock prerequisites;
 * this is not the enclosing idle-TSSI MAC/PHY-wake wrapper. Slots3/2 use actual
 * table29/indexffff reads in SNAPSHOT; RESTORE skips their ffff no-op modifies.
 * Slots1/0 use845/645 and preserve fresh upper6 bits during mask3ff restores.
 * The four-slot saved[]/valid is the callback buffer; lower PHY convenience
 * saved_tssi[] is untouched. SNAPSHOT invalidates active valid bits at ENTRY.
 * Direct slots commit after successful read SY (also cancelling SY). Table
 * slots commit only after the complete table wrapper, including guard/MAC
 * operations; word_valid alone cannot commit a TSSI slot. Partial buffers stay
 * explicit, with no retry/cleanup. RESTORE requires all active valid bits first.
 */
int axd_phy_tssi_begin(struct axd_phy_tssi *job, enum axd_tssi_command command,
                       const struct axd_tssi_context *context);
/* One user callback maximum per step. Successful-cancel effects propagate;
 * callback error wins. Child contexts/diagnostics remain available, including
 * table/PSM dependencies after a valid partial data word. Callbacks may cancel,
 * not reenter begin/step or mutate fields; single-threaded, terminal states silent.
 */
enum axd_tssi_phase axd_phy_tssi_step(struct axd_phy_tssi *job,
                                     axd_d11_io_fn io, void *context);
enum axd_tssi_phase axd_phy_tssi_cancel(struct axd_phy_tssi *job);
#endif
