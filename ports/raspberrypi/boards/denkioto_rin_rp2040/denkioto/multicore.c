// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include "multicore.h"
#include "pico/multicore.h"

// Global state variables
static volatile bool core1_running = false;
static volatile bool core1_should_stop = false;
static uint32_t core1_counter = 0;

// Forward declaration
static void __not_in_flash_func(denkioto_core1_main)(void);

// Core1 main function that runs the infinite loop
static void __not_in_flash_func(denkioto_core1_main)(void) {
    // Signal that core1 is ready
    core1_running = true;

    // Main loop - increment counter until told to stop
    while (!core1_should_stop) {
        // Atomic increment of the counter
        __atomic_add_fetch(&core1_counter, 1, __ATOMIC_SEQ_CST);

        // Small delay to prevent overwhelming the system
        // This also allows other operations to happen
        tight_loop_contents();
    }

    // Cleanup when stopping
    core1_running = false;
}

void denkioto_multicore_init(void) {
    // Reset all state
    core1_running = false;
    core1_should_stop = false;
    __atomic_store_n(&core1_counter, 0, __ATOMIC_SEQ_CST);
}

void denkioto_multicore_start_core1(void) {
    // Only start if not already running
    if (core1_running) {
        return;
    }

    // Reset stop flag
    core1_should_stop = false;

    // Launch core1
    multicore_launch_core1(denkioto_core1_main);

    // Wait for core1 to signal it's ready
    while (!core1_running) {
        tight_loop_contents();
    }
}

void denkioto_multicore_stop_core1(void) {
    if (!core1_running) {
        return;
    }

    // Signal core1 to stop
    core1_should_stop = true;

    // Wait for core1 to actually stop
    while (core1_running) {
        tight_loop_contents();
    }

    // Reset the core (this will also reset its stack and state)
    multicore_reset_core1();
}

uint32_t denkioto_multicore_get_counter(void) {
    return __atomic_load_n(&core1_counter, __ATOMIC_SEQ_CST);
}

bool denkioto_multicore_is_core1_running(void) {
    return core1_running;
}

void denkioto_multicore_reset_counter(void) {
    __atomic_store_n(&core1_counter, 0, __ATOMIC_SEQ_CST);
}
