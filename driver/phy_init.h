/* SPDX-License-Identifier: MIT */
#ifndef AXD_PHY_INIT_H
#define AXD_PHY_INIT_H
#include "phy_table_write.h"
#include "si_pmu_gpio.h"

struct axd_phy_init_span { const uint8_t *data; size_t length; uint8_t known; };
struct axd_phy_init_context {
    struct axd_phy_table_context hardware;
    struct axd_si_pmu_gpio_context control;
    struct axd_wake_u32 otp_word;
    struct axd_phy_init_span tables[3];
    uint32_t chip_id;
    uint8_t control_state_known;
};
enum axd_phy_init_phase {
    AXD_PI_INVALID = 0, AXD_PI_RUNNING, AXD_PI_TRACE_COMPLETE,
    AXD_PI_DEPENDENCY, AXD_PI_FAILED, AXD_PI_CANCELLED
};
enum axd_phy_init_error {
    AXD_PI_NO_ERROR = 0, AXD_PI_BAD_ARGUMENT, AXD_PI_UNSUPPORTED,
    AXD_PI_BINDING, AXD_PI_PHY_ERROR, AXD_PI_TABLE_ERROR,
    AXD_PI_CONTROL_ERROR, AXD_PI_IO_ERROR
};
enum axd_phy_init_field { AXD_PI_NO_FIELD = 0, AXD_PI_OTP, AXD_PI_TABLE0, AXD_PI_TABLE1, AXD_PI_TABLE2, AXD_PI_SI_STATE };
enum axd_phy_init_path { AXD_PI_D11 = 1, AXD_PI_CONTROL };
struct axd_phy_init_operation {
    enum axd_phy_init_path path;
    struct axd_d11_operation d11;
    struct axd_si_pmu_gpio_operation control;
};
struct axd_phy_init_reply {
    uint32_t value;
    struct axd_si_pmu_gpio_reply control;
};
typedef int (*axd_phy_init_io_fn)(void *,const struct axd_phy_init_operation *,struct axd_phy_init_reply *);
struct axd_phy_init {
    enum axd_phy_init_phase phase;
    enum axd_phy_init_error error;
    enum axd_phy_init_field last_field;
    struct axd_phy_init_context context;
    struct axd_phy_register phy_job;
    struct axd_phy_table_write table_job;
    struct axd_si_pmu_gpio control_job;
    uint32_t voltage_value;
    uint16_t phy0_sample, guard_sample;
    unsigned phy0_valid, guard_valid, tables_completed;
    unsigned stage,after_child,metadata_accesses,pending_callouts;
    uint64_t attempted_ops,completed_ops;
};
/* Exact registered table-init body for chip63178/core130.0/1/AC PHY51. Real
 * PHY primitives, packed/table writer and SI PMU/GPIO helpers are composed.
 * OTP is caller-known actual word10; table-context5bit field is derived from
 * bits8..12, never guessed. tables[] are ordered immutable private spans of
 * at least32/256/4 bytes; owner validates provenance with phy_init_inventory.
 * They remain outside all jobs. Context/SI catalogs/metadata are coherent and
 * immutable except copied child software state; actual resource/OS ownership
 * remains caller-owned. This is not the full PHY scheduler/radio/calibration.
 */
int axd_phy_init_begin(struct axd_phy_init *,const struct axd_phy_init_context *);
/* Begin snapshots own aliases before clearing. One callback max per step;
 * error wins cancel and successful cancelling child effects are retained.
 * All child partial contexts propagate. Reached unknown input is dependency.
 * Terminal states are silent: no invented restore, retry, cleanup or readiness.
 */
enum axd_phy_init_phase axd_phy_init_step(struct axd_phy_init *,axd_phy_init_io_fn,void *);
enum axd_phy_init_phase axd_phy_init_cancel(struct axd_phy_init *);
#endif
