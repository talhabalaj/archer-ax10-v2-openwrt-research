/* SPDX-License-Identifier: MIT */
#ifndef AXD_PSM_CONTROL_H
#define AXD_PSM_CONTROL_H
#include "d11_io.h"
#include <stddef.h>

enum axd_psm_command {
    AXD_PSM_WAIT_WAKE = 1,
    AXD_PSM_SUSPEND_MAIN,
    AXD_PSM_SUSPEND_EXT,
    AXD_PSM_ENABLE_MAIN,
    AXD_PSM_ENABLE_EXT
};
struct axd_psm_context {
    uint16_t core, minor, wake_budget_us;
    uint32_t chip_id, main_control, extended_control, mirror_control;
    uint32_t main_depth, extended_depth, wake_reasons, fatal_reason, timeout_flags;
    uint8_t band_present, mute_override, mirror_enabled, wake_hint, mbss_enabled;
};
enum axd_psm_phase {
    AXD_PSM_INVALID = 0, AXD_PSM_RUNNING, AXD_PSM_DONE, AXD_PSM_DEPENDENCY,
    AXD_PSM_FAILED, AXD_PSM_CANCELLED
};
enum axd_psm_result {
    AXD_PSM_NO_RESULT = 0, AXD_PSM_NOOP, AXD_PSM_NESTED,
    AXD_PSM_WOKEN, AXD_PSM_SUSPENDED, AXD_PSM_ENABLED
};
enum axd_psm_error {
    AXD_PSM_NO_ERROR = 0, AXD_PSM_BAD_ARGUMENT, AXD_PSM_UNSUPPORTED,
    AXD_PSM_IO_ERROR, AXD_PSM_BAD_NESTING, AXD_PSM_DEVICE_GONE,
    AXD_PSM_ACK_MISSING, AXD_PSM_WAKE_STATE, AXD_PSM_ENABLE_STATE
};
/* Unexecuted callout requirements. They never mean a successful stub ran. */
enum axd_psm_callout {
    AXD_PSM_CALLOUT_NONE = 0, AXD_PSM_WAKE_CONTROL = 1, AXD_PSM_MBSS_PRQ = 2,
    AXD_PSM_FATAL = 4, AXD_PSM_DOWN = 8
};
struct axd_psm_control {
    enum axd_psm_phase phase;
    enum axd_psm_error error;
    enum axd_psm_result result;
    enum axd_psm_command command;
    struct axd_psm_context context;
    uint32_t last_read, repetitions, wake_limit;
    unsigned stage, after_wake, bank, queued, cursor, pending_callouts;
    struct axd_d11_operation operations[6];
    uint64_t attempted_ops, completed_ops;
};
/* Explicit core130 context, already owning a running/reset-proven resource.
 * No MMIO backend, resource discovery, real fatal/down/wake-control/MBSS calls,
 * or kernel synchronization. Context is copied; caller retains updated snapshot.
 */
int axd_psm_control_begin(struct axd_psm_control *job,
                          enum axd_psm_command command,
                          const struct axd_psm_context *context);
/* At most one callback per step, including delay events. Counter-based budgets:
 * suspend: 8300 delays plus initial/final reads; wake: 40us then ceil(budget/10)
 * delays plus initial/final SHM reads; main enable: 1001 reads/1000 delays while3.
 * Nesting overflow/underflow rejects rather than reproducing unchecked wrapping.
 * Failure predicates stop before diagnostic register reads/fatal/down callouts,
 * with pending_callouts recorded (not executed); main
 * enable ends DEPENDENCY when wake-control/MBSS work is required. Stock can
 * continue through returning fatal helpers. Any non-2 enable state fails even
 * when the stock timeout flag suppresses fatal and permits later cleanup; that
 * case has no pending FATAL and leaves wake reasons unchanged. No rollback/retry.
 * Single-threaded; callback may cancel, never reenter or mutate job. Successful
 * callback cancellation persists; simultaneous callback error takes precedence.
 */
enum axd_psm_phase axd_psm_control_step(struct axd_psm_control *job,
                                        axd_d11_io_fn io, void *context);
enum axd_psm_phase axd_psm_control_cancel(struct axd_psm_control *job);
#endif
