/* SPDX-License-Identifier: MIT */
#ifndef AXD_MCTRL_H
#define AXD_MCTRL_H
#include "psm_control.h"

/* Canonical PSM state plus the reset helper's additional software fields.
 * No layout correspondence to vendor objects is implied. mute_override is
 * the existing normalized nonzero-word predicate, not a raw register image.
 */
struct axd_mctrl_context {
    struct axd_psm_context psm;
    uint8_t suspended_bitmap[9], suspended_count;
    uint16_t reset_halfword;
};
enum axd_mctrl_phase {
    AXD_MCTRL_INVALID = 0, AXD_MCTRL_RUNNING, AXD_MCTRL_DONE,
    AXD_MCTRL_FAILED, AXD_MCTRL_CANCELLED
};
enum axd_mctrl_error {
    AXD_MCTRL_NO_ERROR = 0, AXD_MCTRL_BAD_ARGUMENT,
    AXD_MCTRL_UNSUPPORTED, AXD_MCTRL_IO_ERROR
};
struct axd_mctrl {
    enum axd_mctrl_phase phase;
    enum axd_mctrl_error error;
    struct axd_mctrl_context context;
    uint32_t mask, value, effective, mirror_pending;
    unsigned reset, prepared, cursor, queued, mirror_pending_valid;
    struct axd_d11_operation operations[4];
    uint64_t attempted_ops, completed_ops;
};
/* Exact main-control update: (cache & ~mask) | value. Value is NOT masked.
 * A cache match performs no writes, even with changed override predicates.
 * Both entry points snapshot own-context aliases before clearing the job.
 */
int axd_mctrl_begin(struct axd_mctrl *job, uint32_t mask, uint32_t value,
                    const struct axd_mctrl_context *context);
/* First step clears main cache, nine bitmap bytes, count, wake/mute predicates
 * and reset_halfword before the FFFFFFFF/04000400 update. Both depths survive.
 * Extended cache is untouched; its enclosing reset write is separately owned.
 */
int axd_mctrl_reset_begin(struct axd_mctrl *job,
                          const struct axd_mctrl_context *context);
/* At most one synchronous D11 callback per step. No reads, polls, readbacks,
 * cleanup, retries or MMIO backend. Callback error wins simultaneous cancel;
 * successful cancelling callbacks retain their completed effects. Terminal
 * steps/cancel are silent. Callback may cancel, never reenter/mutate the job.
 */
enum axd_mctrl_phase axd_mctrl_step(struct axd_mctrl *job,
                                    axd_d11_io_fn io, void *context);
enum axd_mctrl_phase axd_mctrl_cancel(struct axd_mctrl *job);
#endif
