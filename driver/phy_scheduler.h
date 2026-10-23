/* SPDX-License-Identifier: MIT */
#ifndef AXD_PHY_SCHEDULER_H
#define AXD_PHY_SCHEDULER_H
#include "phy_init.h"
#include "radio_power.h"
#include "phy_channel_init.h"
#include "phy_noise.h"
#include "si_core.h"

#define AXD_PHY_REGISTRY_CAPACITY 23u
#define AXD_PHY_TARGET_INIT_COUNT 16u
/* Names/values identify recovered functions, not arbitrary host callbacks. */
enum axd_phy_init_callback {
    AXD_INIT_UNKNOWN=0, AXD_INIT_CACHE=1, AXD_INIT_ANALOG=3,
    AXD_INIT_BANDWIDTH=4, AXD_INIT_TYPE=5, AXD_INIT_NOISE_RESET=6,
    AXD_INIT_RADIO=7, AXD_INIT_TABLE=8, AXD_INIT_TPC=9,
    AXD_INIT_RADAR=10, AXD_INIT_ANTDIV=11, AXD_INIT_NOISE=12,
    AXD_INIT_CHANSPEC=13, AXD_INIT_PAPD=16, AXD_INIT_RXGCRS=19,
    AXD_INIT_BTCX=20, AXD_INIT_HIRSSI=21
};
struct axd_phy_registry_entry {
    enum axd_phy_init_callback callback;
    struct axd_wake_u32 context;
    uint8_t priority;
};
struct axd_phy_registry {
    struct axd_phy_registry_entry entries[AXD_PHY_REGISTRY_CAPACITY];
    uint16_t init_count, total_count;
};
/* Empty registry has actual capacity23. Add inserts before equal priority and
 * shifts the entire occupied suffix, including supplied down entries. Full is
 * -22; invalid bounded host metadata is -1, with no mutation. Priority>255 is
 * rejected rather than reproducing stock's compare-u32/store-u8 discrepancy.
 * The entry is copied before shifting, including aliases into the registry. */
void axd_phy_registry_clear(struct axd_phy_registry *);
int axd_phy_registry_add(struct axd_phy_registry *,enum axd_phy_init_callback,
                           const struct axd_wake_u32 *,uint32_t priority);
/* Construct the complete16 init vector in actual common-module attach order.
 * contexts[] are indexed by the sorted priorities returned in entries[]. This
 * reconstructs the init half; allocation/down registry execution is separate.
 * Unknown context labels remain unknown until a callback actually needs them.
 */
int axd_phy_registry_target51(struct axd_phy_registry *,const struct axd_wake_u32 contexts[AXD_PHY_TARGET_INIT_COUNT]);

struct axd_phy_scheduler_cache {
    struct axd_wake_u32 reuse_state, reuse_bytes, reuse_buffer;
    struct axd_wake_u32 entries, table_pointer;
    uint32_t table_bytes;
    uint16_t table_used;
};
struct axd_phy_scheduler_context {
    /* table.hardware and table.control are canonical. radio.hardware mirrors
     * table.hardware on dispatch and after every radio child transition. */
    struct axd_phy_init_context table;
    struct axd_radio_control_context radio;
    /* radio is the compatible canonical old control snapshot; the new private
     * spans/calibration fields live here, with .radio synchronized on dispatch. */
    struct axd_radio_power_context radio_power;
    struct axd_phy_channel_context channel;
    struct axd_phy_noise_context noise;
    struct axd_phy_scheduler_cache cache;
    struct axd_wake_u32 allocator, flags3b0, chanspec, bandwidth_state;
    struct axd_wake_u32 hardware_chanspec, interference2g, interference5g;
    struct axd_wake_u32 interference_override2g, interference_override5g;
    struct axd_wake_u32 interference_current, channel_state_pointer;
    struct axd_wake_u32 slice_unit, slice_up;
    struct axd_wake_u8 forced_interference, clocks, rxgcrs_enabled;
    struct axd_wake_u8 radar_enabled;
    uint8_t phy_core_count_known;
    uint8_t tpc_min[4], tpc_max[4], btcx_byte18, rxgcrs_bytea8;
    uint8_t hirssi_byte16, hirssi_byte17;
    uint16_t hirssi_half18, hirssi_half1a, channel_state_chanspec;
    uint32_t noise_mode378;
};
enum axd_phy_scheduler_phase { AXD_PS_INVALID=0, AXD_PS_RUNNING,
    AXD_PS_TRACE_COMPLETE, AXD_PS_DEPENDENCY, AXD_PS_FAILED, AXD_PS_CANCELLED };
