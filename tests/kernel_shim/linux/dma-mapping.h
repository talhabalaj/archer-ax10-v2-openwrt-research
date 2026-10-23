/* SPDX-License-Identifier: MIT */
#ifndef AXD_SHIM_DMA_MAPPING_H
#define AXD_SHIM_DMA_MAPPING_H
#include <stdlib.h>
#include "types.h"
#include "device.h"
#include "io.h"
#define GFP_KERNEL 0
#define DMA_BIT_MASK(n) (((n) == 64) ? ~0ULL : ((1ULL << (n)) - 1))
/* Test controls: mask result, next bus address, forced allocation failure. */
extern int axd_shim_mask_result;
extern uint64_t axd_shim_mask_seen, axd_shim_next_dma;
extern int axd_shim_fail_alloc;
static inline int dma_set_mask_and_coherent(struct device *d, uint64_t mask)
{(void)d;axd_shim_mask_seen=mask;return axd_shim_mask_result;}
/* Like the real API: aligned to the smallest page order covering size. */
static inline void *dma_alloc_coherent(struct device *d, size_t size, dma_addr_t *handle, int gfp)
{
    size_t align=4096;void *p;
    (void)d;(void)gfp;
    while(align<size)align<<=1;
    if(axd_shim_fail_alloc)return NULL;
    p=aligned_alloc(align,(size+align-1)/align*align);
    if(p){memset(p,0,size);*handle=axd_shim_next_dma;}
    return p;
}
static inline void dma_free_coherent(struct device *d, size_t size, void *cpu, dma_addr_t handle)
{(void)d;(void)size;axd_shim_log(SHIM_FREE,(uintptr_t)cpu,(u32)handle);free(cpu);}
#endif
