/* SPDX-License-Identifier: MIT */
#include "../driver/status_poll.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

struct fake_status {
    unsigned int calls, ready_at, fail_at;
    struct axd_status_poll *cancel_job;
};
static int read_status(void *ctx, uint16_t reg, uint32_t *value)
{
    struct fake_status *status = ctx;
    assert(reg == 0x200); /* Synthetic, not a recovered readiness register. */
    status->calls++;
    if (status->cancel_job != NULL)
        axd_poll_cancel(status->cancel_job);
    if (status->calls == status->fail_at)
        return -1;
    *value = status->calls >= status->ready_at ? 0x84 : 0x80;
    return 0;
}

int main(void)
{
    struct axd_status_poll poll = {0};
    struct fake_status status = {0, 1, 0, NULL};
    assert(axd_poll_step(&poll, 0, read_status, &status) == AXD_POLL_INVALID);
    assert(status.calls == 0);
    assert(axd_poll_begin(&poll, 0x200, 4, 4, 100, 30, 10) == 0);
    assert(axd_poll_step(&poll, 100, read_status, &status) == AXD_POLL_READY);
    assert(poll.reads == 1 && poll.last_value == 0x84);
    assert(axd_poll_step(&poll, 0, NULL, NULL) == AXD_POLL_READY);
    assert(axd_poll_cancel(&poll) == AXD_POLL_READY && status.calls == 1);

    status.calls = 0;
    status.ready_at = 3;
    assert(axd_poll_begin(&poll, 0x200, 4, 4, 100, 30, 10) == 0);
    assert(axd_poll_step(&poll, 100, read_status, &status) == AXD_POLL_WAITING);
    assert(axd_poll_step(&poll, 100, read_status, &status) == AXD_POLL_WAITING && status.calls == 1);
    assert(axd_poll_step(&poll, 109, read_status, &status) == AXD_POLL_WAITING && status.calls == 1);
    assert(axd_poll_step(&poll, 110, read_status, &status) == AXD_POLL_WAITING);
    assert(axd_poll_step(&poll, 120, read_status, &status) == AXD_POLL_READY && status.calls == 3);

    status.calls = 0;
    status.ready_at = 99;
    assert(axd_poll_begin(&poll, 0x200, 4, 4, 100, 20, 10) == 0);
    assert(axd_poll_step(&poll, 100, read_status, &status) == AXD_POLL_WAITING);
    assert(axd_poll_step(&poll, 110, read_status, &status) == AXD_POLL_WAITING);
    assert(axd_poll_step(&poll, 120, read_status, &status) == AXD_POLL_TIMED_OUT && status.calls == 2);
    assert(axd_poll_step(&poll, 130, NULL, NULL) == AXD_POLL_TIMED_OUT);
    assert(axd_poll_cancel(&poll) == AXD_POLL_TIMED_OUT);

    status.calls = 0;
    status.fail_at = 1;
    assert(axd_poll_begin(&poll, 0x200, 4, 4, 0, 20, 10) == 0);
    assert(axd_poll_step(&poll, 0, read_status, &status) == AXD_POLL_FAILED);
    assert(poll.attempts == 1 && poll.reads == 0 && poll.error == AXD_POLL_READ_ERROR);
    assert(axd_poll_step(&poll, 0, read_status, &status) == AXD_POLL_FAILED && status.calls == 1);

    status.calls = 0;
    status.fail_at = 0;
    assert(axd_poll_begin(&poll, 0x200, 4, 4, 100, 20, 10) == 0);
    assert(axd_poll_step(&poll, 99, read_status, &status) == AXD_POLL_FAILED);
    assert(poll.error == AXD_POLL_CLOCK_ERROR && status.calls == 0);
    assert(axd_poll_begin(&poll, 0x200, 4, 4, UINT64_MAX - 20, 20, 10) == 0);
    assert(axd_poll_step(&poll, UINT64_MAX, read_status, &status) == AXD_POLL_TIMED_OUT);
    assert(poll.attempts == 0);
    assert(axd_poll_begin(&poll, 0x200, 4, 0, 0, 20, 10) == 0);
    assert(axd_poll_step(&poll, 0, read_status, &status) == AXD_POLL_READY);
    status.calls = 0;
    assert(axd_poll_begin(&poll, 0x200, 4, 4, 0, 20, 10) == 0);
    assert(axd_poll_step(&poll, 0, NULL, NULL) == AXD_POLL_FAILED && poll.attempts == 0);
    assert(axd_poll_begin(&poll, 0x200, 4, 4, 0, 20, 10) == 0);
    assert(axd_poll_cancel(&poll) == AXD_POLL_CANCELLED);
    assert(axd_poll_step(&poll, 0, read_status, &status) == AXD_POLL_CANCELLED && status.calls == 0);
    assert(axd_poll_begin(&poll, 0x200, 4, 4, 0, 20, 10) == 0);
    assert(axd_poll_step(&poll, 0, read_status, &status) == AXD_POLL_WAITING);
    assert(axd_poll_cancel(&poll) == AXD_POLL_CANCELLED);
    assert(axd_poll_step(&poll, 10, read_status, &status) == AXD_POLL_CANCELLED && status.calls == 1);
    assert(axd_poll_begin(&poll, 0x201, 4, 4, 0, 20, 10) != 0);
    assert(axd_poll_begin(&poll, 0x200, 0, 0, 0, 20, 10) != 0);
    assert(axd_poll_begin(&poll, 0x200, 4, 8, 0, 20, 10) != 0);
    assert(axd_poll_begin(&poll, 0x200, 4, 4, 0, 0, 10) != 0);
    assert(axd_poll_begin(&poll, 0x200, 4, 4, 0, 20, 0) != 0);
    status.calls = 0;
    status.ready_at = 1;
    status.cancel_job = &poll;
    assert(axd_poll_begin(&poll, 0x200, 4, 4, 0, 20, 10) == 0);
    assert(axd_poll_step(&poll, 0, read_status, &status) == AXD_POLL_CANCELLED);
    assert(poll.attempts == 1 && poll.reads == 1 && poll.last_value == 0x84);
    assert(axd_poll_step(&poll, 10, NULL, NULL) == AXD_POLL_CANCELLED && status.calls == 1);
    status.calls = 0;
    status.fail_at = 1;
    assert(axd_poll_begin(&poll, 0x200, 4, 4, 0, 20, 10) == 0);
    assert(axd_poll_step(&poll, 0, read_status, &status) == AXD_POLL_FAILED);
    assert(poll.error == AXD_POLL_READ_ERROR && poll.reads == 0 && poll.attempts == 1);
    puts("status polling: immediate/delayed success, timeout, stalled/backward clocks, cancellation and errors passed");
    return 0;
}
