/* SPDX-License-Identifier: MIT */
#ifndef AXD_CORE_RESET_H
#define AXD_CORE_RESET_H
#include "core_reset_low.h"
#include "dma_reset.h"
#include "phy_reset.h"
#include "wl_clock.h"

#define AXD_CORE_MAX_QUEUES 72u
struct axd_core_dma_slot {
    struct axd_wake_u32 primary, secondary;
    struct axd_dma_reset_context tx, paired_tx, rx;
};
struct axd_core_reset_context {
    struct axd_wl_clock_context clock;
    struct axd_core_reset_low_context low;
    struct axd_mctrl_context control;
    struct axd_psm_policy policy;
    struct axd_phy_reset_context phy;
    struct axd_wake_u32 band, phy_shared;
    uint32_t band_flags, tx_count, word164, word22c;
    uint8_t no_reset, paired_dma, rx_fifo3, extended_supported;
    uint8_t tail_byte, phy_shared_clock;
    size_t dma_slots;
    struct axd_core_dma_slot dma[AXD_CORE_MAX_QUEUES];
};
enum axd_core_reset_path {
    AXD_CORE_WL = 1, AXD_CORE_SI, AXD_CORE_DMA, AXD_CORE_LOW,
    AXD_CORE_D11, AXD_CORE_PHY, AXD_CORE_PLL
};
struct axd_core_reset_operation {
    enum axd_core_reset_path path;
    unsigned queue, secondary;
    uint32_t dma_identity;
    union {
        struct axd_wl_clock_operation wl;
        struct axd_si_core_operation si;
        struct axd_dma_operation dma;
        struct axd_core_reset_low_operation low;
        struct axd_d11_operation d11;
        struct axd_phy_reset_operation phy;
        struct axd_clock_operation pll;
    } operation;
};
struct axd_core_reset_reply {
    uint32_t value;
    uint8_t irq_state_known;
    struct axd_si_state irq_state;
};
typedef int (*axd_core_reset_io_fn)(void *, const struct axd_core_reset_operation *,
                                    struct axd_core_reset_reply *);
enum axd_core_reset_phase {
    AXD_CORE_INVALID = 0, AXD_CORE_RUNNING, AXD_CORE_TRACE_COMPLETE,
    AXD_CORE_DEPENDENCY, AXD_CORE_FAILED, AXD_CORE_CANCELLED
};
enum axd_core_reset_error {
    AXD_CORE_NO_ERROR = 0, AXD_CORE_BAD_ARGUMENT, AXD_CORE_UNSUPPORTED,
    AXD_CORE_BINDING, AXD_CORE_BOUNDS, AXD_CORE_CHILD_ERROR, AXD_CORE_IO_ERROR
};
enum axd_core_reset_field {
    AXD_CORE_NO_FIELD = 0, AXD_CORE_BAND, AXD_CORE_PHY_POINTER,
    AXD_CORE_PHY_SHARED, AXD_CORE_PRIMARY_DMA, AXD_CORE_SECONDARY_DMA,
    AXD_CORE_SI_CATALOG
};
struct axd_core_reset {
    enum axd_core_reset_phase phase;
    enum axd_core_reset_error error;
    enum axd_core_reset_field last_field;
    struct axd_core_reset_context context;
    struct axd_wl_clock wl_job;
    struct axd_si_core_job si_job;
    struct axd_dma_reset dma_job;
    struct axd_core_reset_low low_job;
    struct axd_mctrl mctrl_job;
    struct axd_psm_control psm_job;
    struct axd_phy_reset phy_job;
    struct axd_clock_control pll_job;
    uint32_t flags, sample;
    uint8_t original_fast;
    unsigned stage, after_child, queue, secondary, flags_index;
    unsigned metadata_accesses, pending_callouts, si_state_known;
    unsigned initial_up, initial_up_valid, pll_up, pll_up_valid;
    unsigned low_completed, control_reset_completed, phy_completed, pll_completed;
    uint64_t attempted_ops, completed_ops;
};
/* Offline exact core130.0/1 enclosing reset, with copied runtime facts. The
 * lower context's SI/state is canonical; WL fallback binds to it at each call.
 * control.psm is canonical MAC state; clock.d11 owns clock/cache state. PHY
 * identity is phy.band_type/band_revision/phy_pointer. Every real child receives
 * these current snapshots; partial updates always propagate to canonical state.
 * Runtime metadata (including queue count) is immutable through a job. This
 * narrows stock's live reloads; no callback may mutate it. The 72-slot/9-byte
 * bounded policy rejects reached out-of-range indexes, never indexes unchecked.
 * Pointer known0 is reached DEPENDENCY; known1/value0 means actual null. The
 * version samples are retained on successful reads; each subsequent software
 * type/revision store is a separate internal step after its successful SY.
 * band default-flags path requires nonnull band. No assumed DMA handles, core
 * up, auxiliary reset, PHY readiness or successful unknown callouts.
 * begin snapshots own-context aliases before clearing. No device backend,
 * resource acquisition, kernel integration or hardware/RF operation is provided.
 */
int axd_core_reset_begin(struct axd_core_reset *, uint32_t flags,
                          const struct axd_core_reset_context *);
/* One user callback maximum per step. Error wins simultaneous cancellation;
 * successful cancelling callbacks retain effects and child partial state.
 * No invented unwind/restore/final clock request after a terminal exit. */
enum axd_core_reset_phase axd_core_reset_step(struct axd_core_reset *,
                                              axd_core_reset_io_fn, void *);
enum axd_core_reset_phase axd_core_reset_cancel(struct axd_core_reset *);
#endif
