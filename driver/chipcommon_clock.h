/* SPDX-License-Identifier: MIT */
#ifndef AXD_CHIPCOMMON_CLOCK_H
#define AXD_CHIPCOMMON_CLOCK_H
#include <stddef.h>
#include <stdint.h>

enum axd_cc_clock_kind {
    AXD_CC_STORE_BARRIER = 1, AXD_CC_FULL_BARRIER,
    AXD_CC_READ32, AXD_CC_WRITE32, AXD_CC_PCI_READ32, AXD_CC_PCI_WRITE32,
    AXD_CC_DELAY_US
};
enum axd_cc_clock_region { AXD_CC_NONE = 0, AXD_CC_CHIPCOMMON, AXD_CC_PCI_CONFIG };
struct axd_cc_clock_operation {
    enum axd_cc_clock_kind kind;
    enum axd_cc_clock_region region;
    uint32_t offset, value;
};
struct axd_cc_clock_context {
    int32_t revision; /* Signed ChipCommon revision, not D11 revision. */
    uint32_t capabilities, bus, bus_core_id, bus_core_revision;
    uint8_t chipcommon_owned, pci_config_owned;
};
enum axd_cc_clock_phase {
    AXD_CC_INVALID = 0, AXD_CC_RUNNING, AXD_CC_TRACE_COMPLETE,
    AXD_CC_DEPENDENCY, AXD_CC_FAILED, AXD_CC_CANCELLED
};
enum axd_cc_clock_error { AXD_CC_NO_ERROR = 0, AXD_CC_BAD_ARGUMENT, AXD_CC_IO_ERROR };
enum axd_cc_clock_dependency { AXD_CC_NO_DEPENDENCY = 0, AXD_CC_OWNER, AXD_CC_PCI_OWNER };
struct axd_cc_clock {
    enum axd_cc_clock_phase phase;
    enum axd_cc_clock_error error;
    enum axd_cc_clock_dependency dependency;
    struct axd_cc_clock_context context;
    uint32_t mode, last_read, last_cc_sample, last_cc_offset, last_status;
    uint32_t rmw_value, crystal_input, crystal_output, crystal_enable;
    int32_t crystal_return;
    unsigned stage, resume, queued, cursor, crystal_on, crystal_called, crystal_return_valid;
    unsigned return_value, cc_sample_observed, cc_writes_since_sample;
    unsigned status_observed, clock_writes_since_status, poll_delays, poll_exhausted;
    struct axd_cc_clock_operation operations[3];
    uint64_t attempted_ops, completed_ops;
};
typedef int (*axd_cc_clock_io_fn)(void *context,
                                 const struct axd_cc_clock_operation *operation,
                                 uint32_t *read_value);
/* Operation body only: the caller must own the correct ChipCommon resource and
 * provide IRQ-safe mapping/window lifetime and restoration. No selection/IRQ
 * callback is simulated. Missing ownership is DEPENDENCY, not stock's null-fast-
 * mapping skip. PCI config ownership is needed only on reached crystal I/O.
 * Signed revisions <=5 return false before prerequisites or callbacks. Other
 * modes than 0/2 emit no clock work and return false; the capability gate can
 * also skip work. Composite WL fallback cache/wake updates remain outside.
 * begin snapshots context before clearing job, including &job.context.
 */
int axd_cc_clock_begin(struct axd_cc_clock *job, uint32_t mode,
                       const struct axd_cc_clock_context *context);
/* At most one synchronous callback or bounded internal transition per step.
 * TRACE_COMPLETE and return_value report the mode predicate, not readiness.
 * PMU waits: initial read plus <=1500 delay10/read iterations, no final read.
 * Crystal helper's ordinary return is recorded/ignored; callback error is not.
 * CC observations exclude PCI reads. Successful callbacks update telemetry even
 * when cancelling; error wins over concurrent cancellation and counts no success.
 * Terminal states emit no further I/O. No rollback, automatic retry or cleanup.
 * Single-threaded: callbacks may cancel, not reenter or mutate job fields.
 */
enum axd_cc_clock_phase axd_cc_clock_step(struct axd_cc_clock *job,
                                         axd_cc_clock_io_fn io, void *context);
enum axd_cc_clock_phase axd_cc_clock_cancel(struct axd_cc_clock *job);
#endif
