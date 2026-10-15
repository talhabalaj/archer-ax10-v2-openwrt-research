/* SPDX-License-Identifier: MIT */
#include "../driver/effective_init.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Synthetic records and literal expectations, independent of the decoder.
 * Object-copy length 2 is implicit in AXD_INIT_OBJECT_COPY16, not an MMIO write.
 */
#define ROW(o, w, v) \
    (uint8_t)((o) & 0xffu), (uint8_t)(((o) >> 8) & 0xffu), \
    (uint8_t)((w) & 0xffu), (uint8_t)(((w) >> 8) & 0xffu), \
    (uint8_t)((v) & 0xffu), (uint8_t)(((v) >> 8) & 0xffu), \
    (uint8_t)(((v) >> 16) & 0xffu), (uint8_t)(((v) >> 24) & 0xffu)

static const uint8_t mixed[] = {
    ROW(0x130u, 2u, 0xffffffffu),
    ROW(0x130u, 4u, 0xffffffffu),
    ROW(0x12eu, 2u, 0xa1b2c3d4u),
    ROW(0x134u, 4u, 0x12345678u),
    ROW(0x135u, 3u, 0x87654321u), /* Unknown MMIO width: skip, no barrier. */
    ROW(0x1000u, 0u, 0xabcdef01u),
    ROW(0x1001u, 1u, 0x12345678u), /* Odd object offset aliases 0x1000. */
    ROW(0x1002u, 2u, 0xfffe0001u),
    ROW(0x1004u, 4u, 0x89abcdefu),
    ROW(0xfffeu, 0xffffu, 0xffffabcdu),
    ROW(0xffffu, 7u, 0xfeedfaceu), /* Sentinel fields are ignored. */
    ROW(0x131u, 4u, 0u) /* Invalid trailing row is ignored. */
};
static const struct axd_init_operation expected[] = {
    {AXD_INIT_STORE_BARRIER, 0, 0, 0},
    {AXD_INIT_WRITE16, 0x130, 1, 0},
    {AXD_INIT_STORE_BARRIER, 0, 0, 0},
    {AXD_INIT_WRITE32, 0x130, 1, 0},
    {AXD_INIT_STORE_BARRIER, 0, 0, 0},
    {AXD_INIT_WRITE16, 0x12e, 0xc3d4, 0},
    {AXD_INIT_STORE_BARRIER, 0, 0, 0},
    {AXD_INIT_WRITE32, 0x134, 0x12345678, 0},
    {AXD_INIT_OBJECT_COPY16, 0x1800, 0xef01, 0x30000},
    {AXD_INIT_OBJECT_COPY16, 0x1800, 0x5678, 0x30000},
    {AXD_INIT_OBJECT_COPY16, 0x1804, 1, 0x30000},
    {AXD_INIT_OBJECT_COPY16, 0x1808, 0xcdef, 0x30000},
    {AXD_INIT_OBJECT_COPY16, 0x1f7fc, 0xabcd, 0x30000}
};
static const size_t records_before[] = {0, 0, 1, 1, 2, 2, 3, 3, 5, 6, 7, 8, 9};
static const size_t records_after[] = {0, 1, 1, 2, 2, 3, 3, 4, 6, 7, 8, 9, 10};

struct recorder {
    const struct axd_init_operation *expected;
    size_t count, events, fail_at, cancel_at;
    struct axd_effective_init *cancel_job;
};

static struct recorder fresh_recorder(void)
{
    struct recorder result = {expected, sizeof(expected) / sizeof(expected[0]),
                              0, (size_t)-1, (size_t)-1, NULL};
    return result;
}

static int record(void *context, const struct axd_init_operation *op)
{
    struct recorder *r = context;
    const struct axd_init_operation *want;
    size_t current = r->events;
    assert(current < r->count);
    want = &r->expected[current];
    assert(op->kind == want->kind && op->offset == want->offset);
    assert(op->value == want->value && op->selector == want->selector);
    r->events++;
    if (r->cancel_job != NULL && current == r->cancel_at)
        assert(axd_effective_init_cancel(r->cancel_job) == AXD_INIT_CANCELLED);
    /* Failure may follow a simulated side effect. */
    return current == r->fail_at ? -1 : 0;
}

static void begin_mixed(struct axd_effective_init *init)
{
    uint32_t adjustment = 2;
    assert(axd_effective_init_begin(init, 130, 1, mixed, sizeof(mixed),
                                   &adjustment, 0x1000, 0x1f7fe) == 0);
    adjustment = 0; /* The model owns a snapshot, not a borrowed context word. */
    assert(init->addend == 2 && init->record_count == 10);
}

