#!/bin/sh
# SPDX-License-Identifier: MIT
# Cross-build the kernel module against a pinned Linux tree in a container.
# Compile/link (modpost) evidence only: no module is loaded anywhere.
# Usage: sh scripts/build_kernel_module.sh [NEW-output-dir-outside-checkout]
set -eu
cd "$(dirname "$0")/.."
root=$(pwd)
linux_version=6.12.114
linux_sha256=7a8589912bd7a8adb07fed074283652cf482a35facef8954554654f0154b4af3
out=${1:-$(mktemp -d "${TMPDIR:-/tmp}/axd-kbuild-XXXXXX")}
case "$out" in "$root"|"$root"/*) echo "output must be outside the checkout" >&2; exit 2;; esac
mkdir -p "$out/src/portable"
cp kernel/Kbuild kernel/*.c kernel/*.h "$out/src/"
cp -R kernel/compat "$out/src/"
cp driver/*.c driver/*.h "$out/src/portable/"
docker build -q -t axd-kbuild - >/dev/null <<'DOCKER'
FROM debian:bookworm
RUN apt-get update && apt-get install -y --no-install-recommends gcc-arm-linux-gnueabihf gcc libc6-dev make bc bison flex libssl-dev libelf-dev xz-utils curl ca-certificates python3 perl kmod && rm -rf /var/lib/apt/lists/*
DOCKER
set +e
docker run --rm -v axd-kbuild-cache:/cache -v "$out/src:/src" -e V="$linux_version" -e SHA="$linux_sha256" axd-kbuild sh -euc '
  export ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-
  cd /cache
  if [ ! -f linux-$V/.axd-ready ]; then
    rm -rf linux-$V linux-$V.tar.xz
    curl -fsSL --retry 5 --retry-delay 3 --retry-all-errors -o linux-$V.tar.xz https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$V.tar.xz
    echo "$SHA  linux-$V.tar.xz" | sha256sum -c -
    tar xf linux-$V.tar.xz && rm linux-$V.tar.xz
    make -C linux-$V -s multi_v7_defconfig
    make -C linux-$V -s -j"$(nproc)" modules_prepare
    touch linux-$V/.axd-ready
  fi
  # Exported-symbol list of the built kernel, so modpost checks every import.
  if [ ! -f linux-$V/.axd-symvers ]; then
    make -C linux-$V -s -j"$(nproc)" vmlinux
    cp linux-$V/vmlinux.symvers linux-$V/Module.symvers
    touch linux-$V/.axd-symvers
  fi
  make -C linux-$V -s M=/src W=1 modules
  modinfo /src/axd_wl.ko | grep -E "^(license|description|parm|alias|vermagic):"
  arm-linux-gnueabihf-nm -u /src/axd_wl.ko | sort
' > "$out/build.log" 2>&1
status=$?
set -e
cat "$out/build.log"
echo "output: $out"
exit $status
