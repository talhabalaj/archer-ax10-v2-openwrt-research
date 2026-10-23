/* SPDX-License-Identifier: MIT */
#ifndef AXD_PSM_CONTROL_H
#define AXD_PSM_CONTROL_H
#include "d11_io.h"
#include "wake_policy.h"
#include <stddef.h>

enum axd_psm_command {
    AXD_PSM_WAIT_WAKE = 1,
    AXD_PSM_SUSPEND_MAIN,
    AXD_PSM_SUSPEND_EXT,
    AXD_PSM_ENABLE_MAIN,
    AXD_PSM_ENABLE_EXT,
    AXD_PSM_OVERRIDE_SET, AXD_PSM_OVERRIDE_CLEAR, AXD_PSM_UPDATE_WAKE
};
struct axd_psm_context {
    uint16_t core, minor, wake_budget_us;
    uint32_t chip_id, main_control, extended_control, mirror_control;
    uint32_t main_depth, extended_depth, wake_reasons, fatal_reason, timeout_flags;
    uint8_t band_present, mute_override, mirror_enabled, wake_hint, mbss_enabled;
    /* Optional nullable-band binding. Default0 retains the legacy explicit
     * band_present predicate. Required1/known0 becomes a dependency only if
     * actual wait-for-wake reaches its band guard, after prior cache/I/O.
     */
    uint8_t band_binding_required, band_presence_known;
};
/* Explicit facts or an immutable input binding for the actual pure predicate.
 * Unknown public-up/policy is a dependency. Known public-up false is the
 * actual no-I/O wrapper return. Known true requires an actual policy result.
 */
struct axd_psm_policy {
    uint8_t public_up_known, public_up, stay_awake_known, stay_awake;
    /* Optional actual predicate snapshot, immutable caller-owned storage outside
     * all jobs through terminal state. Evaluated at the decision point with
     * CURRENT reason word; takes precedence over explicit stay_awake facts.
     */
    const struct axd_wake_inputs *inputs;
};
enum axd_psm_phase {
    AXD_PSM_INVALID = 0, AXD_PSM_RUNNING, AXD_PSM_DONE, AXD_PSM_DEPENDENCY,
    AXD_PSM_FAILED, AXD_PSM_CANCELLED
};
enum axd_psm_result {
    AXD_PSM_NO_RESULT = 0, AXD_PSM_NOOP, AXD_PSM_NESTED,
    AXD_PSM_WOKEN, AXD_PSM_SUSPENDED, AXD_PSM_ENABLED,
    AXD_PSM_OVERRIDE_UPDATED, AXD_PSM_WAKE_UPDATED
};
enum axd_psm_error {
    AXD_PSM_NO_ERROR = 0, AXD_PSM_BAD_ARGUMENT, AXD_PSM_UNSUPPORTED,
    AXD_PSM_IO_ERROR, AXD_PSM_BAD_NESTING, AXD_PSM_DEVICE_GONE,
    AXD_PSM_ACK_MISSING, AXD_PSM_WAKE_STATE, AXD_PSM_ENABLE_STATE,
    AXD_PSM_POLICY_STATE
};
/* Unexecuted callout requirements. They never mean a successful stub ran. */
enum axd_psm_callout {
    AXD_PSM_CALLOUT_NONE = 0, AXD_PSM_WAKE_CONTROL = 1, AXD_PSM_MBSS_PRQ = 2,
    AXD_PSM_FATAL = 4, AXD_PSM_DOWN = 8, AXD_PSM_BAND_STATE = 16
};
struct axd_psm_control {
    enum axd_psm_phase phase;
    enum axd_psm_error error;
    enum axd_psm_result result;
    enum axd_psm_command command;
    struct axd_psm_context context;
    struct axd_psm_policy policy;
    uint32_t override_reason, wake_control_sample;
    uint32_t mirror_pending;
    enum axd_wake_decision policy_decision;
    /* Last full policy result, kept in the job rather than on the stack:
     * its diagnostic trace is too large for a kernel stack frame. */
    struct axd_wake_result policy_result;
    enum axd_wake_field policy_reason;
    unsigned policy_evaluated, policy_accesses;
    uint8_t policy_slot, policy_pass;
    uint32_t last_read, repetitions, wake_limit;
    unsigned stage, after_wake, after_policy, bank, queued, cursor, pending_callouts;
    unsigned wake_control_observed;
    unsigned mirror_commit_cursor, mirror_pending_valid;
    struct axd_d11_operation operations[6];
    uint64_t attempted_ops, completed_ops;
};
/* Explicit core130 context, already owning a running/reset-proven resource.
 * No MMIO backend, resource discovery, real fatal/down/MBSS calls,
 * or kernel synchronization. Context is copied; caller retains updated snapshot.
 */
int axd_psm_control_begin(struct axd_psm_control *job,
                          enum axd_psm_command command,
                          const struct axd_psm_context *context);
/* Existing commands1..5 with optional explicit wake-policy facts. The legacy
 * begin leaves these unknown, preserving dependency behavior. Both context
 * and policy may alias the same job's previous snapshots safely.
 */
int axd_psm_control_begin_policy(struct axd_psm_control *job,
                                 enum axd_psm_command command,
                                 const struct axd_psm_context *context,
                                 const struct axd_psm_policy *policy);
/* Actual generic override set/clear, arbitrary u32 reason including zero.
 * CLEAR always stores old&~reason; zero result runs the wake-control wrapper,
 * even if no reason bit changed. SET only forces cached wake/wait when old
 * reasons==0 and cached wake bit clear. No initial hardware control read.
 */
int axd_psm_override_begin(struct axd_psm_control *job, unsigned set,
                           uint32_t reason, const struct axd_psm_context *context,
                           const struct axd_psm_policy *policy);
/* Top-level wlc_set_wake_ctrl wrapper with explicit public-up/policy facts.
 * Up-true reads control/SY before the stay-awake decision; unknown decision
 * then stops with WAKE_CONTROL dependency after that read. True requests
 * cached wake and waits only when original physical sample lacked the bit;
 * false requests cached wake clear. Cache/override/mirror semantics still apply.
 */
int axd_psm_wake_control_begin(struct axd_psm_control *job,
                               const struct axd_psm_context *context,
                               const struct axd_psm_policy *policy);
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
