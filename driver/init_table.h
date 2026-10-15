/* SPDX-License-Identifier: MIT */
#ifndef AXD_INIT_TABLE_H
#define AXD_INIT_TABLE_H

#include <stddef.h>
#include <stdint.h>

/* Offline decoder for little-endian {u16 offset, u16 width, u32 value}.
 * Only offsets below 0x1000 are admitted. No MMIO backend, radio enablement,
 * object-memory translation, or hardware initialization is supplied.
 */
enum axd_result {
    AXD_OK = 0,
    AXD_INVALID_ARGUMENT = -1,
    AXD_INVALID_TABLE = -2,
    AXD_OUT_OF_RANGE = -3,
    AXD_WRITE_FAILED = -4
};

struct axd_table_stats {
    size_t writes;
    size_t writes16;
    size_t writes32;
};

typedef int (*axd_write_fn)(void *context, uint16_t offset,
                            uint16_t width, uint32_t value);

int axd_table_validate(const uint8_t *data, size_t length, uint32_t window_size,
                       struct axd_table_stats *stats);

/* Records raw entries; this is NOT a register-write implementation. The stock
 * consumer applies runtime-dependent fixups which are not implemented here.
 * Validates the complete table before the first callback. Values retain their
 * table order and repeats. The caller must keep data immutable throughout.
 * Stops at the first callback error; completed records are reported in written.
 * Callback effects cannot be rolled back. No automatic retry is performed.
 */
int axd_table_replay(const uint8_t *data, size_t length, uint32_t window_size,
                     axd_write_fn write, void *context, size_t *written);

#endif
