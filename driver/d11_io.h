/* SPDX-License-Identifier: MIT */
#ifndef AXD_D11_IO_H
#define AXD_D11_IO_H
#include <stdint.h>

/* Portable synchronous D11-relative trace events, with no transport backend.
 * Register access width is part of kind; delay uses value only. Barriers are
 * explicit events, not host memory-order instructions.
 */
enum axd_d11_kind {
    AXD_D11_STORE_BARRIER = 1,
    AXD_D11_FULL_BARRIER,
    AXD_D11_WRITE32,
    AXD_D11_READ32,
    AXD_D11_READ16,
    AXD_D11_DELAY_US
};
struct axd_d11_operation {
    enum axd_d11_kind kind;
    uint32_t offset, value; /* D11-relative byte offset; delay uses value only. */
};
/* Set *read_value on a successful read. Other events ignore it. */
typedef int (*axd_d11_io_fn)(void *context,
                           const struct axd_d11_operation *operation,
                           uint32_t *read_value);
#endif
