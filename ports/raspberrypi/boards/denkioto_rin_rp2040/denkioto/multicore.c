// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include "multicore.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "denkioto/uart_rx.pio.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#define clamp(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#ifndef __min
#define __min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef __max
#define __max(a, b) (((a) > (b)) ? (a) : (b))
#endif

#define TX_PIN_MIDI 0  // UART 0
#define RX_PIN_MIDI 1  // UART 0
#define TX_PIN0 2
#define RX_PIN0 3
#define TX_PIN1 4
#define RX_PIN1 5
#define TX_PIN2 6
#define RX_PIN2 7
#define TX_PIN3 8
#define RX_PIN3 9

// Global state variables
static volatile bool core1_running = false;
static volatile bool core1_should_stop = false;
static volatile uint32_t core1_counter = 0;

static int RingVals[4][25];
PIO uart_pio = pio0;
uint uart0_sm = 0;
uint uart1_sm = 1;
uint uart2_sm = 2;
uint uart3_sm = 3;
static int uart_pio_offset = -1;

//
// Forward declarations
static void __not_in_flash_func(denkioto_core1_main)(void);
static bool denkioto_get_uart_rx(PIO pio, uint sm, uint8_t *data);

// Core1 main function that runs the infinite loop
static void __not_in_flash_func(denkioto_core1_main)(void) {
    core1_running = true;

    for (int irq_num = 0; irq_num < 26; irq_num++) {
        irq_set_enabled(irq_num, false);
    }

    while (!core1_should_stop) {
        // Atomic increment of the counter
        __atomic_add_fetch(&core1_counter, 1, __ATOMIC_SEQ_CST);

        for (int i = 0; i < 4; i++)
        {
            uint8_t data;
            static int ledidx[4] = {0, 0, 0, 0};
            if (denkioto_get_uart_rx(uart_pio, i, &data)) {
                if (data == 0) {
                    ledidx[i] = 0;
                } else {
                    if (ledidx[i] < 25) {
                        int V = __min(255, abs((int)data));
                        RingVals[i][ledidx[i]++] = __min(255, __max(0, (V - 127) * 2));
                    }
                }
            }
        }

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

    // Set up PIO to read touch rings
    uart_pio_offset = pio_add_program(uart_pio, &uart_rx_program);
    uart_rx_program_init(uart_pio, uart0_sm, uart_pio_offset, RX_PIN0, 100000);
    uart_rx_program_init(uart_pio, uart1_sm, uart_pio_offset, RX_PIN1, 100000);
    uart_rx_program_init(uart_pio, uart2_sm, uart_pio_offset, RX_PIN2, 100000);
    uart_rx_program_init(uart_pio, uart3_sm, uart_pio_offset, RX_PIN3, 100000);

    // Launch core1
    multicore_launch_core1(denkioto_core1_main);

    // Wait for core1 to signal it's ready
    while (!core1_running) {
        tight_loop_contents();
    }
}

int32_t denkioto_multicore_stop_core1(int where) {
    if (!core1_running) {
        printf("denkioto_multicore_stop_core1() called but core1 is not running, where: %d\n", where);
        return -1;
    }

    printf("Before scheduling stop: core1_running: %d, core1_should_stop: %d\n",
        core1_running, core1_should_stop);

    // Signal core1 to stop
    core1_should_stop = true;

    // Wait for core1 to actually stop with longer timeout
    long retries = 500000;
    while (core1_running && retries > 0) {
        retries--;
        tight_loop_contents();
    }

    printf("Before core reset: core1_running: %d, core1_should_stop: %d, retries left: %ld\n",
        core1_running, core1_should_stop, retries);

    // Stop and reset PIO state machines
    pio_sm_set_enabled(uart_pio, uart0_sm, false);
    pio_sm_set_enabled(uart_pio, uart1_sm, false);
    pio_sm_set_enabled(uart_pio, uart2_sm, false);
    pio_sm_set_enabled(uart_pio, uart3_sm, false);

    // Clear FIFOs
    pio_sm_clear_fifos(uart_pio, uart0_sm);
    pio_sm_clear_fifos(uart_pio, uart1_sm);
    pio_sm_clear_fifos(uart_pio, uart2_sm);
    pio_sm_clear_fifos(uart_pio, uart3_sm);

    // Remove the PIO program if it was loaded
    if (uart_pio_offset >= 0) {
        pio_remove_program(uart_pio, &uart_rx_program, uart_pio_offset);
        uart_pio_offset = -1;
    }

    // Reset the core (this will also reset its stack and state)
    multicore_reset_core1();
    spin_locks_reset();

    // Force reset our state variables after core reset
    denkioto_multicore_init();

    printf("denkioto_multicore_stop_core1() called, where: %d, retries left: %ld\n", where, retries);

    return retries;
}

uint32_t denkioto_multicore_get_counter(void) {
    return __atomic_load_n(&core1_counter, __ATOMIC_SEQ_CST);
}

bool denkioto_multicore_is_core1_running(void) {
    printf("denkioto_multicore_is_core1_running() called, core1_running: %d\n", core1_running);
    for (int irq_num = 0; irq_num < 26; irq_num++) {
        printf("is IRQ %d enabled? %d\n", irq_num, irq_is_enabled(irq_num));
    }
    return core1_running;
}

void denkioto_multicore_reset_counter(void) {
    __atomic_store_n(&core1_counter, 0, __ATOMIC_SEQ_CST);
}

static bool denkioto_get_uart_rx(PIO pio, uint sm, uint8_t *data) {
    // 8-bit read from the uppermost byte of the FIFO, as data is left-justified
    if (pio_sm_is_rx_fifo_empty(pio, sm)) {
        return false;
    }
    io_rw_8 *rxfifo_shift = (io_rw_8 *)&pio->rxf[sm] + 3;
    *data = *rxfifo_shift;
    return true;
}

// RingVals accessor functions
int denkioto_multicore_get_ring_value(int ring, int index) {
    if (ring < 0 || ring >= 4 || index < 0 || index >= 25) {
        return -1;  // Invalid parameters
    }
    return RingVals[ring][index];
}

void denkioto_multicore_get_ring_values(int ring, int *values, int count) {
    if (ring < 0 || ring >= 4 || values == NULL || count <= 0) {
        return;  // Invalid parameters
    }

    int copy_count = count > 25 ? 25 : count;
    for (int i = 0; i < copy_count; i++) {
        values[i] = RingVals[ring][i];
    }
}

void denkioto_multicore_clear_ring_values(int ring) {
    if (ring < 0 || ring >= 4) {
        return;  // Invalid parameter
    }

    for (int i = 0; i < 25; i++) {
        RingVals[ring][i] = 0;
    }
}