static void run(struct axd_effective_init *init, struct recorder *r)
{
    size_t i;
    for (i = 0; i < 32; i++) {
        enum axd_init_phase p = axd_effective_init_step(init, record, r);
        if (p == AXD_INIT_COMPLETE || p == AXD_INIT_FAILED || p == AXD_INIT_CANCELLED)
            return;
    }
    assert(0 && "effective init exceeded bounded synthetic trace");
}

static void terminal(struct axd_effective_init *init, struct recorder *r,
                     enum axd_init_phase phase)
{
    size_t before = r->events;
    assert(axd_effective_init_step(init, record, r) == phase);
    assert(axd_effective_init_step(init, NULL, NULL) == phase);
    assert(axd_effective_init_cancel(init) == phase);
    assert(r->events == before);
}

static void rejected(const uint8_t *data, size_t length, const uint32_t *context,
                     uint32_t mmio_window, uint32_t object_window,
                     enum axd_init_error error)
{
    struct axd_effective_init init;
    struct recorder r = fresh_recorder();
    assert(axd_effective_init_begin(&init, 130, 1, data, length, context,
                                   mmio_window, object_window) != 0);
    assert(init.phase == AXD_INIT_FAILED && init.error == error);
    assert(init.attempted_ops == 0 && init.completed_ops == 0);
    terminal(&init, &r, AXD_INIT_FAILED);
    assert(r.events == 0);
}

static void arithmetic_and_boundaries(void)
{
    static const uint8_t carry[] = {
        ROW(0x130u, 2u, 0x0000ffffu), ROW(0x130u, 4u, 0x0000ffffu),
        ROW(0x130u, 2u, 0xffffffffu), ROW(0x130u, 4u, 0xffffffffu),
        ROW(0xffffu, 0u, 0u)};
    static const struct axd_init_operation want_carry[] = {
        {AXD_INIT_STORE_BARRIER, 0, 0, 0}, {AXD_INIT_WRITE16, 0x130, 0, 0},
        {AXD_INIT_STORE_BARRIER, 0, 0, 0}, {AXD_INIT_WRITE32, 0x130, 0x10000, 0},
        {AXD_INIT_STORE_BARRIER, 0, 0, 0}, {AXD_INIT_WRITE16, 0x130, 0, 0},
        {AXD_INIT_STORE_BARRIER, 0, 0, 0}, {AXD_INIT_WRITE32, 0x130, 0, 0}};
    static const uint8_t boundaries[] = {
        ROW(0x130u, 2u, 0xabcdefffu), ROW(0xffeu, 2u, 0x12345678u),
        ROW(0xffcu, 4u, 0x87654321u), ROW(0xffffu, 0u, 0u)};
    static const struct axd_init_operation want_boundaries[] = {
        {AXD_INIT_STORE_BARRIER, 0, 0, 0}, {AXD_INIT_WRITE16, 0x130, 0xefff, 0},
        {AXD_INIT_STORE_BARRIER, 0, 0, 0}, {AXD_INIT_WRITE16, 0xffe, 0x5678, 0},
        {AXD_INIT_STORE_BARRIER, 0, 0, 0}, {AXD_INIT_WRITE32, 0xffc, 0x87654321, 0}};
    struct axd_effective_init init;
    struct recorder r = fresh_recorder();
    uint32_t addend = 1;
    r.expected = want_carry;
    r.count = sizeof(want_carry) / sizeof(want_carry[0]);
    assert(axd_effective_init_begin(&init, 130, 0, carry, sizeof(carry),
                                   &addend, 0x1000, 0) == 0);
    run(&init, &r);
    assert(init.phase == AXD_INIT_COMPLETE && r.events == 8 && init.completed_records == 4);
    terminal(&init, &r, AXD_INIT_COMPLETE);
    addend = 0;
    r = fresh_recorder();
    r.expected = want_boundaries;
    r.count = sizeof(want_boundaries) / sizeof(want_boundaries[0]);
    assert(axd_effective_init_begin(&init, 130, 1, boundaries, sizeof(boundaries),
                                   &addend, 0x1000, 0) == 0);
    run(&init, &r);
    assert(init.phase == AXD_INIT_COMPLETE && r.events == 6 && init.completed_records == 3);
    terminal(&init, &r, AXD_INIT_COMPLETE);
}

