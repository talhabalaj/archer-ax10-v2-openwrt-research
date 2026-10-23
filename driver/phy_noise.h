/* SPDX-License-Identifier: MIT */
#ifndef AXD_PHY_NOISE_H
#define AXD_PHY_NOISE_H
#include "phy_channel_init.h"
#include "phy_table_read.h"
#include "phy_guard.h"
#include "radio_control.h"
struct axd_phy_noise_context {
    struct axd_phy_table_context hardware;
    struct axd_phy_channel_context channel;
    struct axd_radio_control_context radio;
    struct axd_wake_u32 phy_pointer,ac_pointer,common_pointer,data_pointer;
    struct axd_wake_u32 chanspec,current_channel,shared_mode,postfilter_shm;
    struct axd_wake_u32 hwaci_2g_pointer,hwaci_5g_pointer;
    struct axd_wake_u32 hwaci_shm_offset;
    struct axd_wake_u8 hwaci_2g_flag,hwaci_5g_flag,hwaci_adc_flag,phy_depth;
    uint8_t hwaci_2g[3],hwaci_5g[3],preempt_byte10;
    uint16_t preempt_halfe;
};
enum axd_phy_noise_command {AXD_NOISE_INIT_MODE=1,AXD_NOISE_HWACI_OFF,
    AXD_NOISE_PREEMPT,AXD_NOISE_POSTFILTER,AXD_NOISE_ELNABYPASS,AXD_NOISE_MCLIP,AXD_NOISE_HWACI_ON};
enum axd_phy_noise_phase {AXD_NOISE_INVALID=0,AXD_NOISE_RUNNING,
    AXD_NOISE_TRACE_COMPLETE,AXD_NOISE_DEPENDENCY,AXD_NOISE_FAILED,AXD_NOISE_CANCELLED};
enum axd_phy_noise_error {AXD_NOISE_NO_ERROR=0,AXD_NOISE_BAD_ARGUMENT,
    AXD_NOISE_BINDING,AXD_NOISE_BOUNDS,AXD_NOISE_UNSUPPORTED,AXD_NOISE_CHILD_ERROR,AXD_NOISE_IO_ERROR};
enum axd_phy_noise_dependency {AXD_NOISE_NO_DEPENDENCY=0,AXD_NOISE_METADATA,
    AXD_NOISE_DESENSE_RESET_APPLY,AXD_NOISE_HWACI28NM,AXD_NOISE_CHANNEL_GETSET,
    AXD_NOISE_MAC_CHILD,AXD_NOISE_RESOURCE_CHILD};
enum axd_phy_noise_field {AXD_NOISE_NO_FIELD=0,AXD_NOISE_PHY,AXD_NOISE_AC,
    AXD_NOISE_COMMON,AXD_NOISE_DATA,AXD_NOISE_CHANSPEC,AXD_NOISE_CHANNEL,
    AXD_NOISE_SHARED_MODE,AXD_NOISE_POSTFILTER_SHM,AXD_NOISE_HWACI_2G,
    AXD_NOISE_HWACI_5G,AXD_NOISE_HWACI_2G_FLAG,AXD_NOISE_HWACI_5G_FLAG,
    AXD_NOISE_DEPTH,AXD_NOISE_RADIO_ID,AXD_NOISE_HWACI_ADC_FLAG,AXD_NOISE_HWACI_SHM};
struct axd_phy_noise {
    enum axd_phy_noise_phase phase;enum axd_phy_noise_error error;
    enum axd_phy_noise_dependency dependency;enum axd_phy_noise_field last_field;
    enum axd_phy_noise_command command;
    struct axd_phy_noise_context context;
    struct axd_phy_register phy_job;
    struct axd_phy_table_write packed_job,writer_job;
    struct axd_phy_table_read reader_job;
    struct axd_phy_channel_init channel_job;
    struct axd_object_memory object_job;
    struct axd_radio_control radio_job;
    struct axd_psm_control mac_job;
    struct axd_phy_guard guard_job;
    uint32_t mode,main_control;
    uint16_t guard_sample,preempt_guard,postfilter_value,hwaci_shm_sample;
    uint8_t actual[6],desired[6];
    unsigned enabled,stage,after_child,after_helper,after_filter,index,core_index;
    unsigned hwaci_completed,preempt_completed,hwobss_completed,elnabypass_completed,mclip_completed;
    unsigned table_reads,table_writes,software_stores,metadata_accesses,pending_callouts,shm_sample_valid;
    uint64_t attempted_ops,completed_ops;
};
/* Actual target PHY51 initialization mode entry (reset argument0), and selected
 * real descendants. Mode is the actual u32 supplied by the common callback;
 * other commands accept only the actual initialization caller's boolean0/1
 * (HWACI_OFF requires0, HWACI_ON requires1), rejecting broader standalone
 * argument semantics. This
 * does not supply board mode/radio identity, current channel data or a vendor
 * callback result. Reached source gaps remain explicit dependencies. Canonical
 * hardware is shared with channel/radio/PHY/PSM children; all object resources
 * and metadata are actual caller bindings. Borrowed lifetimes remain external.
 * One synchronous callback maximum per step; callbacks may cancel but must not
 * reenter or mutate job/metadata. Error wins cancellation, successful effects
 * and pre-I/O stores persist; terminals are silent without invented cleanup.
 */
int axd_phy_noise_begin(struct axd_phy_noise *,enum axd_phy_noise_command,uint32_t,const struct axd_phy_noise_context *);
enum axd_phy_noise_phase axd_phy_noise_step(struct axd_phy_noise *,axd_d11_io_fn,void *);
enum axd_phy_noise_phase axd_phy_noise_cancel(struct axd_phy_noise *);
#endif
