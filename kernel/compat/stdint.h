/* SPDX-License-Identifier: MIT */
/* Kernel build only: map the portable components' fixed-width types to the
 * kernel's own definitions. Never used by host builds. */
#ifndef AXD_KERNEL_COMPAT_STDINT_H
#define AXD_KERNEL_COMPAT_STDINT_H
#include <linux/types.h>
#include <linux/limits.h>
#ifndef UINT8_MAX
#define UINT8_MAX U8_MAX
#endif
#ifndef UINT16_MAX
#define UINT16_MAX U16_MAX
#endif
#ifndef UINT32_MAX
#define UINT32_MAX U32_MAX
#endif
#ifndef UINT64_MAX
#define UINT64_MAX U64_MAX
#endif
#ifndef UINT32_C
#define UINT32_C(x) (x##U)
#endif
#endif
