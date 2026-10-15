/* SPDX-License-Identifier: MIT */
#ifndef AXD_PSM_START_H
#define AXD_PSM_START_H
#include <stddef.h>
#include <stdint.h>
#include "d11_io.h"

/* Core-130 wowlucode_start helper boundary, not complete device bring-up.
 * Caller must establish exclusive ownership, clocks/reset, uploaded microcode,
 * and any required PHY/SMC/VASIP prerequisites. No backend is supplied.
 */
struct axd_start_context {
    uint16_t core, minor;
    uint32_t main_control, extended_control, mirror_control;
    uint8_t wake_override, mute_override, mirror_enabled, psmx_enabled;
};
enum axd_start_phase {
    AXD_START_INVALID = 0,
    AXD_START_RUNNING,
    AXD_START_SUSPENDED,
    AXD_START_FAILED,
    AXD_START_CANCELLED
};
enum axd_start_error {
    AXD_START_NO_ERROR = 0,
    AXD_START_BAD_ARGUMENT,
    AXD_START_UNSUPPORTED_CORE,
    AXD_START_IO_ERROR,
    AXD_START_MAIN_ACK_MISSING,
    AXD_START_EXT_ACK_MISSING,
    AXD_START_FIFO_LAYOUT
};
struct axd_psm_start {
    enum axd_start_phase phase;
    enum axd_start_error error;
    struct axd_start_context context;
    uint32_t last_read, repetitions, mirror_pending;
    unsigned stage, queued, cursor, bank;
    unsigned mirror_commit_cursor, mirror_pending_valid;
    struct axd_d11_operation operations[6];
    uint64_t attempted_ops, completed_ops;
};
int axd_psm_start_begin(struct axd_psm_start *job,
                        const struct axd_start_context *context);
/* At most one callback per step; no callback for a pure state transition.
 * At most 100000 delay/read iterations per bank, with an initial read and a
 * mandatory final confirmation read, including immediately acknowledged paths.
 * Missing main/extended acknowledgement is terminal fail-stop policy: the stock
 * extended helper dumps a diagnostic then continues FIFO verification. Diagnostic
 * dump internals are not modeled. Success means suspended ACK + FIFO-layout match,
 * not radio readiness. No retry/rollback or kernel synchronization is supplied.
 * Single-threaded: callbacks may cancel, never reenter begin/step or mutate job.
 * Successful callback cancellation survives; simultaneous callback error wins.
 */
enum axd_start_phase axd_psm_start_step(struct axd_psm_start *job,
                                        axd_d11_io_fn io, void *context);
enum axd_start_phase axd_psm_start_cancel(struct axd_psm_start *job);
#endif
