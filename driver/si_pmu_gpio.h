/* SPDX-License-Identifier: MIT */
#ifndef AXD_SI_PMU_GPIO_H
#define AXD_SI_PMU_GPIO_H
#include "si_select.h"
struct axd_si_pmu_gpio_context {
    struct axd_si_context si;
    struct axd_si_state state;
    int32_t chipcommon_revision;
    uint32_t capabilities_ext, bus_core_revision, bus_core_index;
    uint32_t irq_core_id, disable_callback, restore_callback, irq_context;
    uint32_t gpio_reservation;
    uint8_t gpio_reservation_known;
};
enum axd_si_pmu_gpio_command { AXD_SPG_VREG=1, AXD_SPG_GPIO };
enum axd_si_pmu_gpio_kind { AXD_SPG_SELECT=1, AXD_SPG_READ32, AXD_SPG_FULL_BARRIER, AXD_SPG_STORE_BARRIER, AXD_SPG_WRITE32, AXD_SPG_IRQ_DISABLE, AXD_SPG_IRQ_RESTORE };
struct axd_si_pmu_gpio_operation {
    enum axd_si_pmu_gpio_kind kind;
    struct axd_si_operation select;
    uint32_t mapping, offset, value, callback, callback_context;
    const struct axd_si_state *state;
};
struct axd_si_pmu_gpio_reply {
    uint32_t value;
    uint8_t irq_state_known;
    struct axd_si_state irq_state;
};
typedef int (*axd_si_pmu_gpio_io_fn)(void *,const struct axd_si_pmu_gpio_operation *,struct axd_si_pmu_gpio_reply *);
enum axd_si_pmu_gpio_phase { AXD_SPG_INVALID=0, AXD_SPG_RUNNING, AXD_SPG_TRACE_COMPLETE, AXD_SPG_DEPENDENCY, AXD_SPG_FAILED, AXD_SPG_CANCELLED };
enum axd_si_pmu_gpio_error { AXD_SPG_NO_ERROR=0, AXD_SPG_BAD_ARGUMENT, AXD_SPG_BINDING, AXD_SPG_BOUNDS, AXD_SPG_CHILD_ERROR, AXD_SPG_IO_ERROR };
enum axd_si_pmu_gpio_dependency { AXD_SPG_NO_DEPENDENCY=0, AXD_SPG_CATALOG, AXD_SPG_GPIO_RESERVATION, AXD_SPG_IRQ_STATE, AXD_SPG_SB };
struct axd_si_pmu_gpio {
    enum axd_si_pmu_gpio_phase phase; enum axd_si_pmu_gpio_error error;
    enum axd_si_pmu_gpio_dependency dependency; enum axd_si_pmu_gpio_command command;
    struct axd_si_pmu_gpio_context context; struct axd_si_select selector;
    uint32_t register_index, mask, value, effective_mask, effective_value;
    uint32_t requested_index, offset, mapping, last_read, pending_value, result, saved_index, irq_token;
    unsigned stage, register_stage, after_register, after_select, vreg_part, search_index;
    unsigned direct_mapping, state_valid, disable_called, restore_called, register_completed;
    unsigned metadata_accesses;
    uint64_t attempted_ops, completed_ops;
};
/* Actual PMU-vreg and GPIO control, using current SI resource/callback facts.
 * VREG register_index is the vreg address. GPIO register_index is actual priority
 * (target caller0), with global reservation input required only at its real guard.
 * VREG selects PMU index827 only with signed CC revision>34 and extcap40;
 * otherwise index0. Address658 and data65c each execute real AI corereg paths.
 * Bus0 corereg maps/cache-accesses the requested core WITHOUT changing SI index;
 * bus1 fast windows and ordinary selector/IRQ paths retain actual ordering.
 * PMU wait-loop filtering does not apply to658/65c; no synthetic poll is added.
 * Catalog/IRQ facts are immutable except explicit post-IRQ state replies. Every
 * successful IRQ callback must return actual state, even when unchanged. Unknown
 * reached metadata is a dependency; null reached mappings fail before MMIO.
 * No physical backend, resource acquisition, rollback/retry or readiness claim.
 */
int axd_si_pmu_gpio_begin(struct axd_si_pmu_gpio *,enum axd_si_pmu_gpio_command,
                           uint32_t register_index,uint32_t mask,uint32_t value,
                           const struct axd_si_pmu_gpio_context *);
enum axd_si_pmu_gpio_phase axd_si_pmu_gpio_step(struct axd_si_pmu_gpio *,axd_si_pmu_gpio_io_fn,void *);
enum axd_si_pmu_gpio_phase axd_si_pmu_gpio_cancel(struct axd_si_pmu_gpio *);
#endif
