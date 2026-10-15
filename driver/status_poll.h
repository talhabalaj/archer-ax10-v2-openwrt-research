/* SPDX-License-Identifier: MIT */
#ifndef AXD_STATUS_POLL_H
#define AXD_STATUS_POLL_H
#include <stdint.h>

/* Generic simulated status wait. No BCM63178 readiness register/mask or
 * reset/start procedure is supplied: all predicates are caller-defined.
 */
enum axd_poll_state {
    AXD_POLL_INVALID = 0,
    AXD_POLL_WAITING,
    AXD_POLL_READY,
    AXD_POLL_TIMED_OUT,
    AXD_POLL_FAILED,
    AXD_POLL_CANCELLED
};

enum axd_poll_error {
    AXD_POLL_NO_ERROR = 0,
    AXD_POLL_BAD_ARGUMENT,
    AXD_POLL_READ_ERROR,
    AXD_POLL_CLOCK_ERROR
};

struct axd_status_poll {
    enum axd_poll_state state;
    enum axd_poll_error error;
    uint16_t reg;
    uint32_t mask, expected, timeout_us, interval_us, last_value;
    uint64_t start_us, last_us, next_poll_us, attempts, reads;
};

typedef int (*axd_status_read_fn)(void *context, uint16_t reg, uint32_t *value);

int axd_poll_begin(struct axd_status_poll *poll, uint16_t reg, uint32_t mask,
                    uint32_t expected, uint64_t now_us,
                    uint32_t timeout_us, uint32_t interval_us);

/* At most one read per step; no sleeping. Clock must be nondecreasing.
 * Timeout takes precedence at elapsed >= timeout_us, with no deadline read.
 * next_poll_us is relative to start_us. Terminal calls perform no callbacks.
 * A read error can still have backend side effects; attempts != successful reads.
 * Single-threaded API: callbacks may cancel, but must not call begin/step or
 * mutate fields. Successful reads preserve cancellation; read errors take
 * precedence when cancellation and failure coincide.
 */
enum axd_poll_state axd_poll_step(struct axd_status_poll *poll, uint64_t now_us,
                                 axd_status_read_fn read, void *context);
enum axd_poll_state axd_poll_cancel(struct axd_status_poll *poll);
#endif
