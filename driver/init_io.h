/* SPDX-License-Identifier: MIT */
#ifndef AXD_INIT_IO_H
#define AXD_INIT_IO_H
#include "effective_init.h"
#include "object_memory.h"

struct axd_init_io_request {
    const uint8_t *data;
    size_t length;
    const uint32_t *offset130_addend;
    uint32_t mmio_window, object_window;
};
enum axd_init_io_path { AXD_INIT_IO_DIRECT = 1, AXD_INIT_IO_OBJECT };
struct axd_init_io_operation {
    enum axd_init_io_path path;
    struct axd_d11_operation operation;
};
typedef int (*axd_init_io_fn)(void *, const struct axd_init_io_operation *, uint32_t *);
enum axd_init_io_phase {
    AXD_INIT_IO_INVALID = 0, AXD_INIT_IO_RUNNING, AXD_INIT_IO_DONE,
    AXD_INIT_IO_DEPENDENCY, AXD_INIT_IO_FAILED, AXD_INIT_IO_CANCELLED
};
enum axd_init_io_error {
    AXD_INIT_IO_NO_ERROR = 0, AXD_INIT_IO_BAD_ARGUMENT,
    AXD_INIT_IO_TABLE_ERROR, AXD_INIT_IO_OBJECT_ERROR, AXD_INIT_IO_CALLBACK_ERROR
};
struct axd_init_io {
    enum axd_init_io_phase phase;
    enum axd_init_io_error error;
    struct axd_init_io_request request;
    struct axd_object_context context;
    struct axd_effective_init effective;
    struct axd_object_memory object;
    uint32_t addend;
    uint8_t object_data[2];
    unsigned object_active;
    size_t completed_records, skipped_records;
    uint64_t attempted_ops, completed_ops;
};
/* Complete physical-port lowering of the existing effective-init contract.
 * Same whole-table bounds/alignment/addend preflight; no proprietary tables or
 * board defaults. Object copies execute the real COPY helper, not an accepted
 * semantic callback standing in for I/O. The table is borrowed immutable storage
 * outside the job; the optional addend is snapshotted before clearing this job.
 * request/context and addend may alias prior job snapshots safely. Capacities
 * are supplied software bounds, not asserted hardware geometry. Resource
 * ownership is required only when reached. No MMIO backend or lock supplied.
 */
int axd_init_io_begin(struct axd_init_io *, const struct axd_init_io_request *,
                        const struct axd_object_context *);
/* One user callback maximum per step. Physical completed_records waits for
 * whole object-helper return; partial object bytes/readbacks remain explicit.
 * effective.* counters describe semantic dispatch, not physical callbacks.
 * Error wins cancellation, successful callback effects survive; terminals are
 * silent, with no automatic unwind/retry/rollback. Single-threaded callbacks
 * may cancel, never reenter or mutate jobs.
 */
enum axd_init_io_phase axd_init_io_step(struct axd_init_io *, axd_init_io_fn, void *);
enum axd_init_io_phase axd_init_io_cancel(struct axd_init_io *);
#endif
