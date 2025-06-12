// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include "multicore.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include <stdio.h>

// Global state variables
static volatile bool core1_running = false;
static volatile bool core1_should_stop = false;
static volatile bool deinit_hit = false;
static volatile uint32_t core1_counter = 0;

//
// Forward declaration
static void __not_in_flash_func(denkioto_core1_main)(void);

// Core1 main function that runs the infinite loop
static void __not_in_flash_func(denkioto_core1_main)(void) {
    core1_running = true;

    for (int irq_num = 0; irq_num < 26; irq_num++) {
        irq_set_enabled(irq_num, false);
    }

    while (!core1_should_stop) {
        // Atomic increment of the counter
        __atomic_add_fetch(&core1_counter, 1, __ATOMIC_SEQ_CST);

        tight_loop_contents();
    }

    core1_running = false;
}
//

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

int32_t denkioto_multicore_stop_core1(int in_deinit) {
    if (in_deinit) {
        deinit_hit = true;
    }

    if (!core1_running) {
        printf("denkioto_multicore_stop_core1() called but core1 is not running, in_deinit: %d\n", in_deinit);
        return -1;
    }

    // Signal core1 to stop
    core1_should_stop = true;

    // Wait for core1 to actually stop with longer timeout
    long retries = 10000;
    while (core1_running && retries > 0) {
        retries--;
        tight_loop_contents();
    }

    printf("Before reset: core1_running: %d, core1_should_stop: %d, retries left: %ld\n",
        core1_running, core1_should_stop, retries);

    // Reset the core (this will also reset its stack and state)
    multicore_reset_core1();
    spin_locks_reset();

    // Force reset our state variables after core reset
    denkioto_multicore_init();

    printf("denkioto_multicore_stop_core1() called, in_deinit: %d, retries left: %ld\n", in_deinit, retries);

    return retries;
}

uint32_t denkioto_multicore_get_counter(void) {
    return __atomic_load_n(&core1_counter, __ATOMIC_SEQ_CST);
}

bool denkioto_multicore_is_core1_running(void) {
    printf("denkioto_multicore_is_core1_running() called, core1_running: %d\n", core1_running);
    return core1_running;
}

void denkioto_multicore_reset_counter(void) {
    __atomic_store_n(&core1_counter, 0, __ATOMIC_SEQ_CST);
}
