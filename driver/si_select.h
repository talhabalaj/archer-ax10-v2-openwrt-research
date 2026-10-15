/* SPDX-License-Identifier: MIT */
#ifndef AXD_SI_SELECT_H
#define AXD_SI_SELECT_H
#include <stddef.h>
#include <stdint.h>

#define AXD_SI_MAX_CORES 32u
struct axd_si_core {
    uint32_t core_id, core_physical, primary_physical, secondary_physical;
};
struct axd_si_cache { uint32_t core, primary, secondary; };
struct axd_si_state {
    uint32_t current_map, current_index, primary_wrapper;
    struct axd_si_cache cache[AXD_SI_MAX_CORES];
};
struct axd_si_context {
    uint32_t interconnect, bus, bus_core_id, core_count;
    const struct axd_si_core *catalog;
    size_t catalog_length;
    uint8_t pci_config_owned;
};
enum axd_si_command { AXD_SI_BY_ID = 1, AXD_SI_BY_INDEX };
enum axd_si_phase {
    AXD_SI_INVALID = 0, AXD_SI_RUNNING, AXD_SI_TRACE_COMPLETE,
    AXD_SI_DEPENDENCY, AXD_SI_FAILED, AXD_SI_CANCELLED
};
enum axd_si_error { AXD_SI_NO_ERROR = 0, AXD_SI_BAD_ARGUMENT, AXD_SI_BOUNDS, AXD_SI_IO_ERROR };
enum axd_si_dependency { AXD_SI_NO_DEPENDENCY = 0, AXD_SI_SB_SELECTOR, AXD_SI_CATALOG, AXD_SI_PCI_OWNER };
enum axd_si_result {
    AXD_SI_NO_RESULT = 0, AXD_SI_SELECTED, AXD_SI_NO_MATCH,
    AXD_SI_OUT_OF_RANGE, AXD_SI_INTERCONNECT_SKIPPED
};
enum axd_si_kind { AXD_SI_MAP = 1, AXD_SI_PCI_WRITE32 };
enum axd_si_resource { AXD_SI_CORE = 1, AXD_SI_PRIMARY_WRAPPER, AXD_SI_SECONDARY_WRAPPER };
struct axd_si_operation {
    enum axd_si_kind kind;
    enum axd_si_resource resource;
    uint32_t address, size, value, map_type;
};
typedef int (*axd_si_io_fn)(void *context, const struct axd_si_operation *operation,
                           uint32_t *mapping_result);
struct axd_si_select {
    enum axd_si_phase phase;
    enum axd_si_error error;
    enum axd_si_dependency dependency;
    enum axd_si_result result;
    enum axd_si_command command;
    struct axd_si_context context;
    struct axd_si_state state;
    struct axd_si_core selected_core;
    uint32_t requested, unit, search_index, matches, selected_index, return_value;
    unsigned selected_index_valid, stage, zero_map_mask, zero_map_results;
    unsigned current_map_stores, wrapper_stores, index_stores;
    struct axd_si_operation operation;
    uint64_t attempted_ops, completed_ops;
};
/* si_setcore(id,unit) or si_setcoreidx(index), primary-wrapper selector0 only.
 * Runtime count is explicitly bounded at32: larger counts fail before I/O
 * rather than reproducing an unbounded ID search (AI itself clamps at32).
 * Borrowed catalog is immutable complete physical metadata, outside every job;
 * state owns copied mutable caches. Context AND state can alias job snapshots.
 * Callers supply coherent current SI state, initialized cache ownership,
 * serialized mapping/window lifetime, and an actual OS adapter. No board
 * addresses, pointer acquisition, kernel map or interrupt state is invented.
 */
int axd_si_select_begin(struct axd_si_select *job, enum axd_si_command command,
                        uint32_t id_or_index, uint32_t unit,
                        const struct axd_si_context *context,
                        const struct axd_si_state *state);
/* At most one logical MAP/PCI callback or bounded internal transition per step.
 * MAP: address=physical,size=0x1000/0xf000,map_type=0,value=0; successful callback
 * returns an ARM32 mapping, committed to its cache even when cancelling. Zero
 * is USED as stock uses it; zero_map diagnostics do not reject or mean ready.
 * The later SI current-map/wrapper/index stores are separate transitions and
 * never run after cancellation. Bus1 PCI requests are size4, no ARM barriers.
 * SB0 is an explicit unresolved dependency; AI1/3/4 run the actual body. Other
 * interconnects and no-match/range rejection return0 without context changes.
 * Callback error wins over cancellation; no failed callback cache commit.
 * Terminals silent. Callback may cancel, never reenter or mutate jobs. No
 * automatic restoration, unmap, rollback, retry or readiness/ownership result.
 */
enum axd_si_phase axd_si_select_step(struct axd_si_select *job, axd_si_io_fn io, void *context);
enum axd_si_phase axd_si_select_cancel(struct axd_si_select *job);
#endif
