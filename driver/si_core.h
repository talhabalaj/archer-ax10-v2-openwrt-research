/* SPDX-License-Identifier: MIT */
#ifndef AXD_SI_CORE_H
#define AXD_SI_CORE_H
#include <stdint.h>
/* Actual SI/AI iscoreup and core-cflags wrapper, with explicit selected mapping.
 * No address acquisition, readiness inference or synthetic up result. */
enum axd_si_core_command { AXD_SICO_IS_UP=1, AXD_SICO_FLAGS };
enum axd_si_core_kind { AXD_SICO_READ32=1, AXD_SICO_FULL_BARRIER, AXD_SICO_STORE_BARRIER, AXD_SICO_WRITE32 };
struct axd_si_core_operation { enum axd_si_core_kind kind; uint32_t wrapper, offset, value; };
struct axd_si_core_context { uint32_t interconnect, chip_id, core_id, primary_wrapper; uint8_t wrapper_known; };
enum axd_si_core_phase { AXD_SICO_INVALID=0, AXD_SICO_RUNNING, AXD_SICO_DONE, AXD_SICO_DEPENDENCY, AXD_SICO_FAILED, AXD_SICO_CANCELLED };
enum axd_si_core_error { AXD_SICO_NO_ERROR=0, AXD_SICO_BAD_ARGUMENT, AXD_SICO_IO_ERROR };
struct axd_si_core_job { enum axd_si_core_phase phase; enum axd_si_core_error error; enum axd_si_core_command command; struct axd_si_core_context context; uint32_t mask,value,last_read,result; unsigned stage; uint64_t attempted_ops,completed_ops; };
typedef int (*axd_si_core_io_fn)(void *, const struct axd_si_core_operation *, uint32_t *);
int axd_si_core_begin(struct axd_si_core_job *, enum axd_si_core_command, uint32_t mask, uint32_t value, const struct axd_si_core_context *);
enum axd_si_core_phase axd_si_core_step(struct axd_si_core_job *, axd_si_core_io_fn, void *);
enum axd_si_core_phase axd_si_core_cancel(struct axd_si_core_job *);
#endif
