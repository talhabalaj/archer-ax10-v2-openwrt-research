/* SPDX-License-Identifier: MIT */
/* Host test shim for kernel/ glue: just the types and annotations it uses. */
#ifndef AXD_SHIM_TYPES_H
#define AXD_SHIM_TYPES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;
typedef uint64_t dma_addr_t;typedef size_t resource_size_t;
#define __iomem
#define lower_32_bits(n) ((u32)((n) & 0xffffffffu))
#define upper_32_bits(n) ((u32)((uint64_t)(n) >> 32))
/* Every observable primitive is recorded: kind, address/offset, value. */
enum axd_shim_kind {SHIM_WMB=1,SHIM_MB,SHIM_DMA_WMB,SHIM_DMA_RMB,SHIM_UDELAY,SHIM_R16,SHIM_R32,SHIM_W16,SHIM_W32,SHIM_FREE};
void axd_shim_log(enum axd_shim_kind kind,uintptr_t where,uint32_t value);
#endif
