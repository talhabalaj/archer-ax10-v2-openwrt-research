/* SPDX-License-Identifier: MIT */
#ifndef AXD_EFFECTIVE_INIT_H
#define AXD_EFFECTIVE_INIT_H
#include <stddef.h>
#include <stdint.h>

/* Semantic trace of the recovered core-130 initialization consumer. Object
 * operations stop at the copyto_objmem helper boundary, not physical ports.
 * No device backend is supplied. Source bytes must remain immutable/alive.
 */
enum axd_init_kind {
    AXD_INIT_STORE_BARRIER = 1,
    AXD_INIT_WRITE16,
    AXD_INIT_WRITE32,
    AXD_INIT_OBJECT_COPY16
};

struct axd_init_operation {
    enum axd_init_kind kind;
    uint32_t offset, value, selector;
};

enum axd_init_phase {
    AXD_INIT_INVALID = 0,
    AXD_INIT_BARRIER,
    AXD_INIT_OPERATION,
    AXD_INIT_SKIP,
    AXD_INIT_COMPLETE,
    AXD_INIT_FAILED,
    AXD_INIT_CANCELLED
};

enum axd_init_error {
    AXD_INIT_NO_ERROR = 0,
    AXD_INIT_BAD_ARGUMENT,
    AXD_INIT_BAD_TABLE,
    AXD_INIT_UNSUPPORTED_CORE,
    AXD_INIT_CONTEXT_MISSING,
    AXD_INIT_BOUNDS,
    AXD_INIT_CALLBACK_ERROR
};

struct axd_effective_init {
    enum axd_init_phase phase;
    enum axd_init_error error;
    const uint8_t *data;
    size_t record_count, record_index, completed_records, skipped_records;
    uint32_t addend;
    struct axd_init_operation operation;
    uint64_t attempted_ops, completed_ops;
};

typedef int (*axd_init_emit_fn)(void *context, const struct axd_init_operation *op);

/* offset130_addend is copied at begin. NULL means unknown and is rejected when
 * an effective write at 0x130 needs it; an explicit pointer to zero is valid.
 * Object-space capacity must be supplied if any object-copy records occur.
 * All actionable records are validated before the first callback. MMIO
 * alignment and bounds are stricter checks than the unchecked stock consumer.
 * Sentinel fields/trailing records are ignored after offset 0xffff, as in stock.
 */
int axd_effective_init_begin(struct axd_effective_init *init,
                              uint16_t core, uint16_t minor,
                              const uint8_t *data, size_t length,
                              const uint32_t *offset130_addend,
                              uint32_t mmio_window, uint32_t object_window);

/* At most one callback or one skipped record per step. A barrier precedes each
 * MMIO event. Object-copy events specify helper arguments: byte offset, low16
 * value, length 2 implicit in kind, selector 0x30000. No helper internals assumed.
 * Single-threaded: callbacks may cancel, never reenter begin/step or mutate the
 * job. Successful callback cancellation is preserved; callback error wins when
 * both occur. Terminal calls do nothing. No rollback or automatic retry.
 */
enum axd_init_phase axd_effective_init_step(struct axd_effective_init *init,
                                            axd_init_emit_fn emit, void *context);
enum axd_init_phase axd_effective_init_cancel(struct axd_effective_init *init);
#endif
