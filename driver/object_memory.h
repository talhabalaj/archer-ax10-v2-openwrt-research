/* SPDX-License-Identifier: MIT */
#ifndef AXD_OBJECT_MEMORY_H
#define AXD_OBJECT_MEMORY_H
#include "d11_io.h"
#include <stddef.h>

enum axd_object_command {
    AXD_OBJECT_COPY = 1, AXD_OBJECT_COPY32, AXD_OBJECT_SET32, AXD_OBJECT_WRITE16
};
struct axd_object_request {
    enum axd_object_command command;
    uint32_t address, selector, value;
    int32_t byte_count;
    const uint8_t *data;
    size_t data_length;
};
struct axd_object_context { uint16_t core, minor; uint8_t owned; };
enum axd_object_phase {
    AXD_OBJECT_INVALID = 0, AXD_OBJECT_RUNNING, AXD_OBJECT_DONE,
    AXD_OBJECT_DEPENDENCY, AXD_OBJECT_FAILED, AXD_OBJECT_CANCELLED
};
enum axd_object_error {
    AXD_OBJECT_NO_ERROR = 0, AXD_OBJECT_BAD_ARGUMENT, AXD_OBJECT_UNSUPPORTED,
    AXD_OBJECT_BOUNDS, AXD_OBJECT_IO_ERROR
};
struct axd_object_memory {
    enum axd_object_phase phase;
    enum axd_object_error error;
    struct axd_object_request request;
    struct axd_object_context context;
    uint32_t completed_bytes, last_read, address_writes;
    unsigned stage, halfword_route, queued, cursor;
    struct axd_d11_operation operations[6];
    uint64_t attempted_ops, completed_ops;
};
/* Current core130 indirect-port writer. Address is a full32 byte address;
 * selector is ORed with its word index, never inferred from the address.
 * COPY follows the actual selector halfword-route predicate and otherwise
 * emits words plus a halfword tail. COPY32/SET32 program auto-increment once,
 * even for nonpositive counts; COPY does nothing for nonpositive counts.
 * Positive COPY counts must be even, COPY32/SET32 counts multiples4. These
 * stricter policies reject stock's possible partial-source overread. Actual
 * lane alignment and nonwrapping ranges are checked before any callback.
 * Buffer is borrowed immutable storage outside the job through terminal state.
 * owned0 is reached DEPENDENCY, never a successful missing-resource stub.
 * No object geometry, MMIO backend, lock or hardware commitment is supplied.
 * Both request/context snapshots may alias this job safely at begin.
 */
int axd_object_memory_begin(struct axd_object_memory *,
                              const struct axd_object_request *,
                              const struct axd_object_context *);
/* At most one synchronous callback per step. Readbacks are mandatory supplied
 * samples, discarded by stock, not write echoes/readiness. Successful data
 * callbacks retain completed_bytes even when cancelling; callback error wins.
 * No terminal cleanup, retry or rollback. Callbacks may cancel, not reenter.
 */
enum axd_object_phase axd_object_memory_step(struct axd_object_memory *, axd_d11_io_fn, void *);
enum axd_object_phase axd_object_memory_cancel(struct axd_object_memory *);
#endif
