// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT
//
// IRQ-based synchronization approach:
// - DMA channels start with transfer_count=1 (single byte mode)
// - IRQ handler checks each byte for zero (frame start marker)
// - Once zero found, DMA switches to transfer_count=26 (normal mode)
// - Ensures perfect frame alignment from first zero byte detected
// - Eliminates need for costly resync operations

#include "multicore.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "denkioto/uart_rx.pio.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

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
static int RingVals_stable[4][25];  // Stable copy for core0 reads

// Three-state atomic flags per ring:
// 0 = data not ready (stale or being written)
// 1 = data ready for reading
// 2 = data being read (core0 has exclusive access)
static volatile uint32_t ring_data_state[4] = {0, 0, 0, 0};

PIO uart_pio = pio0;
uint uart0_sm = 0;
uint uart1_sm = 1;
uint uart2_sm = 2;
uint uart3_sm = 3;
static int uart_pio_offset = -1;

// Double buffering structures
typedef struct {
    uint8_t buffer[2][26];  // Two buffers, each holding 26 bytes
    volatile uint8_t active_buffer;  // 0 or 1, indicates which buffer is being filled by DMA
    volatile bool buffer_ready[2];   // Indicates if buffer has complete data
    volatile long resync_count;      // Number of times resync was called for this ring
    volatile long data_ready_count;  // Number of times data was marked ready for this ring
    volatile bool is_synced;         // Whether we're in sync mode (26 bytes) or searching (1 byte)
} ring_buffer_t;

static ring_buffer_t ring_buffers[4];

// DMA channels - one per ring
static int dma_channels[4] = {-1, -1, -1, -1};

// DMA control blocks for chaining
static dma_channel_config dma_configs[4];

//
// Forward declarations
static void __not_in_flash_func(denkioto_core1_main)(void);
static void setup_dma_for_ring(int ring);
static void update_stable_ring_data(void);

// Touch ring DMA interrupt handler (uses DMA_IRQ_1)
static void __isr touch_ring_dma_irq_handler(void) {
    // Check which of our DMA channels triggered the interrupt
    for (int ring = 0; ring < 4; ring++) {
        if (dma_channels[ring] >= 0 && dma_channel_get_irq1_status(dma_channels[ring])) {
            // Clear the interrupt
            dma_channel_acknowledge_irq1(dma_channels[ring]);

            if (!ring_buffers[ring].is_synced) {
                // We're in single-byte mode, looking for zero
                uint8_t *buffer = ring_buffers[ring].buffer[ring_buffers[ring].active_buffer];
                uint8_t byte = buffer[0];  // Single byte always goes to position 0

                if (byte == 0) {
                    // Found zero! Now collect the remaining 25 bytes to complete this frame
                    ring_buffers[ring].is_synced = true;

                    // Configure DMA to read the next 25 bytes into buffer[1] through buffer[25]
                    dma_channel_set_trans_count(dma_channels[ring], 25, false);
                    dma_channel_set_write_addr(dma_channels[ring],
                        &ring_buffers[ring].buffer[ring_buffers[ring].active_buffer][1], false);
                    dma_channel_start(dma_channels[ring]);
                } else {
                    // Not zero, continue single-byte mode (same position)
                    dma_channel_set_write_addr(dma_channels[ring],
                        &ring_buffers[ring].buffer[ring_buffers[ring].active_buffer][0], false);
                    dma_channel_start(dma_channels[ring]);
                }
            } else {
                // We just completed either the initial 25-byte completion or a normal frame
                uint8_t completed_buffer = ring_buffers[ring].active_buffer;

                // Mark buffer as ready (it now contains a complete frame with zero at index 0)
                ring_buffers[ring].buffer_ready[completed_buffer] = true;

                // Switch to the other buffer
                ring_buffers[ring].active_buffer = 1 - completed_buffer;

                // Start normal 26-byte transfers for subsequent frames
                dma_channel_set_trans_count(dma_channels[ring], 26, false);
                dma_channel_set_write_addr(dma_channels[ring],
                    ring_buffers[ring].buffer[ring_buffers[ring].active_buffer], false);
                dma_channel_start(dma_channels[ring]);
            }
        }
    }
}

