/* SPDX-License-Identifier: MIT */
#ifndef AXD_AI_RESET_H
#define AXD_AI_RESET_H
#include <stdint.h>

/* Offline current-HND D11 core130 AI wrapper protocol. A transport resolves
 * wrapper0=primary, wrapper1=secondary using actual runtime metadata. This model
 * does not discover/map windows or supply clocks, PHY/DMA reset or ownership.
 */
enum axd_reset_kind {
    AXD_RESET_STORE_BARRIER = 1,
    AXD_RESET_FULL_BARRIER,
    AXD_RESET_WRITE32,
    AXD_RESET_READ32,
    AXD_RESET_DELAY_US
};
struct axd_reset_operation {
    enum axd_reset_kind kind;
    unsigned wrapper;
    uint32_t offset, value;
};
struct axd_reset_context {
    uint16_t core_id, revision;
    uint8_t interconnect, secondary_present;
    uint32_t bits, reset_bits; /* Already resolved caller inputs, not defaults. */
};
enum axd_reset_phase {
    AXD_RESET_INVALID = 0,
    AXD_RESET_RUNNING,
    AXD_RESET_TRACE_COMPLETE,
    AXD_RESET_FAILED,
    AXD_RESET_CANCELLED
};
enum axd_reset_error {
    AXD_RESET_NO_ERROR = 0,
    AXD_RESET_BAD_ARGUMENT,
    AXD_RESET_UNSUPPORTED,
    AXD_RESET_IO_ERROR
};
struct axd_ai_reset {
    enum axd_reset_phase phase;
    enum axd_reset_error error;
    struct axd_reset_context context;
    unsigned wrapper, stage, after_busy, queued, cursor, busy_repetitions;
    /* release_writes counts successful zero-write callbacks on current wrapper. */
    unsigned release_writes, completed_wrappers, reset_nonzero_mask, busy_exhaustions;
    uint32_t last_read;
    struct axd_reset_operation operations[5];
    uint64_t attempted_ops, completed_ops;
};
typedef int (*axd_reset_io_fn)(void *context,
                             const struct axd_reset_operation *operation,
                             uint32_t *read_value);
int axd_ai_reset_begin(struct axd_ai_reset *job,
                        const struct axd_reset_context *context);
/* One callback maximum per step. Delays synchronous through backend; no busy
 * loops inside a step. Each busy wait allows 30 delays of 10us; release allows 9
 * writes. Busy exhaustion/stuck reset do not abort the stock trace: inspect
 * busy_exhaustions and reset_nonzero_mask. TRACE_COMPLETE is not reset success.
 * No rollback/retry. Single-threaded; callbacks may cancel but not reenter or
 * mutate fields. Successful callback cancellation survives, callback error wins.
 */
enum axd_reset_phase axd_ai_reset_step(struct axd_ai_reset *job,
                                        axd_reset_io_fn io, void *context);
enum axd_reset_phase axd_ai_reset_cancel(struct axd_ai_reset *job);
#endif
