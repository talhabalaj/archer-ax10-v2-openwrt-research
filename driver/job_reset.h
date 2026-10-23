/* SPDX-License-Identifier: MIT */
#ifndef AXD_JOB_RESET_H
#define AXD_JOB_RESET_H
#include <stddef.h>
#include <stdint.h>
/* Kernel-stack-safe job reset for begin functions with large inputs.
 * Instead of snapshotting inputs into stack copies, callers copy distinct
 * inputs straight into the job (or keep an input that already is the job's
 * own member), then zero every other byte. Inputs that partially overlap
 * the job are rejected by axd_job_overlaps(). */
static inline int axd_job_overlaps(const void *job, size_t job_size, const void *input,
                                   size_t input_size, const void *member)
{
    uintptr_t j = (uintptr_t)job, i = (uintptr_t)input;
    if (!input || input == member) return 0;
    return i < j + job_size && j < i + input_size;
}
static inline void axd_job_zero_except(void *job, size_t size, size_t keep1, size_t len1,
                                       size_t keep2, size_t len2)
{
    unsigned char *b = job;
    size_t k;
    for (k = 0; k < size; k++)
        if (!(k >= keep1 && k < keep1 + len1) && !(k >= keep2 && k < keep2 + len2)) b[k] = 0;
}
#endif
