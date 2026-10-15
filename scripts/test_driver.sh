#!/bin/sh
# SPDX-License-Identifier: MIT
# Run public synthetic checks; optionally add private stock-input comparison.
set -eu
cd "$(dirname "$0")/.."
axd_build_dir=$(mktemp -d)
trap 'rm -rf "$axd_build_dir"' EXIT HUP INT TERM
for axd_test in init_table effective_init ucode_upload status_poll psm_start ai_reset psm_control dma_reset; do
    cc -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
        driver/init_table.c driver/effective_init.c driver/ucode_select.c driver/ucode_upload.c driver/status_poll.c \
        driver/psm_start.c driver/ai_reset.c driver/psm_control.c driver/dma_reset.c \
        "tests/test_$axd_test.c" -o "$axd_build_dir/test_$axd_test"
    "$axd_build_dir/test_$axd_test"
done
if [ "$#" -gt 0 ]; then
    case $(uname -s) in
        Darwin) axd_library="$axd_build_dir/libaxd_offline.dylib"; axd_shared_flag=-dynamiclib ;;
        *) axd_library="$axd_build_dir/libaxd_offline.so"; axd_shared_flag=-shared ;;
    esac
    cc -std=c11 -Wall -Wextra -Werror -pedantic -fPIC "$axd_shared_flag" \
        driver/init_table.c driver/effective_init.c driver/ucode_select.c driver/ucode_upload.c driver/status_poll.c \
        driver/psm_start.c driver/ai_reset.c driver/psm_control.c driver/dma_reset.c \
        -o "$axd_library"
    python3 tests/check_stock_firmware.py "$1" "$axd_library"
fi
