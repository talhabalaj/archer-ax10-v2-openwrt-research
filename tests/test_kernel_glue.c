/* SPDX-License-Identifier: MIT */
/* Host test of the kernel glue (kernel/axd_kio.c, kernel/axd_ring.c) against
 * the recording shim in tests/kernel_shim: the hw_access gate, bounds and
 * alignment, wrapper identity, delay cap, barrier mapping, and ring memory
 * validation/codec ordering. No kernel, device or hardware. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "axd_kio.h"
#include "axd_ring.h"
struct entry {enum axd_shim_kind kind;uintptr_t where;uint32_t value;};
static struct entry events[64];static unsigned count;
int axd_shim_mask_result;uint64_t axd_shim_mask_seen,axd_shim_next_dma;int axd_shim_fail_alloc;
void axd_shim_log(enum axd_shim_kind kind,uintptr_t where,uint32_t value)
{assert(count<64);events[count++]=(struct entry){kind,where,value};}
static int d11(struct axd_kio *k,enum axd_d11_kind kind,uint32_t off,uint32_t val,uint32_t *out)
{struct axd_d11_operation op={kind,off,val};uint32_t v=0xdeadbeef;int r=axd_kio_d11(k,&op,&v);if(out)*out=v;return r;}
static int si(struct axd_kio *k,enum axd_si_core_kind kind,uint32_t wrapper,uint32_t off,uint32_t val,uint32_t *out)
{struct axd_si_core_operation op={kind,wrapper,off,val};uint32_t v=0xdeadbeef;int r=axd_kio_si(k,&op,&v);if(out)*out=v;return r;}
int main(void)
{
    static unsigned char regs[0x1000],wrapper[0x100];
    struct axd_kio k={0};struct device dev={1};struct axd_ring ring;
    struct axd_dma64_descriptor d={0x80000040u,0x1234u,0x10000000u,0},back;uint32_t v;
    k.d11=regs;k.d11_len=sizeof(regs);k.wrapper=wrapper;k.wrapper_len=sizeof(wrapper);k.wrapper_phys=0x18100000u;
    /* Disabled: every kind fails with -EPERM before any primitive runs. */
    count=0;assert(d11(&k,AXD_D11_READ32,0x120,0,&v)==-EPERM && d11(&k,AXD_D11_STORE_BARRIER,0,0,NULL)==-EPERM);
    assert(si(&k,AXD_SICO_READ32,0x18100000u,0x408,0,&v)==-EPERM && count==0 && k.rejected==3 && k.completed==0);
    /* Enabled: exact width/offset accesses and barrier mapping. */
    k.hw_enabled=true;k.rejected=0;regs[0x120]=0x78;regs[0x121]=0x56;regs[0x122]=0x34;regs[0x123]=0x12;
    count=0;assert(!d11(&k,AXD_D11_READ32,0x120,0,&v) && v==0x12345678u);
    assert(events[0].kind==SHIM_R32 && events[0].where==(uintptr_t)(regs+0x120));
    assert(!d11(&k,AXD_D11_READ16,0x122,0,&v) && v==0x1234u);
    assert(!d11(&k,AXD_D11_WRITE16,0x3fc,0xbeef,NULL) && regs[0x3fc]==0xef && regs[0x3fd]==0xbe);
    assert(!d11(&k,AXD_D11_WRITE32,0x160,0x10010,NULL) && events[3].kind==SHIM_W32 && events[3].value==0x10010);
    assert(!d11(&k,AXD_D11_STORE_BARRIER,0,0,NULL) && events[4].kind==SHIM_WMB);
    assert(!d11(&k,AXD_D11_FULL_BARRIER,0,0,NULL) && events[5].kind==SHIM_MB);
    assert(!d11(&k,AXD_D11_DELAY_US,0,20000,NULL) && events[6].kind==SHIM_UDELAY && events[6].value==20000);
    assert(k.completed==7 && count==7);
    /* Refusals touch nothing: delay cap, window end, misalignment, wide u16, unknown kind. */
    count=0;
    assert(d11(&k,AXD_D11_DELAY_US,0,20001,NULL)==-ERANGE);
    assert(d11(&k,AXD_D11_READ32,0xffe,0,&v)==-EFAULT && d11(&k,AXD_D11_READ32,0x1000,0,&v)==-EFAULT);
    assert(d11(&k,AXD_D11_READ32,0x122,0,&v)==-EFAULT && d11(&k,AXD_D11_READ16,0x121,0,&v)==-EFAULT);
    assert(d11(&k,AXD_D11_WRITE16,0x10,0x10000,NULL)==-EFAULT && d11(&k,AXD_D11_WRITE32,0xfffffffcu,1,NULL)==-EFAULT);
    assert(d11(&k,(enum axd_d11_kind)99,0,0,NULL)==-EINVAL && count==0 && k.rejected==8);
    /* SI: only the owned wrapper identity and window are accessible. */
    wrapper[0x40]=0x31;count=0;
    assert(!si(&k,AXD_SICO_READ32,0x18100000u,0x40,0,&v) && v==0x31);
    assert(si(&k,AXD_SICO_READ32,0x18200000u,0x40,0,&v)==-EFAULT && si(&k,AXD_SICO_WRITE32,0x18100000u,0x100,1,NULL)==-EFAULT);
    assert(!si(&k,AXD_SICO_STORE_BARRIER,0,0,0,NULL) && !si(&k,AXD_SICO_FULL_BARRIER,0,0,0,NULL));
    assert(!si(&k,AXD_SICO_WRITE32,0x18100000u,0x40,0xabcd,NULL) && wrapper[0x40]==0xcd && count==4);
    k.wrapper=NULL;assert(si(&k,AXD_SICO_READ32,0x18100000u,0x40,0,&v)==-EFAULT);
    /* Ring: mask, count validation, coherent memory, codec ordering, release. */
    axd_shim_mask_result=0;assert(!axd_ring_dma_mask(&dev) && axd_shim_mask_seen==0xffffffffu);
    assert(axd_ring_alloc(&ring,&dev,3)==-EINVAL && axd_ring_alloc(&ring,&dev,1)==-EINVAL && axd_ring_alloc(&ring,&dev,65536)==-EINVAL);
    axd_shim_fail_alloc=1;assert(axd_ring_alloc(&ring,&dev,64)==-ENOMEM);axd_shim_fail_alloc=0;
    axd_shim_next_dma=0x1ffff000ull;count=0;
    assert(!axd_ring_alloc(&ring,&dev,256) && ring.bytes==4096 && ring.sw.count==256);
    assert(ring.sw.pointer_base==0x1ffff000u && ring.sw.pointer_mask==0xfffu && ring.sw.in==0 && ring.sw.out==0);
    assert(!axd_ring_write(&ring,255,&d) && events[count-1].kind==SHIM_DMA_WMB);
    assert(!axd_ring_read(&ring,255,&back) && events[count-1].kind==SHIM_DMA_RMB && !memcmp(&d,&back,sizeof(d)));
    assert(axd_ring_write(&ring,256,&d)==-EINVAL && axd_ring_read(&ring,256,&back)==-EINVAL);
    axd_ring_free(&ring);assert(events[count-1].kind==SHIM_FREE && !ring.cpu && axd_ring_write(&ring,0,&d)==-EINVAL);
    /* A bus address above 4 GiB cannot be described and is released. */
    axd_shim_next_dma=0x100000000ull;count=0;
    assert(axd_ring_alloc(&ring,&dev,64)==-ERANGE && count==1 && events[0].kind==SHIM_FREE && !ring.cpu);
    puts("kernel glue: hw_access gate, bounds/alignment, wrapper identity, delay cap, barriers and ring memory passed");
    return 0;
}
