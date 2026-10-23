#!/bin/sh
# SPDX-License-Identifier: MIT
# Run public synthetic checks; optionally add private stock-input comparison.
set -eu
cd "$(dirname "$0")/.."
axd_build_dir=$(mktemp -d)
trap 'rm -rf "$axd_build_dir"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -pedantic -fsyntax-only tests/check_driver_headers.c
printf '%s\n' 'Public driver headers compile together; numeric AI transport/state values preserved.'
for axd_test in init_table effective_init ucode_upload status_poll psm_start ai_reset psm_control dma_reset clock_control phy_register chipcommon_clock phy_table phy_tssi phy_clock psm_override phy_guard idle_tssi wake_policy bf_clean phy_reset si_select si_clock wl_clock mctrl si_core core_reset_low core_reset dma_descriptor object_memory init_io phy_table_write smc si_pmu_gpio phy_init dma_tx_post dma_rx_take radio_identity radio_control phy_scheduler dma_rx_list dma_rx_status_list rx_packets rx_status si_switch offload_status dma_rx_fill radio_power phy_channel_init radio_regtbl phy_table_read phy_noise dma_rx_status_fill bmac_rx_fill phy_chanspec hnd_math_fp; do
    cc -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
        driver/init_table.c driver/effective_init.c driver/ucode_select.c driver/ucode_upload.c driver/status_poll.c \
        driver/psm_start.c driver/ai_reset.c driver/psm_control.c driver/dma_reset.c driver/clock_control.c driver/phy_register.c driver/chipcommon_clock.c driver/phy_table.c driver/phy_tssi.c driver/phy_clock.c \
        driver/phy_guard.c driver/idle_tssi.c driver/wake_policy.c \
        driver/bf_clean.c driver/phy_reset.c \
        driver/si_select.c driver/si_clock.c driver/wl_clock.c \
        driver/mctrl.c driver/si_core.c driver/core_reset_low_ai.c driver/core_reset_low.c driver/core_reset.c \
        driver/dma_descriptor.c driver/object_memory.c driver/init_io.c driver/phy_table_write.c driver/smc.c \
        driver/si_pmu_gpio.c driver/phy_init.c driver/dma_tx_post.c driver/dma_rx_take.c \
        driver/radio_identity.c \
        driver/radio_control.c driver/phy_scheduler.c driver/dma_rx_list.c driver/dma_rx_status_list.c \
        driver/rx_packets.c driver/rx_status.c driver/si_switch.c driver/offload_status.c \
        driver/dma_rx_fill.c driver/radio_power.c driver/phy_channel_init.c \
        driver/radio_regtbl.c driver/phy_table_read.c driver/phy_noise.c \
        driver/dma_rx_status_fill.c driver/bmac_rx_fill.c driver/phy_chanspec.c driver/hnd_math_fp.c \
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
        driver/psm_start.c driver/ai_reset.c driver/psm_control.c driver/dma_reset.c driver/clock_control.c driver/phy_register.c driver/chipcommon_clock.c driver/phy_table.c driver/phy_tssi.c driver/phy_clock.c \
        driver/phy_guard.c driver/idle_tssi.c driver/wake_policy.c \
        driver/bf_clean.c driver/phy_reset.c \
        driver/si_select.c driver/si_clock.c driver/wl_clock.c \
        driver/mctrl.c driver/si_core.c driver/core_reset_low_ai.c driver/core_reset_low.c driver/core_reset.c \
        driver/dma_descriptor.c driver/object_memory.c driver/init_io.c driver/phy_table_write.c driver/smc.c \
        driver/si_pmu_gpio.c driver/phy_init.c driver/dma_tx_post.c driver/dma_rx_take.c \
        driver/radio_identity.c \
        driver/radio_control.c driver/phy_scheduler.c driver/dma_rx_list.c driver/dma_rx_status_list.c \
        driver/rx_packets.c driver/rx_status.c driver/si_switch.c driver/offload_status.c \
        driver/dma_rx_fill.c driver/radio_power.c driver/phy_channel_init.c \
        driver/radio_regtbl.c driver/phy_table_read.c driver/phy_noise.c \
        driver/dma_rx_status_fill.c driver/bmac_rx_fill.c driver/phy_chanspec.c driver/hnd_math_fp.c \
        tests/smc_stock_adapter.c \
        -o "$axd_library"
    python3 tests/check_stock_firmware.py "$1" "$axd_library"
    python3 tests/check_smc_stock.py "$1" "$axd_library"
fi
