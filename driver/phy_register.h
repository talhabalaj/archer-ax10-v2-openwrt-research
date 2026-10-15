/* SPDX-License-Identifier: MIT */
#ifndef AXD_PHY_REGISTER_H
#define AXD_PHY_REGISTER_H
#include "d11_io.h"
#include <stddef.h>

enum axd_phy_command {
    AXD_PHY_READ = 1, AXD_PHY_MODIFY, AXD_PHY_ANALOG_RESET,
    AXD_PHY_RESET_PULSE, AXD_PHY_TSSI_SNAPSHOT, AXD_PHY_TSSI_RESTORE
};
struct axd_phy_context {
    uint16_t core, minor, phy_type, phy_revision;
    uint16_t bookkeeping, saved_tssi[2];
    uint8_t phy_core_count, snapshot_valid;
};
enum axd_phy_phase {
    AXD_PHY_INVALID = 0, AXD_PHY_RUNNING, AXD_PHY_TRACE_COMPLETE,
    AXD_PHY_DEPENDENCY, AXD_PHY_FAILED, AXD_PHY_CANCELLED
};
enum axd_phy_error {
    AXD_PHY_NO_ERROR = 0, AXD_PHY_BAD_ARGUMENT, AXD_PHY_UNSUPPORTED,
    AXD_PHY_MISSING_SNAPSHOT, AXD_PHY_IO_ERROR
};
enum axd_phy_dependency { AXD_PHY_NO_DEPENDENCY = 0, AXD_PHY_TABLE_READ };
struct axd_phy_register {
    enum axd_phy_phase phase;
    enum axd_phy_error error;
    enum axd_phy_command command;
    enum axd_phy_dependency dependency;
    struct axd_phy_context context;
    uint16_t selector, mask, value, last_read, result, pulse_original;
    unsigned stage, after_read, queued, cursor, tssi_slot, result_valid;
    struct axd_d11_operation operations[4];
    uint64_t attempted_ops, completed_ops;
};
/* All operations require caller-owned powered/clocks-enabled D11/PHY access.
 * This low-level layer does not suspend/enable MAC, enter/exit PHY nesting,
 * acquire mappings, or execute outer reset/PHY-clock/beamforming callouts.
 * READ/MODIFY use selector/mask/value; other commands require these all zero.
 * READ requires mask/value zero. Context identity is core130 minor0/1, type0xb,
 * PHY51; core count1..4 is explicit. TSSI1/2 slots are per PHY core, not access
 * order: 2-core visits slot1(selector845) then slot0(selector645).
 * READ>=c000 and TSSI3/4 end DEPENDENCY_TABLE_READ before I/O. MODIFYffff is
 * the actual no-op; other high selectors still use direct PHY ports.
 * RESTORE requires valid saved active slots before I/O; this is stricter than
 * stock's raw caller buffer. SNAPSHOT invalidates active slots at entry, then
 * commits a slot after its read+full barrier. The caller must keep partial
 * context on failure/cancellation distinct from a completed snapshot.
 * RESET_PULSE includes the core130 following100us delay, then completes; it
 * excludes outer core-flags/PHY-clock, analog dispatch and TSSI operations.
 * result/result_valid commit the last full16 READ/SNAPSHOT reply after SY;
 * saved_tssi stores its low10. Other commands leave result zero.
 */
int axd_phy_register_begin(struct axd_phy_register *job,
                           enum axd_phy_command command, uint16_t selector,
                           uint16_t mask, uint16_t value,
                           const struct axd_phy_context *context);
/* At most one synchronous callback/step. Read replies are narrowed to16bits.
 * READ clears bookkeeping immediately after successful address write; MODIFY
 * after successful data write. No software shadow update occurs on callback
 * error; successful callback cancellation keeps its observed effects. Callback
 * error wins over cancellation. No retries/rollback or ready predicate. Single
 * threaded: callback may cancel, never reenter begin/step or mutate job fields.
 */
enum axd_phy_phase axd_phy_register_step(struct axd_phy_register *job,
                                         axd_d11_io_fn io, void *context);
enum axd_phy_phase axd_phy_register_cancel(struct axd_phy_register *job);
#endif
