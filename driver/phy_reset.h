/* SPDX-License-Identifier: MIT */
#ifndef AXD_PHY_RESET_H
#define AXD_PHY_RESET_H
#include "bf_clean.h"
#include "idle_tssi.h"
#include "phy_clock.h"

struct axd_reset_u16 { uint16_t value; uint8_t known; };
struct axd_phy_reset_context {
    struct axd_idle_tssi_context idle;
    struct axd_phy_clock_context clock;
    struct axd_bf_clean_context bf;
    uint16_t band_type, band_revision;
    struct axd_reset_u16 public_revision;
    struct axd_wake_u32 phy_pointer, analog_module, analog_table;
    struct axd_wake_u32 analog_context, analog_context_phy;
    struct axd_wake_u32 idle_module, idle_table, idle_context, idle_context_phy;
    struct axd_wake_u8 idle_callback, analog_callback;
    struct axd_wake_u32 bf_flags, txbf_pointer;
    struct axd_wake_u8 public_bf, txbf_enabled;
    uint32_t wlc_identity, hardware_identity;
};
enum axd_reset_region {
    AXD_RESET_NO_REGION = 0, AXD_RESET_D11,
    AXD_RESET_CURRENT_WRAPPER, AXD_RESET_UNIT1_WRAPPER
};
struct axd_phy_reset_operation {
    enum axd_d11_kind kind;
    enum axd_reset_region region;
    uint32_t offset, value;
};
typedef int (*axd_phy_reset_io_fn)(void *context,
                                  const struct axd_phy_reset_operation *operation,
                                  uint32_t *read_value);
enum axd_reset_phase {
    AXD_RESET_INVALID = 0, AXD_RESET_RUNNING, AXD_RESET_TRACE_COMPLETE,
    AXD_RESET_DEPENDENCY, AXD_RESET_FAILED, AXD_RESET_CANCELLED
};
enum axd_reset_error {
    AXD_RESET_NO_ERROR = 0, AXD_RESET_BAD_ARGUMENT, AXD_RESET_UNSUPPORTED,
    AXD_RESET_BINDING, AXD_RESET_IDLE_ERROR, AXD_RESET_CLOCK_ERROR,
    AXD_RESET_PHY_ERROR, AXD_RESET_BF_ERROR, AXD_RESET_IO_ERROR
};
enum axd_reset_field {
    AXD_RESET_NO_FIELD = 0, AXD_RESET_PUBLIC_REVISION, AXD_RESET_PHY_POINTER,
    AXD_RESET_IDLE_MODULE, AXD_RESET_IDLE_TABLE, AXD_RESET_IDLE_CALLBACK,
    AXD_RESET_ANALOG_MODULE, AXD_RESET_ANALOG_TABLE,
    AXD_RESET_ANALOG_CALLBACK, AXD_RESET_ANALOG_CONTEXT, AXD_RESET_ANALOG_CONTEXT_PHY,
    AXD_RESET_BF_FLAGS, AXD_RESET_PUBLIC_BF,
    AXD_RESET_TXBF_POINTER, AXD_RESET_TXBF_ENABLED
};
struct axd_phy_reset {
    enum axd_reset_phase phase;
    enum axd_reset_error error;
    struct axd_phy_reset_context context;
    struct axd_idle_tssi idle_job;
    struct axd_phy_clock clock_job;
    struct axd_phy_register phy_job;
    struct axd_bf_clean bf_job;
    enum axd_reset_field last_field;
    unsigned stage, after_child, pending_callouts, metadata_accesses;
    unsigned snapshot_completed, prepare_completed, pulse_completed;
    unsigned bandwidth_recorded, analog_completed, restore_completed, bf_completed;
    uint64_t attempted_ops, completed_ops;
};
/* Exact target AC/band-PHY51/core130/chip63178 reset path. Other band types or
 * revisions are rejected before I/O, not treated as a skipped pulse. Public
 * revision is separate: it gates only the two outer TSSI calls. Stable copied
 * facts are re-evaluated at their actual reached points; known0 => DEPENDENCY,
 * known values other than1 => failure. Pointers are explicit ARM32 labels;
 * required null pointers fail BINDING rather than emulating a stock crash.
 * Analog/idle callback facts mean the actual registered AC implementation
 * (0 absent,1 present), not arbitrary callbacks. Owners acquire/validate these
 * bindings and keep all external policy/BF storage immutable outside all jobs.
 * A present analog callback requires context->PI to match phy_pointer, binding
 * the callback's descriptor/base accesses to the supplied PHY resource.
 * ENTRY zeroes the local four 16-bit TSSI slots/validity. Snapshot then occurs
 * BEFORE clock preparation; pulse+100us, BW store, analog, restore, BF follow.
 * The actual bandwidth store requires a PHY pointer even if analog is absent.
 * No mapper, lock, hardware backend, higher device reset or ready result.
 */
int axd_phy_reset_begin(struct axd_phy_reset *job,
                         const struct axd_phy_reset_context *context);
/* At most one typed user I/O callback per step. D11 barriers use D11 region;
 * all delays use NO_REGION; wrapper events preserve their distinct resources.
 * Partial child contexts/callouts propagate after EVERY child step. Completed
 * flags describe whole child completion, not physical readiness. No invented
 * unwind, restoration, retry or rollback. Callback may cancel, not reenter or
 * mutate jobs. Error wins over cancellation; successful effects survive.
 */
enum axd_reset_phase axd_phy_reset_step(struct axd_phy_reset *job,
                                        axd_phy_reset_io_fn io, void *context);
enum axd_reset_phase axd_phy_reset_cancel(struct axd_phy_reset *job);
#endif