static void validation(void)
{
    const uint32_t zero = 0;
    struct axd_effective_init init;
    struct recorder r = fresh_recorder();
    static const uint8_t misaligned[] = {
        ROW(0x120u, 4u, 1u), ROW(0x131u, 2u, 2u), ROW(0xffffu, 0u, 0u)};
    static const uint8_t misaligned32[] = {ROW(0x132u, 4u, 0u), ROW(0xffffu, 0u, 0u)};
    static const uint8_t late_context[] = {
        ROW(0x120u, 4u, 1u), ROW(0x130u, 2u, 2u), ROW(0xffffu, 0u, 0u)};
    static const uint8_t end16[] = {ROW(0xffeu, 2u, 0xffffu), ROW(0xffffu, 0u, 0u)};
    static const uint8_t end32[] = {ROW(0xffcu, 4u, 0xffffffffu), ROW(0xffffu, 0u, 0u)};
    static const uint8_t object[] = {ROW(0x1000u, 0u, 1u), ROW(0xffffu, 0u, 0u)};
    static const uint8_t unterminated[] = {ROW(0x120u, 4u, 1u)};
    static const uint8_t only_end[] = {
        ROW(0xffffu, 0xffffu, 0xffffffffu), ROW(0x131u, 4u, 1u)};
    static const uint8_t skip[] = {
        ROW(0x130u, 0u, 0xffffffffu), ROW(0xfffu, 1u, 1u), ROW(0xffffu, 0u, 0u)};

    rejected(misaligned, sizeof(misaligned), &zero, 0x1000, 0, AXD_INIT_BOUNDS);
    rejected(misaligned32, sizeof(misaligned32), &zero, 0x1000, 0, AXD_INIT_BOUNDS);
    rejected(late_context, sizeof(late_context), NULL, 0x1000, 0, AXD_INIT_CONTEXT_MISSING);
    rejected(end16, sizeof(end16), NULL, 0xfff, 0, AXD_INIT_BOUNDS);
    rejected(end32, sizeof(end32), NULL, 0xfff, 0, AXD_INIT_BOUNDS);
    rejected(object, sizeof(object), NULL, 0x1000, 0, AXD_INIT_BOUNDS);
    rejected(object, sizeof(object), NULL, 0x1000, 0x1801, AXD_INIT_BOUNDS);
    rejected(mixed, sizeof(mixed), &zero, 0x1000, 0x1f7fd, AXD_INIT_BOUNDS);
    rejected(unterminated, sizeof(unterminated), NULL, 0x1000, 0, AXD_INIT_BAD_TABLE);
    rejected(only_end, sizeof(only_end) - 1, NULL, 0x1000, 0, AXD_INIT_BAD_TABLE);
    rejected(only_end, 0, NULL, 0x1000, 0, AXD_INIT_BAD_TABLE);
    rejected(NULL, sizeof(only_end), NULL, 0x1000, 0, AXD_INIT_BAD_ARGUMENT);
    rejected(only_end, sizeof(only_end), NULL, 0, 0, AXD_INIT_BAD_ARGUMENT);
    rejected(only_end, sizeof(only_end), NULL, 0x1001, 0, AXD_INIT_BAD_ARGUMENT);
    assert(axd_effective_init_begin(NULL, 130, 1, only_end, sizeof(only_end),
                                   NULL, 0x1000, 0) != 0);
    assert(axd_effective_init_begin(&init, 129, 1, only_end, sizeof(only_end),
                                   NULL, 0x1000, 0) != 0);
    assert(init.error == AXD_INIT_UNSUPPORTED_CORE);
    terminal(&init, &r, AXD_INIT_FAILED);
    assert(axd_effective_init_begin(&init, 130, 2, only_end, sizeof(only_end),
                                   NULL, 0x1000, 0) != 0);
    assert(init.error == AXD_INIT_UNSUPPORTED_CORE);
    terminal(&init, &r, AXD_INIT_FAILED);
    assert(axd_effective_init_begin(&init, 130, 0, only_end, sizeof(only_end),
                                   NULL, 0x1000, 0) == 0);
    assert(init.phase == AXD_INIT_COMPLETE && init.record_count == 0);
    terminal(&init, &r, AXD_INIT_COMPLETE);

    /* Skipped widths need neither context nor a callback, even at offset0x130. */
    assert(axd_effective_init_begin(&init, 130, 1, skip, sizeof(skip), NULL, 1, 0) == 0);
    assert(init.phase == AXD_INIT_SKIP);
    assert(axd_effective_init_step(&init, NULL, NULL) == AXD_INIT_SKIP);
    assert(axd_effective_init_step(&init, NULL, NULL) == AXD_INIT_COMPLETE);
    assert(init.skipped_records == 2 && init.completed_records == 2);
    assert(init.attempted_ops == 0 && init.completed_ops == 0);
    terminal(&init, &r, AXD_INIT_COMPLETE);
    assert(axd_effective_init_begin(&init, 130, 1, skip, sizeof(skip), NULL, 1, 0) == 0);
    assert(axd_effective_init_cancel(&init) == AXD_INIT_CANCELLED);
    terminal(&init, &r, AXD_INIT_CANCELLED);

    /* Exact MMIO boundaries and explicit zero context remain valid. */
    assert(axd_effective_init_begin(&init, 130, 1, end16, sizeof(end16), NULL, 0x1000, 0) == 0);
    assert(axd_effective_init_begin(&init, 130, 1, end32, sizeof(end32), NULL, 0x1000, 0) == 0);
    assert(axd_effective_init_begin(&init, 130, 1, late_context, sizeof(late_context),
                                   &zero, 0x1000, 0) == 0);
    assert(init.addend == 0);
    assert(axd_effective_init_step(&init, NULL, NULL) == AXD_INIT_FAILED);
    assert(init.error == AXD_INIT_BAD_ARGUMENT && init.attempted_ops == 0);
    terminal(&init, &r, AXD_INIT_FAILED);
}