// Core1 main function that runs the infinite loop
static void __not_in_flash_func(denkioto_core1_main)(void) {
    core1_running = true;

    // Disable all interrupts on core1 except our touch ring DMA interrupt
    for (int irq_num = 0; irq_num < 32; irq_num++) {
        irq_set_enabled(irq_num, false);
    }

    // Enable DMA_IRQ_1 for touch ring channels
    irq_set_exclusive_handler(DMA_IRQ_1, touch_ring_dma_irq_handler);
    irq_set_enabled(DMA_IRQ_1, true);

    while (!core1_should_stop) {
        // Atomic increment of the counter
        __atomic_add_fetch(&core1_counter, 1, __ATOMIC_SEQ_CST);

        // Process data from double buffers filled by DMA interrupts
        for (int ring = 0; ring < 4; ring++) {
            // Only process if we're in synced mode and have complete buffers
            if (!ring_buffers[ring].is_synced) {
                continue;  // Still searching for sync, skip processing
            }

            // Check if we have a complete buffer ready to process
            uint8_t inactive_buffer = 1 - ring_buffers[ring].active_buffer;

            if (ring_buffers[ring].buffer_ready[inactive_buffer]) {
                // Process the complete buffer with thread-safe write
                uint8_t *buffer = ring_buffers[ring].buffer[inactive_buffer];

                // Try to claim writing access - accept state 0 or 1 (overwrite unread data)
                bool got_access = false;

                // First try: state 0 → 0 (available for writing)
                uint32_t expected = 0;
                if (__atomic_compare_exchange_n(&ring_data_state[ring], &expected, 0,
                    false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
                    got_access = true;
                } else {
                    // Second try: state 1 → 0 (overwrite unread data - latest data priority)
                    expected = 1;
                    if (__atomic_compare_exchange_n(&ring_data_state[ring], &expected, 0,
                        false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
                        got_access = true;
                    }
                    // If both fail, state must be 2 (core0 reading) - skip this update
                }

                if (got_access) {
                    // Since we switched to 26-byte mode only after finding zero,
                    // the buffer should always be properly aligned
                    if (buffer[0] == 0) {
                        // Frame is properly aligned - process the 25 data values (indices 1-25)
                        for (int i = 1; i < 26; i++) {
                            if (buffer[i] != 0) {  // Skip any unexpected zeros
                                int V = __min(255, abs((int)buffer[i]));
                                RingVals[ring][i - 1] = __min(255, __max(0, (V - 127) * 2));
                            } else {
                                RingVals[ring][i - 1] = 0;  // Handle unexpected zero
                            }
                        }

                        // Mark data ready for reading
                        __atomic_store_n(&ring_data_state[ring], 1, __ATOMIC_SEQ_CST);

                        // Increment data ready counter for debugging
                        __atomic_add_fetch(&ring_buffers[ring].data_ready_count, 1, __ATOMIC_SEQ_CST);
                    } else {
                        // This shouldn't happen with our new sync method, but handle it gracefully
                        // Reset to single-byte mode to resync
                        ring_buffers[ring].is_synced = false;
                        __atomic_add_fetch(&ring_buffers[ring].resync_count, 1, __ATOMIC_SEQ_CST);

                        // Reconfigure DMA back to single-byte mode
                        dma_channel_abort(dma_channels[ring]);
                        dma_channel_set_trans_count(dma_channels[ring], 1, false);
                        dma_channel_set_write_addr(dma_channels[ring],
                            &ring_buffers[ring].buffer[ring_buffers[ring].active_buffer][0], false);
                        dma_channel_start(dma_channels[ring]);
                    }
                }
                // Mark buffer as processed regardless
                ring_buffers[ring].buffer_ready[inactive_buffer] = false;
            }
        }

        tight_loop_contents();
    }

    // Disable DMA IRQ before exiting
    irq_set_enabled(DMA_IRQ_1, false);
    core1_running = false;
}


void denkioto_multicore_init(void) {
    // Reset all state
    core1_running = false;
    core1_should_stop = false;
    __atomic_store_n(&core1_counter, 0, __ATOMIC_SEQ_CST);

    // Initialize ring buffers
    for (int ring = 0; ring < 4; ring++) {
        memset(&ring_buffers[ring], 0, sizeof(ring_buffer_t));
        ring_buffers[ring].active_buffer = 0;
        ring_buffers[ring].buffer_ready[0] = false;
        ring_buffers[ring].buffer_ready[1] = false;
        ring_buffers[ring].is_synced = false;  // Start in sync-search mode
        __atomic_store_n(&ring_buffers[ring].resync_count, 0, __ATOMIC_SEQ_CST);
        __atomic_store_n(&ring_buffers[ring].data_ready_count, 0, __ATOMIC_SEQ_CST);

        // Initialize ring data arrays and state
        memset(RingVals[ring], 0, sizeof(RingVals[ring]));
        memset(RingVals_stable[ring], 0, sizeof(RingVals_stable[ring]));
        __atomic_store_n(&ring_data_state[ring], 0, __ATOMIC_SEQ_CST);
    }
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

    // Set up DMA for each ring - start with single byte transfers to find frame alignment
    for (int ring = 0; ring < 4; ring++) {
        setup_dma_for_ring(ring);
    }

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

    long retries = 500000;
    while (core1_running && retries > 0) {
        retries--;
        tight_loop_contents();
    }

    printf("Before core reset: core1_running: %d, core1_should_stop: %d, retries left: %ld\n",
        core1_running, core1_should_stop, retries);

    // Stop all DMA channels
    for (int ring = 0; ring < 4; ring++) {
        if (dma_channels[ring] >= 0) {
            dma_channel_abort(dma_channels[ring]);
            dma_channel_set_irq1_enabled(dma_channels[ring], false);
            dma_channel_unclaim(dma_channels[ring]);
            dma_channels[ring] = -1;
        }
    }

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


// Setup DMA for a specific ring
static void setup_dma_for_ring(int ring) {
    dma_channels[ring] = dma_claim_unused_channel(true);
    dma_configs[ring] = dma_channel_get_default_config(dma_channels[ring]);
    channel_config_set_transfer_data_size(&dma_configs[ring], DMA_SIZE_8);

    // Read from PIO RX FIFO (fixed address)
    io_rw_8 *rxfifo_shift = (io_rw_8 *)&uart_pio->rxf[ring] + 3;  // Upper byte for 8-bit data
    channel_config_set_read_increment(&dma_configs[ring], false);

    // Write to buffer (incrementing address)
    channel_config_set_write_increment(&dma_configs[ring], true);

    // Transfer 26 bytes per buffer
    channel_config_set_ring(&dma_configs[ring], false, 0);

    // Pace transfers based on PIO RX FIFO
    channel_config_set_dreq(&dma_configs[ring], pio_get_dreq(uart_pio, ring, false));

    dma_channel_configure(
        dma_channels[ring],
        &dma_configs[ring],
        ring_buffers[ring].buffer[0],  // Initial write address (buffer 0)
        rxfifo_shift,                   // Read address (PIO RX FIFO)
        1,                             // Start with single byte transfer for sync
        false                          // Don't start yet
        );

    // Enable DMA_IRQ_1 for this channel (avoid conflicts with audio/PIO on DMA_IRQ_0)
    dma_channel_set_irq1_enabled(dma_channels[ring], true);
    dma_channel_start(dma_channels[ring]);
}


// Core0 function to update stable ring data (thread-safe reader)
static void update_stable_ring_data(void) {
    for (int ring = 0; ring < 4; ring++) {
        // Try to atomically claim reading rights (1 → 2)
        uint32_t expected = 1;
        if (__atomic_compare_exchange_n(&ring_data_state[ring], &expected, 2,
            false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {

            // Successfully claimed - safe to copy
            memcpy(RingVals_stable[ring], RingVals[ring], sizeof(RingVals[ring]));

            // Release for writing (2 → 0)
            __atomic_store_n(&ring_data_state[ring], 0, __ATOMIC_SEQ_CST);
        }
        // If state != 1, skip (either no new data or already being updated)
    }
}

// RingVals accessor functions
int denkioto_multicore_get_ring_value(int ring, int index) {
    if (ring < 0 || ring >= 4 || index < 0 || index >= 25) {
        return -1;  // Invalid parameters
    }

    // Update stable data from core1 if available
    update_stable_ring_data();

    return RingVals_stable[ring][index];
}

void denkioto_multicore_get_ring_values(int ring, int *values, int count) {
    if (ring < 0 || ring >= 4 || values == NULL || count <= 0) {
        return;  // Invalid parameters
    }

    // Update stable data from core1 if available
    update_stable_ring_data();

    int copy_count = count > 25 ? 25 : count;
    for (int i = 0; i < copy_count; i++) {
        values[i] = RingVals_stable[ring][i];
    }
}

void denkioto_multicore_clear_ring_values(int ring) {
    if (ring < 0 || ring >= 4) {
        return;  // Invalid parameter
    }

    // Clear both working and stable arrays
    // Note: This is called from core0, so we need to be careful about atomics
    // For now, clear the stable array directly (core0 owns this)
    for (int i = 0; i < 25; i++) {
        RingVals_stable[ring][i] = 0;
    }

    // Try to clear the working array if we can get exclusive access
    uint32_t expected = 0;
    if (__atomic_compare_exchange_n(&ring_data_state[ring], &expected, 2,
        false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        // We got exclusive access, clear the working array too
        for (int i = 0; i < 25; i++) {
            RingVals[ring][i] = 0;
        }
        // Release back to available for writing
        __atomic_store_n(&ring_data_state[ring], 0, __ATOMIC_SEQ_CST);
    }
    // If we can't get access, that's okay - just the stable array is cleared
}

// Debug counter accessor functions
long denkioto_multicore_get_resync_count(int ring) {
    if (ring < 0 || ring >= 4) {
        return -1;  // Invalid parameter
    }
    return __atomic_load_n(&ring_buffers[ring].resync_count, __ATOMIC_SEQ_CST);
}

long denkioto_multicore_get_data_ready_count(int ring) {
    if (ring < 0 || ring >= 4) {
        return -1;  // Invalid parameter
    }
    return __atomic_load_n(&ring_buffers[ring].data_ready_count, __ATOMIC_SEQ_CST);
}
