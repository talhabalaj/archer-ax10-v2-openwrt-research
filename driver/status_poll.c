/* SPDX-License-Identifier: MIT */
#include "status_poll.h"
#include <stddef.h>

int axd_poll_begin(struct axd_status_poll *poll, uint16_t reg, uint32_t mask,
                    uint32_t expected, uint64_t now_us,
                    uint32_t timeout_us, uint32_t interval_us)
{
    static const struct axd_status_poll empty={0};
    if (poll == NULL)
        return -1;
    *poll = empty;
    poll->state = AXD_POLL_FAILED;
    poll->error = AXD_POLL_BAD_ARGUMENT;
    if (reg > 0xffcu || reg % 4 != 0 || mask == 0 || (expected & ~mask) != 0 ||
        timeout_us == 0 || interval_us == 0 || interval_us > timeout_us)
        return -1;
    poll->reg = reg;
    poll->mask = mask;
    poll->expected = expected;
    poll->timeout_us = timeout_us;
    poll->interval_us = interval_us;
    poll->start_us = poll->last_us = now_us;
    poll->error = AXD_POLL_NO_ERROR;
    poll->state = AXD_POLL_WAITING;
    return 0;
}

enum axd_poll_state axd_poll_step(struct axd_status_poll *poll, uint64_t now_us,
                                 axd_status_read_fn read, void *context)
{
    uint64_t elapsed;
    uint32_t value = 0;
    if (poll == NULL)
        return AXD_POLL_INVALID;
    if (poll->state != AXD_POLL_WAITING)
        return poll->state;
    if (now_us < poll->last_us) {
        poll->error = AXD_POLL_CLOCK_ERROR;
        poll->state = AXD_POLL_FAILED;
        return poll->state;
    }
    poll->last_us = now_us;
    elapsed = now_us - poll->start_us;
    if (elapsed >= poll->timeout_us) {
        poll->state = AXD_POLL_TIMED_OUT;
        return poll->state;
    }
    if (elapsed < poll->next_poll_us)
        return poll->state;
    if (read == NULL) {
        poll->error = AXD_POLL_BAD_ARGUMENT;
        poll->state = AXD_POLL_FAILED;
        return poll->state;
    }
    poll->attempts++;
    if (read(context, poll->reg, &value) != 0) {
        poll->error = AXD_POLL_READ_ERROR;
        poll->state = AXD_POLL_FAILED;
        return poll->state;
    }
    poll->reads++;
    poll->last_value = value;
    if (poll->state == AXD_POLL_CANCELLED)
        return poll->state;
    if ((value & poll->mask) == poll->expected)
        poll->state = AXD_POLL_READY;
    else
        poll->next_poll_us = elapsed + poll->interval_us;
    return poll->state;
}

enum axd_poll_state axd_poll_cancel(struct axd_status_poll *poll)
{
    if (poll == NULL)
        return AXD_POLL_INVALID;
    if (poll->state == AXD_POLL_WAITING)
        poll->state = AXD_POLL_CANCELLED;
    return poll->state;
}
