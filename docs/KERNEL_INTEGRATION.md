# Kernel integration: transport layer

This is the first kernel-side layer of the original driver. It follows the
three-layer boundary chosen earlier: portable components (`driver/`) stay free
of kernel objects; a kernel transport layer (`kernel/`) owns device resources,
serialization and the hardware backend; a mac80211 adapter comes last, once the
offline gates it depends on exist. Nothing here has been loaded on a device.

## What exists

- [axd_kio.c](../kernel/axd_kio.c) executes the portable D11 and SI operation
  streams with kernel primitives. Reads and writes use the relaxed accessors
  (`readw/readl/writew/writel_relaxed`), which add no implicit barriers, so the
  trace's explicit barrier events keep their meaning: the store barrier maps to
  `wmb()` and the full barrier to `mb()`, which on ARMv7 Linux are `DSB ST` and
  `DSB SY` (plus an outer-cache sync only where an outer cache controller
  exists). Delays map to `udelay()` and are capped at 20 ms per event. Every
  access is bounds- and alignment-checked against the owned window; an SI
  access must name the owned wrapper. Out-of-range or unknown events fail
  instead of touching memory.
- [axd_main.c](../kernel/axd_main.c) is a platform driver. Probe maps the D11
  register window (resource 0) and the core wrapper (resource 1), records the
  interrupt number, creates a debugfs `state` file and issues **no device I/O**.
  Register access is disabled unless the module is loaded with `hw_access=1`;
  with it disabled every backend operation fails with `-EPERM` before any
  access. A mutex is reserved for serializing jobs, because indirect register
  pairs (PHY address/data, radio address/data) must never interleave.
- [axd_ring.c](../kernel/axd_ring.c) owns DMA64 descriptor-ring memory:
  coherent allocation of `count*16` bytes (power-of-two count; the DMA API's
  page-order alignment keeps the ring inside its pointer-mask window), a
  32-bit DMA mask set at probe (descriptors carry a 32-bit low address with no
  carry), validation through the portable `axd_dma64_ring_check`, and
  descriptor encode/decode through the existing codec ordered with
  `dma_wmb()`/`dma_rmb()`. It programs no engine and maps no packet buffers;
  rings are not allocated until a start path exists.
- The interrupt is deliberately not requested: interrupt status and
  acknowledge handling is not implemented, and an unacknowledged level
  interrupt could storm.
- [compat/](../kernel/compat) maps the components' `stdint.h`, `stddef.h` and
  `limits.h` use to the kernel's own types; the components call no library
  functions.

## Provisional device-tree binding

There is no upstream binding for this core. The driver matches a local,
provisional compatible so it can be built and reviewed:

```dts
wlan@<d11-base> {
	compatible = "axd,bcm63178-d11";
	reg = <D11-base D11-size>, <wrapper-base wrapper-size>;
	interrupts = <...>;	/* optional; not requested yet */
};
```

The exact-board addresses, sizes and interrupt are **not established** and
must come from verified board resources, not from the chip name. Whether the
final driver should bind through a platform node or through the SoC's
backplane enumeration (as b43 does with BCMA) is still open.

## Build verification

`sh scripts/build_kernel_module.sh [new-dir-outside-checkout]` cross-builds the
module in a container against Linux 6.12.114 (tarball SHA-256 pinned to the
kernel.org published value), `ARCH=arm multi_v7_defconfig`, with `W=1`. A
`vmlinux` build supplies the kernel's real exported-symbol list, so modpost
checks every import. All portable components are compiled and linked into
`axd_wl.ko`. The current build has **zero warnings**; the only division helpers
used are the kernel-exported 32-bit `__aeabi_uidiv`/`__aeabi_uidivmod`. The
checkpoint verifier runs it with `--kernel-build` and fails on any warning.
This is compile/link evidence only.

### Kernel-stack defect found and fixed

The first kernel build reported 23 functions with stack frames over 1 KB (up
to 23 KB in `axd_phy_scheduler_begin`); ARM32 kernel stacks are 8 KB. Causes:
each `begin` built a job-sized zero template on the stack, several snapshotted
large context structs for alias safety, and the PSM policy kept a result with
a full diagnostic trace on the stack. Fixes: zero templates are `static`;
`core_reset` and the PHY scheduler now copy distinct inputs straight into the
job, keep inputs that already are the job's own member, zero the rest
([job_reset.h](../driver/job_reset.h)) and reject inputs that partially overlap
the job; the policy result lives in the PSM job. No frame now exceeds 1 KB.
GCC's misleading-indentation warnings (compact one-line style) were removed by
whitespace-only line splits, verified byte-identical at the object level.

## Not established

Loading on the exact board, correct resources, interrupt handling, DMA rings
and ownership, firmware delivery, the PHY/radio bring-up sequence (the offline
PHY vector is still incomplete), RX/TX, mac80211 registration and any radio
operation. No hardware access is authorized in this project yet.
