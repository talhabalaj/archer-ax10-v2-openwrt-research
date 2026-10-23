/* SPDX-License-Identifier: MIT */
#include "rx_packets.h"

enum stage {
    ENTRY, RUN_LIST, FIRST_RETURN, OFFSET, PACKET, READ_DATA, READ_LENGTH,
    TEST_LENGTH, DELAY, DELAY_RETURN, INITIAL_TRIM,
    TRIM_NONLINEAR, TRIM_SELECT, TRIM_DATA, TRIM_LENGTH, TRIM_TAIL,
    TRIM_OS, TRIM_FINISH, AFTER_INITIAL, FLAGS_FIRST, HEADER_DATA, HEADER_READ,
    HEADER_TEST, NEXT_FRAGMENT, LINK_FRAGMENT, MORE_RETURN, WRITE_FRAGMENT,
    PREPARE_FRAGMENT, FRAGMENT_UNLINK, FRAGMENT_ADVANCE, FRAGMENT_JOIN,
    READ_FINAL_NEXT, WRITE_CURRENT_NEXT, CLEAR_LAST_NEXT, FLAGS_LAST,
    ACCEPT_TAIL, ACCEPT_COUNT, READ_ACCEPT_NEXT, SET_CURRENT,
    DROP_READ_NEXT, DROP_LINK, DROP_WRITE_LINK, DROP_CLEAR, DROP_NEXT,
    DROP_OS, DROP_COUNT, STATUS, RUN_STATUS, RESULT_HEAD, RESULT_TAIL, FINISH
};
struct relay { struct axd_rx_packets *job; axd_rx_packets_io_fn io; void *opaque; };
static void fail(struct axd_rx_packets *j, enum axd_rx_packets_error error)
{ j->phase = AXD_RP_FAILED; j->error = error; }
static void dependency(struct axd_rx_packets *j, enum axd_rx_packets_dependency reason)
{ j->phase = AXD_RP_DEPENDENCY; j->dependency = reason; }
static int known(struct axd_rx_packets *j, enum axd_rx_packets_metadata field,
                 struct axd_wake_u32 input)
{
    j->last_metadata = field; j->metadata_accesses++;
    if (input.known == 1) return 1;
    if (input.known == 0) dependency(j, AXD_RP_METADATA);
    else fail(j, AXD_RP_BAD_ARGUMENT);
    return 0;
}
static int perform(struct relay *r, const struct axd_rx_packets_operation *op, struct axd_rx_packets_reply *reply)
{
    if (r->io == NULL) { fail(r->job, AXD_RP_BAD_ARGUMENT); return -1; }
    r->job->attempted_ops++;
    if (r->io(r->opaque, op, reply) != 0) { fail(r->job, AXD_RP_IO_ERROR); return -1; }
    r->job->completed_ops++; return 0;
}
static int list_io(void *opaque, const struct axd_rx_list_operation *op, uint32_t *reply)
{
    struct relay *r = opaque; struct axd_rx_packets_operation converted = {0};
    struct axd_rx_packets_reply out={0};
    r->job->context.list = r->job->list_job.context;
    converted.kind = AXD_RP_LIST; converted.list = *op;
    if(perform(r,&converted,&out)!=0)return -1;
    *reply=out.value;return 0;
}
static int status_io(void *opaque, const struct axd_rx_status_operation *op, struct axd_rx_status_reply *reply)
{
    struct relay *r = opaque; struct axd_rx_packets_operation converted = {0};
    struct axd_rx_packets_reply out={0};
    r->job->context.status = r->job->status_job.context;
    r->job->context.status_output = r->job->status_job.output;
    converted.kind = AXD_RP_STATUS; converted.status = *op;
    if(perform(r,&converted,&out)!=0)return -1;
    *reply=out.status;return 0;
}
static void list_begin(struct axd_rx_packets *j, uint32_t budget, unsigned resume)
{
    if (axd_dma_rx_list_begin(&j->list_job, budget, 0, &j->context.list) != 0)
        fail(j, AXD_RP_CHILD_ERROR);
    else { j->after_list = resume; j->stage = RUN_LIST; }
}
static void list_step(struct axd_rx_packets *j, struct relay *r)
{
    enum axd_rx_list_phase p = axd_dma_rx_list_step(&j->list_job, list_io, r);
    j->context.list = j->list_job.context;
    if (j->phase != AXD_RP_RUNNING) return;
    if (p == AXD_RX_LIST_DONE) j->stage = j->after_list;
    else if (p == AXD_RX_LIST_DEPENDENCY) dependency(j, AXD_RP_LIST_DEPENDENCY);
    else if (p == AXD_RX_LIST_CANCELLED) j->phase = AXD_RP_CANCELLED;
    else if (p != AXD_RX_LIST_RUNNING) fail(j, AXD_RP_CHILD_ERROR);
}
static int packet(struct axd_rx_packets *j, struct axd_rx_packets_operation *op,
                  uint32_t label, enum axd_rx_packet_field field, unsigned write, uint32_t value)
{
    if (!j->context.packets_owned) { dependency(j, AXD_RP_PACKET_OWNER); return 0; }
    if (label == 0) { fail(j, AXD_RP_BINDING); return 0; }
    op->kind = write ? AXD_RP_PACKET_WRITE32 : AXD_RP_PACKET_READ32;
    op->packet = label; op->field = field; op->value = value; return 1;
}
static int data_read(struct axd_rx_packets *j, struct axd_rx_packets_operation *op, unsigned word)
{
    if (!j->context.data_owned) { dependency(j, AXD_RP_DATA_OWNER); return 0; }
    if (j->data == 0) { fail(j, AXD_RP_BINDING); return 0; }
    op->kind = word ? AXD_RP_DATA_READ32 : AXD_RP_DATA_READ16;
    op->data = j->data; return 1;
}
static int os(struct axd_rx_packets *j, struct axd_rx_packets_operation *op,
              enum axd_rx_packets_kind kind, uint32_t label, uint32_t value)
{
    if (!j->context.os_owned) { dependency(j, AXD_RP_OS_OWNER); return 0; }
    if (kind == AXD_RP_FREE && !known(j, AXD_RP_OS_CONTEXT, j->context.os_context)) return 0;
    op->kind = kind; op->packet = label; op->value = value;
    if (kind == AXD_RP_FREE) op->os_context = j->context.os_context.value;
    return 1;
}
int axd_rx_packets_begin(struct axd_rx_packets *j, const struct axd_rx_packets_request *request,
                         const struct axd_rx_packets_context *context)
{
    static const struct axd_rx_packets empty={0};
    struct axd_rx_packets_request r; struct axd_rx_packets_context c;
    if (j == NULL) return -1;
    if (request != NULL) { r = *request; request = &r; }
    if (context != NULL) { c = *context; context = &c; }
    *j = empty; fail(j, AXD_RP_BAD_ARGUMENT);
    if (request == NULL || context == NULL || c.packets_owned > 1 || c.data_owned > 1 || c.os_owned > 1)
        return -1;
    j->request = r; j->context = c; j->output = r.output;
    if (c.work_limit == 0 || c.work_limit > 65535) { fail(j, AXD_RP_BOUNDS); return -1; }
    j->phase = AXD_RP_RUNNING; j->error = AXD_RP_NO_ERROR; return 0;
}
enum axd_rx_packets_phase axd_rx_packets_step(struct axd_rx_packets *j,
                                               axd_rx_packets_io_fn io, void *opaque)
{
    struct relay r = {j, io, opaque}; struct axd_rx_packets_operation op = {0};
    uint32_t reply;struct axd_rx_packets_reply out={0};unsigned emit = 0, before;
    if (j == NULL) return AXD_RP_INVALID;
    if (j->phase != AXD_RP_RUNNING) return j->phase;
    switch (j->stage) {
    case ENTRY: list_begin(j, j->request.budget, FIRST_RETURN); break;
    case RUN_LIST: list_step(j, &r); break;
    case FIRST_RETURN: j->head = j->current = j->list_job.return_value; j->stage = OFFSET; break;
    case OFFSET:
        if (known(j, AXD_RP_RECEIVE_OFFSET, j->context.receive_offset))
            j->stage = j->current == 0 ? STATUS : PACKET;
        break;
    case PACKET:
        if (j->visited_packets == j->context.work_limit) { fail(j, AXD_RP_BOUNDS); break; }
        j->visited_packets++; j->poll_remaining = 200; j->stage = READ_DATA; break;
    case READ_DATA: emit = packet(j, &op, j->current, AXD_RP_DATA, 0, 0); break;
    case READ_LENGTH: emit = data_read(j, &op, 0); break;
    case TEST_LENGTH: j->stage = j->length_sample != 0 ? INITIAL_TRIM : DELAY; break;
    case DELAY: emit = os(j, &op, AXD_RP_DELAY_US, 0, 1); break;
    case DELAY_RETURN: j->stage = --j->poll_remaining == 0 ? DROP_READ_NEXT : READ_LENGTH; break;
    case INITIAL_TRIM:
        if (!known(j, AXD_RP_BUFFER_SIZE, j->context.buffer_size)) break;
        if (j->context.buffer_size.value == 0 || j->context.buffer_size.value > UINT16_MAX ||
            j->context.receive_offset.value > j->context.buffer_size.value) { fail(j, AXD_RP_BOUNDS); break; }
        j->trim_length = j->context.receive_offset.value + j->length_sample;
        if (j->trim_length > j->context.buffer_size.value) j->trim_length = j->context.buffer_size.value;
        j->trim_packet = j->current; j->after_trim = AFTER_INITIAL; j->stage = TRIM_NONLINEAR; break;
    case TRIM_NONLINEAR: emit = packet(j, &op, j->trim_packet, AXD_RP_NONLINEAR, 0, 0); break;
    case TRIM_SELECT: j->stage = j->next != 0 ? TRIM_OS : TRIM_DATA; break;
    case TRIM_DATA: emit = packet(j, &op, j->trim_packet, AXD_RP_DATA, 0, 0); break;
    case TRIM_LENGTH: emit = packet(j, &op, j->trim_packet, AXD_RP_LENGTH, 1, j->trim_length); break;
    case TRIM_TAIL:
        if (j->data > UINT32_MAX - j->trim_length) { fail(j, AXD_RP_BOUNDS); break; }
        emit = packet(j, &op, j->trim_packet, AXD_RP_TAIL, 1, j->data + j->trim_length); break;
    case TRIM_OS: emit = os(j, &op, AXD_RP_TRIM, j->trim_packet, j->trim_length); break;
    case TRIM_FINISH: j->stage = j->after_trim; break;
    case AFTER_INITIAL:
        j->remaining = j->context.receive_offset.value + j->length_sample - j->context.buffer_size.value;
        j->stage = j->remaining == 0 || j->remaining >= 0x80000000u ? ACCEPT_TAIL : FLAGS_FIRST; break;
    case FLAGS_FIRST:
        if (known(j, AXD_RP_FLAGS, j->context.flags))
            j->stage = (j->context.flags.value & 0x40u) != 0 ? DROP_READ_NEXT : HEADER_DATA;
        break;
    case HEADER_DATA: emit = packet(j, &op, j->current, AXD_RP_DATA, 0, 0); break;
    case HEADER_READ: emit = data_read(j, &op, 1); break;
    case HEADER_TEST:
        j->fragments_left = (j->header_sample >> 24) & 15u; j->fragment_tail = j->current;
        j->stage = j->fragments_left == 0 ? FLAGS_LAST : NEXT_FRAGMENT; break;
    case NEXT_FRAGMENT: emit = packet(j, &op, j->fragment_tail, AXD_RP_NEXT, 0, 0); break;
    case LINK_FRAGMENT:
        if (j->next == 0) list_begin(j, 1, MORE_RETURN);
        else j->stage = WRITE_FRAGMENT;
        break;
    case MORE_RETURN: j->next = j->list_job.return_value; j->stage = WRITE_FRAGMENT; break;
    case WRITE_FRAGMENT: emit = packet(j, &op, j->fragment_tail, AXD_RP_FRAGMENT, 1, j->next); break;
    case PREPARE_FRAGMENT:
        if (j->next == 0) { j->stage = FRAGMENT_JOIN; break; }
        if (j->remaining >= 0x80000000u) { fail(j, AXD_RP_BOUNDS); break; }
        j->trim_length = j->remaining < j->context.buffer_size.value ? j->remaining : j->context.buffer_size.value;
        j->trim_packet = j->next; j->after_trim = FRAGMENT_UNLINK; j->stage = TRIM_NONLINEAR; break;
    case FRAGMENT_UNLINK: emit = packet(j, &op, j->fragment_tail, AXD_RP_NEXT, 1, 0); break;
    case FRAGMENT_ADVANCE:
        j->fragments_left--; j->remaining -= j->context.buffer_size.value;
        j->fragment_tail = j->trim_packet;
        j->stage = j->fragments_left == 0 ? FRAGMENT_JOIN : NEXT_FRAGMENT; break;
    case FRAGMENT_JOIN: j->stage = j->current == j->fragment_tail ? FLAGS_LAST : READ_FINAL_NEXT; break;
    case READ_FINAL_NEXT: emit = packet(j, &op, j->fragment_tail, AXD_RP_NEXT, 0, 0); break;
    case WRITE_CURRENT_NEXT: emit = packet(j, &op, j->current, AXD_RP_NEXT, 1, j->next); break;
    case CLEAR_LAST_NEXT: emit = packet(j, &op, j->fragment_tail, AXD_RP_NEXT, 1, 0); break;
    case FLAGS_LAST:
        if (known(j, AXD_RP_FLAGS, j->context.flags))
            j->stage = (j->context.flags.value & 4u) != 0 ? ACCEPT_TAIL : DROP_READ_NEXT;
        break;
    case ACCEPT_TAIL: j->accepted_tail = j->current; j->stage = ACCEPT_COUNT; break;
    case ACCEPT_COUNT: j->output.count++; j->stage = READ_ACCEPT_NEXT; break;
    case READ_ACCEPT_NEXT: emit = packet(j, &op, j->current, AXD_RP_NEXT, 0, 0); break;
    case SET_CURRENT: j->current = j->next; j->stage = j->current == 0 ? STATUS : PACKET; break;
    case DROP_READ_NEXT: emit = packet(j, &op, j->current, AXD_RP_NEXT, 0, 0); break;
    case DROP_LINK: j->stage = j->accepted_tail != 0 ? DROP_WRITE_LINK : DROP_CLEAR; break;
    case DROP_WRITE_LINK: emit = packet(j, &op, j->accepted_tail, AXD_RP_NEXT, 1, j->next); break;
    case DROP_CLEAR: emit = packet(j, &op, j->current, AXD_RP_NEXT, 1, 0); break;
    case DROP_NEXT:
        j->free_packet = j->current; j->current = j->next;
        if (j->accepted_tail == 0) j->head = j->next;
        j->stage = DROP_OS; break;
    case DROP_OS: emit = os(j, &op, AXD_RP_FREE, j->free_packet, 0); break;
    case DROP_COUNT:
        j->context.error_count++; j->stage = j->current == 0 ? STATUS : PACKET; break;
    case STATUS:
        if (!known(j, AXD_RP_STATUS_CONTEXT, j->context.status_context)) break;
        if (j->request.status_output == 0 || j->context.status_context.value == 0) {
            j->stage = RESULT_HEAD; break;
        }
        if (j->context.status_binding.known == 0 || j->context.status_output_known == 0) {
            dependency(j, AXD_RP_STATUS_PATH); break;
        }
        if (j->context.status_binding.known != 1 || j->context.status_output_known != 1) {
            fail(j, AXD_RP_BAD_ARGUMENT); break;
        }
        if (j->context.status_binding.value != j->context.status_context.value) {
            fail(j, AXD_RP_BINDING); break;
        }
        if (axd_rx_status_begin(&j->status_job, &j->context.status_output, &j->context.status) != 0)
            fail(j, AXD_RP_CHILD_ERROR);
        else j->stage = RUN_STATUS;
        break;
    case RUN_STATUS: {
        enum axd_rx_status_phase p = axd_rx_status_step(&j->status_job, status_io, &r);
        j->context.status = j->status_job.context;
        j->context.status_output = j->status_job.output;
        if (j->phase != AXD_RP_RUNNING) break;
        if (p == AXD_RS_TRACE_COMPLETE) j->stage = RESULT_HEAD;
        else if (p == AXD_RS_DEPENDENCY) dependency(j, AXD_RP_STATUS_PATH);
        else if (p == AXD_RS_CANCELLED) j->phase = AXD_RP_CANCELLED;
        else if (p != AXD_RS_RUNNING) fail(j, AXD_RP_CHILD_ERROR);
        break;
    }
    case RESULT_HEAD: j->output.head = j->head; j->result_stores |= 1u; j->stage = RESULT_TAIL; break;
    case RESULT_TAIL: j->output.tail = j->accepted_tail; j->result_stores |= 2u; j->stage = FINISH; break;
    case FINISH: j->phase = AXD_RP_TRACE_COMPLETE; break;
    default: fail(j, AXD_RP_BAD_ARGUMENT); break;
    }
    if (!emit) return j->phase;
    before = j->stage;
    if (perform(&r, &op, &out) != 0) return j->phase;
    reply=out.value;
    switch (before) {
    case READ_DATA: case TRIM_DATA: case HEADER_DATA: j->data = reply; break;
    case READ_LENGTH: j->length_sample = (uint16_t)reply; j->samples++; break;
    case DELAY: j->delay_count++; break;
    case TRIM_NONLINEAR: case NEXT_FRAGMENT: case READ_FINAL_NEXT:
    case READ_ACCEPT_NEXT: case DROP_READ_NEXT: j->next = reply; break;
    case HEADER_READ: j->header_sample = reply; break;
    case TRIM_OS: j->trimmed_packets++; break;
    case DROP_OS: j->freed_packets++; break;
    default: break;
    }
    if (before == TRIM_TAIL || before == TRIM_OS) j->stage = TRIM_FINISH;
    else j->stage = before + 1;
    return j->phase;
}
enum axd_rx_packets_phase axd_rx_packets_cancel(struct axd_rx_packets *j)
{
    if (j == NULL) return AXD_RP_INVALID;
    if (j->phase == AXD_RP_RUNNING) {
        j->phase = AXD_RP_CANCELLED;
        if (j->stage == RUN_LIST) axd_dma_rx_list_cancel(&j->list_job);
        else if (j->stage == RUN_STATUS) axd_rx_status_cancel(&j->status_job);
    }
    return j->phase;
}
