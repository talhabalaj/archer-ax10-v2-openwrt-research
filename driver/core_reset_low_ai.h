/* SPDX-License-Identifier: MIT */
#ifndef AXD_LOW_AI_H
#define AXD_LOW_AI_H
#include <stdint.h>

/* Current-HND selected-wrapper reset protocol. d11_special is the recovered
 * core812 revision61 or128..132 predicate; other cores use generic reset.
 * Exactly one selected wrapper per invocation; selection is owned by parent. */
enum axd_low_ai_kind {
    AXD_LOW_AI_STORE_BARRIER = 1,
    AXD_LOW_AI_FULL_BARRIER,
    AXD_LOW_AI_WRITE32,
    AXD_LOW_AI_READ32,
    AXD_LOW_AI_DELAY_US
};
struct axd_low_ai_operation {
    enum axd_low_ai_kind kind;
    unsigned wrapper;
    uint32_t offset, value;
};
struct axd_low_ai_context {
    uint16_t core_id, revision;
    uint8_t interconnect, d11_special;
    uint32_t bits, reset_bits; /* Already resolved caller inputs, not defaults. */
};
enum axd_low_ai_phase {
    AXD_LOW_AI_INVALID = 0,
    AXD_LOW_AI_RUNNING,
    AXD_LOW_AI_TRACE_COMPLETE,
    AXD_LOW_AI_FAILED,
    AXD_LOW_AI_CANCELLED
};
enum axd_low_ai_error {
    AXD_LOW_AI_NO_ERROR = 0,
    AXD_LOW_AI_BAD_ARGUMENT,
    AXD_LOW_AI_UNSUPPORTED,
    AXD_LOW_AI_IO_ERROR
};
struct axd_low_ai {
    enum axd_low_ai_phase phase;
    enum axd_low_ai_error error;
    struct axd_low_ai_context context;
    unsigned wrapper, stage, after_busy, queued, cursor, busy_repetitions;
    /* release_writes counts successful zero-write callbacks on current wrapper. */
    unsigned release_writes, completed_wrappers, reset_nonzero_mask, busy_exhaustions;
    uint32_t last_read;
    struct axd_low_ai_operation operations[5];
    uint64_t attempted_ops, completed_ops;
};
typedef int (*axd_low_ai_io_fn)(void *context,
                             const struct axd_low_ai_operation *operation,
                             uint32_t *read_value);
int axd_low_ai_begin(struct axd_low_ai *job,
                        const struct axd_low_ai_context *context);
/* One callback maximum per step. Delays synchronous through backend; no busy
 * loops inside a step. Each busy wait allows 30 delays of 10us; release allows 9
 * writes. Busy exhaustion/stuck reset do not abort the stock trace: inspect
 * busy_exhaustions and reset_nonzero_mask. TRACE_COMPLETE is not reset success.
 * No rollback/retry. Single-threaded; callbacks may cancel but not reenter or
 * mutate fields. Successful callback cancellation survives, callback error wins.
 */
enum axd_low_ai_phase axd_low_ai_step(struct axd_low_ai *job,
                                        axd_low_ai_io_fn io, void *context);
enum axd_low_ai_phase axd_low_ai_cancel(struct axd_low_ai *job);
#endif