enum axd_phy_scheduler_error { AXD_PS_NO_ERROR=0, AXD_PS_BAD_ARGUMENT,
    AXD_PS_UNSUPPORTED, AXD_PS_BINDING, AXD_PS_BOUNDS, AXD_PS_IO_ERROR,
    AXD_PS_CHILD_ERROR };
enum axd_phy_scheduler_dependency { AXD_PS_NO_DEPENDENCY=0,
    AXD_PS_METADATA, AXD_PS_UNKNOWN_CALLBACK, AXD_PS_ALLOCATOR,
    AXD_PS_FATAL_ALLOCATION, AXD_PS_BANDWIDTH_SET, AXD_PS_RADIO_CHILD,
    AXD_PS_RADAR_CHILD, AXD_PS_NOISE_MODE, AXD_PS_CHANSPEC_CHILD,
    AXD_PS_TABLE_CHILD, AXD_PS_MAC_CHILD, AXD_PS_SI_CHILD };
enum axd_phy_scheduler_path { AXD_PS_D11=1, AXD_PS_CONTROL,
    AXD_PS_SI, AXD_PS_ALLOCATE_ZERO };
struct axd_phy_scheduler_operation {
    enum axd_phy_scheduler_path path;
    struct axd_d11_operation d11;
    struct axd_si_pmu_gpio_operation control;
    struct axd_si_core_operation si;
    uint32_t allocator, bytes;
};
struct axd_phy_scheduler_reply {
    uint32_t value;
    struct axd_si_pmu_gpio_reply control;
};
typedef int (*axd_phy_scheduler_io_fn)(void *,const struct axd_phy_scheduler_operation *,struct axd_phy_scheduler_reply *);
struct axd_phy_scheduler {
    enum axd_phy_scheduler_phase phase;
    enum axd_phy_scheduler_error error;
    enum axd_phy_scheduler_dependency dependency;
    enum axd_phy_init_callback current;
    struct axd_phy_registry registry;
    struct axd_phy_scheduler_context context;
    struct axd_phy_init table_job;
    struct axd_radio_power radio_job;
    struct axd_phy_channel_init channel_job;
    struct axd_phy_noise noise_job;
    struct axd_phy_register phy_job;
    struct axd_psm_control mac_job;
    struct axd_si_core_job si_job;
    uint32_t allocation_result, bw_flags, mac_sample, selected_mode;
    int32_t callback_result, return_value, bandwidth_check_result, antdiv_result;
    unsigned stage, after_child, index, substage, core_index;
    unsigned callbacks_completed, slice_up_stored, return_valid;
    unsigned allocation_observed, metadata_accesses;
    uint64_t attempted_ops, completed_ops;
};
/* Source-backed ordered core130.0/1 AC PHY51 dispatcher. The copied registry
 * and metadata are immutable during execution, narrowing stock's live count /
 * object reloads. A complete target invocation uses target51(), not a vector
 * which omits required stages. Smaller vectors model explicit scheduler inputs
 * for isolated testing, not a completed target initialization.
 * No arbitrary function callback can stand in for an unresolved PHY child.
 * Actual known work executes; reached missing metadata/children stop explicitly.
 * Allocation is the real zero-allocation OS service, returning a caller-owned
 * ARM32 label. Its lifetime survives cancellation; no invented free is issued.
 * slice-up is a software bit store after all callback returns, not readiness.
 * Borrowed tables/catalogs remain external, immutable and owned by the caller.
 */
int axd_phy_scheduler_begin(struct axd_phy_scheduler *,const struct axd_phy_registry *,const struct axd_phy_scheduler_context *);
/* Begin snapshots own aliases. One user callback max per step; error wins
 * cancellation, successful effects and partial children survive. Each later
 * software store / callback return / slice-up / final return is separate.
 * Caller owns coherent function-context bindings, powered/mapped serialized
 * resources and actual board inputs. Terminals are silent without cleanup. */
enum axd_phy_scheduler_phase axd_phy_scheduler_step(struct axd_phy_scheduler *,axd_phy_scheduler_io_fn,void *);
enum axd_phy_scheduler_phase axd_phy_scheduler_cancel(struct axd_phy_scheduler *);
#endif
