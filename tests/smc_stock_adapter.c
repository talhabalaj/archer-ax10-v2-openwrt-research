/* SPDX-License-Identifier: MIT */
/* Host-test ABI only: drive the actual SMC job with private bytes and an
 * externally supplied strict I/O oracle. No firmware values are embedded. */
#include "../driver/smc.h"
struct axd_smc_stock_result {
    uint32_t phase, error, image_rows, table_rows, data_writes;
    uint32_t download_completed, reset0_completed, reset1_completed;
    uint32_t loaded, ac_downloaded, loaded_committed;
    uint64_t attempted_ops, completed_ops;
};
int axd_smc_stock_run(const uint8_t *data,size_t length,uint32_t source_bytes,
                       axd_d11_io_fn io,void *opaque,struct axd_smc_stock_result *result)
{
    struct axd_smc job;
    struct axd_smc_context c={0};
    struct axd_smc_image image={data,length,source_bytes};
    struct axd_wake_u32 binding={.value=0x1000,.known=1};
    unsigned steps=0;
    if(result==NULL)return -1;
    c.hardware.phy.core=130;c.hardware.phy.minor=1;
    c.hardware.phy.phy_type=0xb;c.hardware.phy.phy_revision=51;
    c.hardware.phy.phy_core_count=2;
    c.hardware.mac.core=130;c.hardware.mac.minor=1;
    c.hardware.mac_context_valid=1;
    c.band=binding;binding.value+=0x100;c.phy_pointer=binding;
    binding.value+=0x100;c.smc_info=binding;binding.value+=0x100;c.dispatch=binding;
    binding.value+=0x100;c.callback_context=binding;c.callback_pi=c.phy_pointer;
    binding.value+=0x100;c.reset_callback=binding;binding.value+=0x100;c.download_callback=binding;
    if(axd_smc_begin(&job,AXD_SMC_LOAD,&image,&c))return -1;
    while(job.phase==AXD_SMC_RUNNING && ++steps<1000000u)
        axd_smc_step(&job,io,opaque);
    result->phase=(uint32_t)job.phase;result->error=(uint32_t)job.error;
    result->image_rows=job.image_rows;result->table_rows=job.table_job.index;
    result->data_writes=(uint32_t)job.table_job.data_writes;
    result->download_completed=job.download_completed;
    result->reset0_completed=job.reset0_completed;result->reset1_completed=job.reset1_completed;
    result->loaded=job.context.loaded;result->ac_downloaded=job.context.ac_downloaded;
    result->loaded_committed=job.loaded_committed;
    result->attempted_ops=job.attempted_ops;result->completed_ops=job.completed_ops;
    return job.phase==AXD_SMC_TRACE_COMPLETE?0:-1;
}
