#!/usr/bin/env python3
"""Inspect the official AX10 V2 GPL tar without extracting or executing files.

Usage: python3 inventory_gpl.py /path/to/ax10v2_GPL_20240317.tar.gz
Writes a deterministic, public-safe JSON inventory to stdout. No target configs,
calibration data, credentials, binary contents, or local path are emitted.
"""
import hashlib
import json
import pathlib
import sys
import tarfile

ROOT = "ax10v2_GPL/"
BSP = ROOT + "bcm675x/bcm963xx_router/"
WL = BSP + "bcmdrivers/broadcom/net/wl/impl59/"
WATCH = {
    ROOT + "Readme.txt",
    ROOT + "Iplatform/build/Makefile",
    ROOT + "Iplatform/build/product_configs/ax10v2/basic.config",
    ROOT + "Iplatform/build/product_configs/ax10v2/kernel.config",
    ROOT + "Iplatform/openwrt/include/version.mk",
    ROOT + "bcm675x/toolchain/crosstools-arm-gcc-5.5-linux-4.1-glibc-2.26-binutils-2.28.1.Rel1.10.tar.bz2",
    BSP + "version.make",
    BSP + "kernel/linux-4.1/Makefile",
    BSP + "kernel/dts/63178/963178.dts",
    BSP + "kernel/linux-4.1/arch/arm/mach-bcm963xx/board_963xx.c",
    BSP + "shared/opensource/boardparms/bcm963xx/boardparms_63178.c",
    BSP + "bcmdrivers/opensource/bus/pci/host/impl1/arm/pcie-bcm963xx.c",
    BSP + "bcmdrivers/opensource/bus/pci/host/impl1/arm/pcie-vcore.c",
    BSP + "bcmdrivers/opensource/net/enet/impl7/enet.c",
    BSP + "bcmdrivers/opensource/net/enet/impl7/sf2.c",
    BSP + "bcmdrivers/opensource/net/enet/impl7/archer.c",
    BSP + "bcmdrivers/broadcom/char/archer/impl1/archerTP6750.o_save",
    BSP + "bcmdrivers/broadcom/char/pktflow/impl1/pktflowTP6750.o_save",
    BSP + "bcmdrivers/broadcom/char/bcmlibs/impl1/bcmlibsTP6750.o_save",
    BSP + "bcmdrivers/broadcom/char/wlcsm_ext/impl1/wlcsmTP6750.o_save",
    WL + "main/src/wl/sys/wlc.c",
    WL + "main/src/wl/sys/wlc_bmac.c",
    WL + "main/src/wl/sys/wl_linux.c",
    WL + "main/src/wl/sys/wl_linux.h",
    WL + "main/src/wl/linux/prebuilt/wl.o",
    WL + "main/src/wl/linux/prebuilt/wl.ko",
    WL + "main/src/hnd/linux/prebuilt/hnd.o",
    WL + "main/src/include/epivers.h",
    WL + "main/components/router/hnd_wl/wl_generic.mk",
    WL + "main/src/shared/linux_osl.c",
    WL + "main/src/shared/siutils.c",
    WL + "main/src/shared/hndpmu.c",
    WL + "sys/src/wl/sys/wl_cfg80211.c",
}


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    archive = pathlib.Path(sys.argv[1])
    with archive.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    entries = {}
    member_count = 0
    regular_bytes = 0
    with tarfile.open(archive, "r|gz") as stream:
        for member in stream:
            member_count += 1
            if member.isfile():
                regular_bytes += member.size
            if member.name not in WATCH:
                continue
            entry = {"size": member.size, "type": member.type.decode("ascii")}
            if member.isfile():
                with stream.extractfile(member) as contents:
                    entry["sha256"] = hashlib.file_digest(contents, "sha256").hexdigest()
            entries[member.name] = entry
    print(json.dumps({
        "archive": {
            "filename": "ax10v2_GPL_20240317.tar.gz",
            "url": "https://static.tp-link.com/upload/gpl-code/2025/202503/20250317/ax10v2_GPL_20240317.tar.gz",
            "size": archive.stat().st_size,
            "sha256": digest,
            "members": member_count,
            "sum_regular_member_bytes": regular_bytes,
        },
        "selected_members": dict(sorted(entries.items())),
        "absent_selected_members": sorted(WATCH - entries.keys()),
    }, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
