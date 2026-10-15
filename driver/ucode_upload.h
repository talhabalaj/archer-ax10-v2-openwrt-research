/* SPDX-License-Identifier: MIT */
#ifndef AXD_UCODE_UPLOAD_H
#define AXD_UCODE_UPLOAD_H
#include <stddef.h>
#include <stdint.h>

/* Offline trace model only. No MMIO backend, PSM reset/start, calibration,
 * hardware readiness check, or firmware authenticity check is provided.
 */
enum axd_upload_phase {
    AXD_UPLOAD_INVALID = 0,
    AXD_UPLOAD_SELECT_BARRIER,
    AXD_UPLOAD_SELECT_WRITE,
    AXD_UPLOAD_SELECT_READBACK,
    AXD_UPLOAD_SELECT_FULL_BARRIER,
    AXD_UPLOAD_DATA_BARRIER,
    AXD_UPLOAD_DATA_WRITE,
    AXD_UPLOAD_TRANSFERRED,
    AXD_UPLOAD_FAILED,
    AXD_UPLOAD_CANCELLED
};

enum axd_upload_error {
    AXD_UPLOAD_NO_ERROR = 0,
    AXD_UPLOAD_BAD_ARGUMENT,
    AXD_UPLOAD_UNSUPPORTED_CONTEXT,
    AXD_UPLOAD_CALLBACK_ERROR
};

enum axd_barrier_kind { AXD_BARRIER_STORE = 1, AXD_BARRIER_FULL = 2 };

struct axd_upload_io {
    void *context;
    int (*write32)(void *context, uint16_t reg, uint32_t value);
    int (*read32)(void *context, uint16_t reg, uint32_t *value);
    int (*barrier)(void *context, enum axd_barrier_kind kind);
};

struct axd_ucode_upload {
    enum axd_upload_phase phase;
    enum axd_upload_error error;
    const uint8_t *image[2];
    size_t length[2];
    size_t word_index;
    size_t words[2];
    unsigned int bank;
    uint64_t attempted_ops, completed_ops;
};

/* Validates BOTH bank sizes/core/minor before callbacks. Images must remain
 * immutable and alive through the transfer. Only the explicitly supplied
 * ucode_chunk == 0 case is supported; unknown continuation state is rejected.
 * The caller is responsible for input provenance. Size validation is not
 * firmware authentication. The backend must remain stable during a job.
 */
int axd_upload_begin(struct axd_ucode_upload *upload,
                      uint16_t core, uint16_t minor, uint32_t boardflags,
                      uint32_t ucode_chunk,
                      const uint8_t *primary, size_t primary_length,
                      const uint8_t *extended, size_t extended_length);

/* Emits exactly one bus/barrier callback per active step. Terminal calls emit
 * none. TRANSFERRED means both byte streams ended, never firmware readiness.
 * Failure/cancellation does not undo side effects or perform reset/cleanup.
 * completed_ops counts callbacks returning success, not hardware commitment.
 * Single-threaded API: callbacks may cancel, but must not begin/step a job or
 * mutate its fields. Cancellation during a successful callback is preserved;
 * callback failure takes precedence if cancellation and failure coincide.
 */
enum axd_upload_phase axd_upload_step(struct axd_ucode_upload *upload,
                                      const struct axd_upload_io *io);
enum axd_upload_phase axd_upload_cancel(struct axd_ucode_upload *upload);
#endif
