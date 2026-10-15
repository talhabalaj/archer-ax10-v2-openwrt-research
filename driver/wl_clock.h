/* SPDX-License-Identifier: MIT */
#ifndef AXD_WL_CLOCK_H
#define AXD_WL_CLOCK_H
#include "clock_control.h"
#include "si_clock.h"

struct axd_wl_clock_context {
    struct axd_clock_context d11;
    struct axd_si_clock_context fallback;
};
enum axd_wl_clock_path { AXD_WLC_NO_PATH = 0, AXD_WLC_D11, AXD_WLC_SI };
struct axd_wl_clock_operation {
    enum axd_wl_clock_path path;
    union {
        struct axd_clock_operation d11;
        struct axd_si_clock_operation si;
    } operation;
};
struct axd_wl_clock_reply {
    uint32_t value;
    uint8_t irq_state_known;
    struct axd_si_state irq_state;
};
typedef int (*axd_wl_clock_io_fn)(void *context,
                                 const struct axd_wl_clock_operation *operation,
                                 struct axd_wl_clock_reply *reply);
enum axd_wl_clock_phase {
    AXD_WLC_INVALID = 0, AXD_WLC_RUNNING, AXD_WLC_TRACE_COMPLETE,
    AXD_WLC_DEPENDENCY, AXD_WLC_FAILED, AXD_WLC_CANCELLED
};
enum axd_wl_clock_error {
    AXD_WLC_NO_ERROR = 0, AXD_WLC_BAD_ARGUMENT, AXD_WLC_UNSUPPORTED,
    AXD_WLC_BINDING, AXD_WLC_D11_ERROR, AXD_WLC_SI_ERROR, AXD_WLC_IO_ERROR
};
struct axd_wl_clock {
    enum axd_wl_clock_phase phase;
    enum axd_wl_clock_error error;
    enum axd_wl_clock_path path;
    struct axd_wl_clock_context context;
    struct axd_clock_control d11_job;
    struct axd_si_clock si_job;
    uint32_t mode, return_value, override_sample;
    unsigned stage, return_valid, cache_committed, reason_committed;
    uint64_t attempted_ops, completed_ops;
};
/* Exact WL clock-selection caller for core130 minor0/1. d11.si_capabilities
 * bit10000000 selects the real D11 child; otherwise the real SI child runs.
 * Duplicated fallback.capabilities must match when that branch is reached.
 * Unused direct flags and fallback catalog/config are not validated early.
 * Fallback return is NOT readiness. Its low8 cache store and subsequent inline
 * reason10 word update are distinct internal steps; no PSM helper is called.
 * Context/catalog/config/callback bindings remain immutable and caller-owned.
 * begin snapshots before clearing, including an aliased &job.context.
 */
int axd_wl_clock_begin(struct axd_wl_clock *job, uint32_t mode,
                        const struct axd_wl_clock_context *context);
/* At most one user callback per step. Tagged original child operations retain
 * D11/CC/map/wrapper/PCI/IRQ distinctions and original IRQ state requirements.
 * operation.si.state is read-only and valid only during its IRQ callback.
 * Every child partial context propagates, including failed/cancelled effects.
 * Error wins over cancellation; successful cancelling callback effects survive.
 * Callbacks may cancel, never reenter or mutate jobs. No invented unwind,
 * restoration, retry, ready result or operating-system backend is supplied.
 */
enum axd_wl_clock_phase axd_wl_clock_step(struct axd_wl_clock *job,
                                          axd_wl_clock_io_fn io, void *context);
enum axd_wl_clock_phase axd_wl_clock_cancel(struct axd_wl_clock *job);
#endif