int main(void)
{
    struct axd_effective_init init;
    struct recorder r = fresh_recorder();
    const size_t events = sizeof(expected) / sizeof(expected[0]);
    size_t i;
    begin_mixed(&init);
    run(&init, &r);
    assert(init.phase == AXD_INIT_COMPLETE && init.error == AXD_INIT_NO_ERROR);
    assert(r.events == events && init.attempted_ops == events && init.completed_ops == events);
    assert(init.record_index == 10 && init.completed_records == 10 && init.skipped_records == 1);
    terminal(&init, &r, AXD_INIT_COMPLETE);

    /* Chaining from its own opaque runtime addend must snapshot before clear. */
    init.addend = 0x12345678u;
    assert(axd_effective_init_begin(&init, 130, 1, mixed, sizeof(mixed),
                                   &init.addend, 0x1000, 0x20000) == 0);
    assert(init.addend == 0x12345678u);

    /* Every callback kind, both widths, every object width and final operation. */
    for (i = 0; i < events; i++) {
        r = fresh_recorder();
        r.fail_at = i;
        begin_mixed(&init);
        run(&init, &r);
        assert(init.phase == AXD_INIT_FAILED && init.error == AXD_INIT_CALLBACK_ERROR);
        assert(r.events == i + 1 && init.attempted_ops == i + 1 && init.completed_ops == i);
        assert(init.completed_records == records_before[i]);
        terminal(&init, &r, AXD_INIT_FAILED);

        r = fresh_recorder();
        r.cancel_job = &init;
        r.cancel_at = i;
        begin_mixed(&init);
        run(&init, &r);
        assert(init.phase == AXD_INIT_CANCELLED && init.error == AXD_INIT_NO_ERROR);
        assert(r.events == i + 1 && init.attempted_ops == i + 1 && init.completed_ops == i + 1);
        assert(init.completed_records == records_after[i]);
        terminal(&init, &r, AXD_INIT_CANCELLED);

        r = fresh_recorder();
        r.cancel_job = &init;
        r.cancel_at = r.fail_at = i;
        begin_mixed(&init);
        run(&init, &r);
        assert(init.phase == AXD_INIT_FAILED && init.error == AXD_INIT_CALLBACK_ERROR);
        assert(init.completed_ops == i && init.completed_records == records_before[i]);
        terminal(&init, &r, AXD_INIT_FAILED);
    }
    /* Cancellation before the first callback and before an already-armed write. */
    r = fresh_recorder();
    begin_mixed(&init);
    assert(axd_effective_init_cancel(&init) == AXD_INIT_CANCELLED);
    terminal(&init, &r, AXD_INIT_CANCELLED);
    begin_mixed(&init);
    assert(axd_effective_init_step(&init, record, &r) == AXD_INIT_OPERATION);
    assert(axd_effective_init_cancel(&init) == AXD_INIT_CANCELLED);
    assert(init.completed_ops == 1 && init.completed_records == 0);
    terminal(&init, &r, AXD_INIT_CANCELLED);
    assert(axd_effective_init_step(NULL, record, &r) == AXD_INIT_INVALID);
    assert(axd_effective_init_cancel(NULL) == AXD_INIT_INVALID);
    arithmetic_and_boundaries();
    validation();
    puts("effective init: literal mixed trace, context/wrap/truncation, object bounds, validation, every-event failure/cancellation passed");
    return 0;
}
