/* SPDX-License-Identifier: MIT */
#ifndef AXD_SI_CLOCK_H
#define AXD_SI_CLOCK_H
#include "si_select.h"
#include "chipcommon_clock.h"

struct axd_si_clock_context {
    struct axd_si_context si;
    struct axd_si_state state;
    int32_t revision;
    uint32_t capabilities, bus_core_revision;
    uint32_t irq_core_id, disable_callback, restore_callback, irq_context;
    uint8_t chipcommon_owned;
};
enum axd_si_clock_kind {
    AXD_SIC_MAP = 1, AXD_SIC_PCI_WRITE32, AXD_SIC_PCI_READ32,
    AXD_SIC_STORE_BARRIER, AXD_SIC_FULL_BARRIER,
    AXD_SIC_READ32, AXD_SIC_WRITE32, AXD_SIC_DELAY_US,
    AXD_SIC_IRQ_DISABLE, AXD_SIC_IRQ_RESTORE
};
enum axd_si_clock_resource {
    AXD_SIC_NONE = 0, AXD_SIC_CORE, AXD_SIC_PRIMARY_WRAPPER,
    AXD_SIC_SECONDARY_WRAPPER, AXD_SIC_CHIPCOMMON, AXD_SIC_PCI_CONFIG
};
struct axd_si_clock_operation {
    enum axd_si_clock_kind kind;
    enum axd_si_clock_resource resource;
    uint32_t address, offset, size, value, map_type;
    uint32_t callback, callback_context;
    const struct axd_si_state *state; /* Read-only during IRQ callback only. */
};
struct axd_si_clock_reply {
    uint32_t value; /* MMIO/PCI read, MAP result, or disable's actual token. */
    uint8_t irq_state_known;
    struct axd_si_state irq_state;
};
typedef int (*axd_si_clock_io_fn)(void *context,
                                 const struct axd_si_clock_operation *operation,
                                 struct axd_si_clock_reply *reply);
enum axd_si_clock_phase {
    AXD_SIC_INVALID = 0, AXD_SIC_RUNNING, AXD_SIC_TRACE_COMPLETE,
    AXD_SIC_DEPENDENCY, AXD_SIC_FAILED, AXD_SIC_CANCELLED
};
enum axd_si_clock_error {
    AXD_SIC_NO_ERROR = 0, AXD_SIC_BAD_ARGUMENT, AXD_SIC_BINDING,
    AXD_SIC_SELECT_ERROR, AXD_SIC_CLOCK_ERROR, AXD_SIC_IO_ERROR
};
enum axd_si_clock_dependency {
    AXD_SIC_NO_DEPENDENCY = 0, AXD_SIC_SELECTION, AXD_SIC_CLOCK_OWNER,
    AXD_SIC_IRQ_STATE
};
struct axd_si_clock {
    enum axd_si_clock_phase phase;
    enum axd_si_clock_error error;
    enum axd_si_clock_dependency dependency;
    struct axd_si_clock_context context;
    struct axd_si_select selector;
    struct axd_cc_clock clock_job;
    uint32_t mode, saved_index, mapping, irq_token, return_value;
    unsigned stage, after_child, fast_mapping, map_skipped;
    unsigned disable_called, restore_called, state_valid, body_completed;
    unsigned selection_completed, restoration_completed;
    uint64_t attempted_ops, completed_ops;
};
/* Actual HND si_clkctl_cc resource wrapper. Signed revision<=5 returns0 before
 * unused ownership/catalog/IRQ checks. Fast bus mapping uses u32(current+3000),
 * skips only original/derived zero, and never switches index or invokes IRQ.
 * Ordinary path runs real selectors and independently guarded IRQ callbacks.
 * A successful IRQ callback MUST report its actual resulting SIstate with
 * irq_state_known1, even when unchanged. Disable's saved restoration index is
 * reloaded from that post-callback state, not assumed unchanged. Missing state
 * ends DEPENDENCY with state_valid0, without invented restoration.
 * Catalog/config/callback bindings remain immutable; callbacks may affect only
 * actual SI state reported in reply. Identities are ARM32 labels, not calls
 * through host pointers. The backend owns actual IRQ/MMIO/PCI/mapping APIs.
 */
int axd_si_clock_begin(struct axd_si_clock *job, uint32_t mode,
                        const struct axd_si_clock_context *context);
/* At most one user callback per step. Updated selector caches/state and CC
 * observations propagate on failed/cancelled steps. Callback error wins; a
 * successful cancelling IRQ callback still commits token/reported state.
 * Generic selected mapping0 is rejected only at a reached CC read/write;
 * capability/mode skips still restore selection and IRQ. Barriers/crystal PCI
 * can precede that first dereference. Return value is a mode predicate, not
 * readiness. No automatic unwind, restore, unmap or retry after a terminal exit.
 * begin snapshots context before clearing, including &job.context. Callbacks
 * may cancel, never reenter or mutate jobs; reply reports external effects.
 */
enum axd_si_clock_phase axd_si_clock_step(struct axd_si_clock *job,
                                          axd_si_clock_io_fn io, void *context);
enum axd_si_clock_phase axd_si_clock_cancel(struct axd_si_clock *job);
#endif
