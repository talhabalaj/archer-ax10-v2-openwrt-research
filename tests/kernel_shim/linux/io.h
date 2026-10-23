/* SPDX-License-Identifier: MIT */
#ifndef AXD_SHIM_IO_H
#define AXD_SHIM_IO_H
#include <string.h>
#include "types.h"
/* Relaxed accessors over an in-memory register file; each access is logged
 * with its address so tests can check offsets, widths and values. */
static inline u16 readw_relaxed(const volatile void *a)
{u16 v;memcpy(&v,(const void *)a,2);axd_shim_log(SHIM_R16,(uintptr_t)a,v);return v;}
static inline u32 readl_relaxed(const volatile void *a)
{u32 v;memcpy(&v,(const void *)a,4);axd_shim_log(SHIM_R32,(uintptr_t)a,v);return v;}
static inline void writew_relaxed(u16 v,volatile void *a)
{memcpy((void *)a,&v,2);axd_shim_log(SHIM_W16,(uintptr_t)a,v);}
static inline void writel_relaxed(u32 v,volatile void *a)
{memcpy((void *)a,&v,4);axd_shim_log(SHIM_W32,(uintptr_t)a,v);}
#define wmb() axd_shim_log(SHIM_WMB,0,0)
#define mb() axd_shim_log(SHIM_MB,0,0)
#define dma_wmb() axd_shim_log(SHIM_DMA_WMB,0,0)
#define dma_rmb() axd_shim_log(SHIM_DMA_RMB,0,0)
#endif
