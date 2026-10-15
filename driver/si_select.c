/* SPDX-License-Identifier: MIT */
#include "si_select.h"

enum stage {
    SEARCH, DISPATCH, RANGE, CORE, STORE_CORE, PRIMARY, SECONDARY,
    STORE_WRAPPER, PCI_CORE, PCI_WRAPPER, FINAL_MAP, FINAL_INDEX
};
static void fail(struct axd_si_select *j, enum axd_si_error error)
{
    j->phase = AXD_SI_FAILED;
    j->error = error;
}
static void done(struct axd_si_select *j, enum axd_si_result result)
{
    j->phase = AXD_SI_TRACE_COMPLETE;
    j->result = result;
}
static void dependency(struct axd_si_select *j, enum axd_si_dependency dep)
{
    j->phase = AXD_SI_DEPENDENCY;
    j->dependency = dep;
}
static int catalog(struct axd_si_select *j, uint32_t index)
{
    if (j->context.catalog == NULL) {
        dependency(j, AXD_SI_CATALOG);
        return 0;
    }
    if ((size_t)index >= j->context.catalog_length) {
        fail(j, AXD_SI_BOUNDS);
        return 0;
    }
    return 1;
}
static int perform(struct axd_si_select *j, axd_si_io_fn io, void *context, uint32_t *value)
{
    if (io == NULL) { fail(j, AXD_SI_BAD_ARGUMENT); return 0; }
    j->attempted_ops++;
    if (io(context, &j->operation, value) != 0) {
        fail(j, AXD_SI_IO_ERROR);
        return 0;
    }
    j->completed_ops++;
    return 1;
}
static int map(struct axd_si_select *j, enum axd_si_resource resource,
                uint32_t physical, uint32_t size, uint32_t *cache,
                axd_si_io_fn io, void *context)
{
    const struct axd_si_operation op = {AXD_SI_MAP, resource, physical, size, 0, 0};
    uint32_t value = 0;
    j->operation = op;
    if (!perform(j, io, context, &value)) return 0;
    *cache = value;
    if (value == 0) {
        j->zero_map_results++;
        j->zero_map_mask |= 1u << ((unsigned)resource - 1u);
    }
    return 1;
}
static int pci(struct axd_si_select *j, enum axd_si_resource resource,
                uint32_t offset, uint32_t physical, axd_si_io_fn io, void *context)
{
    const struct axd_si_operation op = {AXD_SI_PCI_WRITE32, resource, offset, 4, physical, 0};
    uint32_t ignored = 0;
    if (!j->context.pci_config_owned) {
        dependency(j, AXD_SI_PCI_OWNER);
        return 0;
    }
    j->operation = op;
    return perform(j, io, context, &ignored);
}
int axd_si_select_begin(struct axd_si_select *j, enum axd_si_command command,
                        uint32_t requested, uint32_t unit,
                        const struct axd_si_context *context,
                        const struct axd_si_state *state)
{
    const struct axd_si_select empty = {0};
    struct axd_si_context context_copy;
    struct axd_si_state state_copy;
    if (j == NULL) return -1;
    if (context != NULL) { context_copy = *context; context = &context_copy; }
    if (state != NULL) { state_copy = *state; state = &state_copy; }
    *j = empty;
    fail(j, AXD_SI_BAD_ARGUMENT);
    if (context == NULL || state == NULL || command < AXD_SI_BY_ID || command > AXD_SI_BY_INDEX ||
        context->pci_config_owned > 1) return -1;
    j->context = *context;
    j->state = *state;
    if (context->core_count > AXD_SI_MAX_CORES) { fail(j, AXD_SI_BOUNDS); return -1; }
    j->command = command;
    j->requested = requested;
    j->unit = unit;
    j->selected_index = requested;
    j->stage = command == AXD_SI_BY_ID ? SEARCH : DISPATCH;
    j->phase = AXD_SI_RUNNING;
    j->error = AXD_SI_NO_ERROR;
    return 0;
}
enum axd_si_phase axd_si_select_step(struct axd_si_select *j, axd_si_io_fn io, void *context)
{
    struct axd_si_cache *cache;
    if (j == NULL) return AXD_SI_INVALID;
    if (j->phase != AXD_SI_RUNNING) return j->phase;
    switch (j->stage) {
    case SEARCH:
        if (j->search_index == j->context.core_count) { done(j, AXD_SI_NO_MATCH); break; }
        if (!catalog(j, j->search_index)) break;
        if (j->context.catalog[j->search_index].core_id == j->requested && j->matches++ == j->unit) {
            j->selected_index = j->search_index;
            j->selected_index_valid = 1;
            j->stage = DISPATCH;
        } else j->search_index++;
        break;
    case DISPATCH:
        if (j->context.interconnect == 0) dependency(j, AXD_SI_SB_SELECTOR);
        else if (j->context.interconnect == 1 || j->context.interconnect == 3 || j->context.interconnect == 4)
            j->stage = RANGE;
        else done(j, AXD_SI_INTERCONNECT_SKIPPED);
        break;
    case RANGE:
        if (j->selected_index >= j->context.core_count) { done(j, AXD_SI_OUT_OF_RANGE); break; }
        if (!catalog(j, j->selected_index)) break;
        j->selected_index_valid = 1;
        j->selected_core = j->context.catalog[j->selected_index];
        j->stage = j->context.bus == 0 ? CORE : (j->context.bus == 1 ? PCI_CORE : FINAL_MAP);
        break;
    case CORE:
        cache = &j->state.cache[j->selected_index];
        if (cache->core == 0 && j->selected_core.core_physical != 0 &&
            !map(j, AXD_SI_CORE, j->selected_core.core_physical,
                 j->selected_core.core_id == 0x50bu ? 0xf000u : 0x1000u, &cache->core, io, context)) break;
        j->stage = STORE_CORE;
        break;
    case STORE_CORE:
        j->state.current_map = j->state.cache[j->selected_index].core;
        j->current_map_stores++;
        j->stage = PRIMARY;
        break;
    case PRIMARY:
        cache = &j->state.cache[j->selected_index];
        if (cache->primary == 0 && j->selected_core.primary_physical != 0 &&
            !map(j, AXD_SI_PRIMARY_WRAPPER, j->selected_core.primary_physical,
                 0x1000, &cache->primary, io, context)) break;
        j->stage = SECONDARY;
        break;
    case SECONDARY:
        cache = &j->state.cache[j->selected_index];
        if (cache->secondary == 0 && j->selected_core.secondary_physical != 0 &&
            !map(j, AXD_SI_SECONDARY_WRAPPER, j->selected_core.secondary_physical,
                 0x1000, &cache->secondary, io, context)) break;
        j->stage = STORE_WRAPPER;
        break;
    case STORE_WRAPPER:
        j->state.primary_wrapper = j->state.cache[j->selected_index].primary;
        j->wrapper_stores++;
        j->stage = FINAL_MAP;
        break;
    case PCI_CORE:
        if (pci(j, AXD_SI_CORE, 0x80, j->selected_core.core_physical, io, context)) j->stage = PCI_WRAPPER;
        break;
    case PCI_WRAPPER:
        if (pci(j, AXD_SI_PRIMARY_WRAPPER, j->context.bus_core_id == 0x83cu ? 0x70u : 0xacu,
                 j->selected_core.primary_physical, io, context)) j->stage = FINAL_MAP;
        break;
    case FINAL_MAP:
        if (j->context.bus == 0) j->state.current_map = j->state.cache[j->selected_index].core;
        else if (j->context.bus != 1) j->state.current_map = 0;
        j->current_map_stores++;
        j->stage = FINAL_INDEX;
        break;
    case FINAL_INDEX:
        j->state.current_index = j->selected_index;
        j->index_stores++;
        j->return_value = j->state.current_map;
        done(j, AXD_SI_SELECTED);
        break;
    default: fail(j, AXD_SI_BAD_ARGUMENT); break;
    }
    return j->phase;
}
enum axd_si_phase axd_si_select_cancel(struct axd_si_select *j)
{
    if (j == NULL) return AXD_SI_INVALID;
    if (j->phase == AXD_SI_RUNNING) j->phase = AXD_SI_CANCELLED;
    return j->phase;
}
