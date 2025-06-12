// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#pragma once

// Put board-specific pico-sdk definitions here. This file must exist.

#define PICO_XOSC_STARTUP_DELAY_MULTIPLIER 64
#define PICO_BOOT_STAGE2_CHOOSE_GENERIC_03H 1
// #define SYS_CLK_MHZ 200

// Disable double-tap reset to prevent debugging issues
// The timer is paused during debugging, causing busy_wait_us() to hang indefinitely
#define PICO_BOOTSEL_VIA_DOUBLE_RESET_TIMEOUT_MS 0
