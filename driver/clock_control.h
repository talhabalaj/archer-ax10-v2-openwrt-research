/* SPDX-License-Identifier: MIT */
#ifndef AXD_CLOCK_CONTROL_H
#define AXD_CLOCK_CONTROL_H
#include "d11_io.h"
#include <stddef.h>

enum axd_clock_command { AXD_CLOCK_SELECT = 1, AXD_CLOCK_PLL, AXD_CLOCK_SECONDARY };
enum axd_clock_region { AXD_CLOCK_CURRENT_D11 = 1, AXD_CLOCK_D11_UNIT1 };
struct axd_clock_operation {
    enum axd_d11_kind kind;
    enum axd_clock_region region;
    uint32_t offset, value;
};
struct axd_clock_context {
    uint16_t core, minor;
    uint32_t si_capabilities, si_word20, wake_reasons;
    uint8_t core_clock_valid, fast_clock, pll_inhibit, core_up;
    uint8_t rsdb, secondary_owned;
};
enum axd_clock_phase {
    AXD_CLOCK_INVALID = 0, AXD_CLOCK_RUNNING, AXD_CLOCK_TRACE_COMPLETE,
    AXD_CLOCK_DEPENDENCY, AXD_CLOCK_FAILED, AXD_CLOCK_CANCELLED
};
enum axd_clock_error {
    AXD_CLOCK_NO_ERROR = 0, AXD_CLOCK_BAD_ARGUMENT,
    AXD_CLOCK_UNSUPPORTED, AXD_CLOCK_IO_ERROR
};
enum axd_clock_dependency { AXD_CLOCK_NO_DEPENDENCY = 0, AXD_CLOCK_HND_FALLBACK, AXD_CLOCK_SECONDARY_OWNER };
struct axd_clock_control {
    enum axd_clock_phase phase;
    enum axd_clock_error error;
    enum axd_clock_command command;
    enum axd_clock_dependency dependency;
    struct axd_clock_context context;
    uint32_t argument, last_read, sample_a, last_status;
    uint32_t rmw_clear, rmw_set;
    unsigned stage, after_rmw, queued, cursor, repetitions, delay_count;
    unsigned status_observed, writes_since_status, poll_exhausted, request_skipped;
    struct axd_clock_operation operations[4];
    uint64_t attempted_ops, completed_ops;
};
typedef int (*axd_clock_io_fn)(void *context,
                             const struct axd_clock_operation *operation,
                             uint32_t *read_value);
/* Select uses the stock mode argument (zero=fast, nonzero=dynamic); PLL and
 * secondary arguments are boolean. SI predicates and secondary ownership are
 * explicit caller-provided context, not calls implemented by this library.
 * Secondary backend must provide exclusive, IRQ-safe unit1 access and preserve
 * caller mapping/interrupt state. No OS transport is supplied. Unknown HND
 * fallback or missing secondary owner returns DEPENDENCY, never a success stub.
 */
int axd_clock_control_begin(struct axd_clock_control *job,
                            enum axd_clock_command command, uint32_t argument,
                            const struct axd_clock_context *context);
/* One synchronous event per step maximum. Stock helpers return void and may
 * finish with no ready sample, skipped polls or exhausted waits. TRACE_COMPLETE
 * is not clock/PLL readiness: inspect observation freshness, sample_a, skipped
 * request and poll_exhausted. last_status is the last current-D11 clock read;
 * writes_since_status flags a newer successful write. It can be a pre-write RMW
 * sample, and unit1 samples never overwrite it. No rollback/automatic retry.
 * Single-threaded: callbacks may cancel, not reenter or mutate fields. Successful
 * callback cancellation survives; simultaneous callback error wins.
 */
enum axd_clock_phase axd_clock_control_step(struct axd_clock_control *job,
                                            axd_clock_io_fn io, void *context);
enum axd_clock_phase axd_clock_control_cancel(struct axd_clock_control *job);
#endif
