// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 Łukasz Langa
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
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "denkioto/uart_rx.pio.h"
#include "denkioto/uart_tx.pio.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "hardware/uart.h"
#include "hardware/timer.h"
#include "hardware/structs/iobank0.h"
#include "tusb.h"
#include "denkioto/midi_state.h"

// TinyUSB internal structures for direct FIFO access
#include "tusb_config.h"
#include "common/tusb_fifo.h"
#include "class/midi/midi.h"

// Buffer size constants (defined in RP2040 Makefile)
#ifndef CFG_TUD_MIDI_RX_BUFSIZE
#define CFG_TUD_MIDI_RX_BUFSIZE 128
#endif
#ifndef CFG_TUD_MIDI_TX_BUFSIZE
#define CFG_TUD_MIDI_TX_BUFSIZE 128
#endif

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
#define POWENABLE_PIN 21  // GPIO21 controls MIDI IN circuit power


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
static volatile uint32_t uart_irq_byte_counter = 0;

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

// MIDI OUT state for PIO1 SM0
static PIO midi_out_pio = pio1;
static uint midi_out_sm = 0;
static int midi_out_pio_offset = -1;
static bool midi_out_initialized = false;

// MIDI Clock definitions
#define MIDI_BAUDRATE 31250
#define MIDI_CLOCK 0xF8
#define MIDI_START 0xFA
#define MIDI_CONTINUE 0xFB
#define MIDI_STOP 0xFC
#define MIDI_SPP 0xF2

// Transport state definitions
#define TRANSPORT_STOPPED 0
#define TRANSPORT_PLAYING 1
#define TRANSPORT_PAUSED 2

// Two-stage filter constants for BPM stability
#define BPM_SHORT_WINDOW 24   // For detecting tempo changes (24 clocks = 1 beat)
#define BPM_LONG_WINDOW 96   // For stable output (96 clocks = 4 beats)

// MIDI Clock state structure
typedef struct {
    volatile uint32_t current_bpm_x1000;     // BPM * 1000 for precision
    volatile uint8_t transport_state;        // 0=stop, 1=play, 2=pause
    volatile uint8_t active_source;          // 0=none, 1=uart, 2=usb
    volatile uint64_t last_clock_timestamp[3];  // Last clock timestamp per source [none, uart, usb]
    volatile int32_t clock_count;            // Unified clock count since last transport start
    volatile int32_t beat_count;             // Unified beat count (6 clocks = 1 beat)
    volatile uint8_t source_priority[3];     // Priority per source [none, uart, usb] - lower number = higher priority
    volatile uint64_t last_source_seen[3];   // Timestamp when each source was last seen
    volatile uint32_t core1_clock_count;     // Clocks processed by Core 1
    volatile uint32_t core0_filtered_count;  // Clocks filtered by Core 0
    // Two-stage filter data
    volatile uint64_t short_intervals[BPM_SHORT_WINDOW];
    volatile uint64_t long_intervals[BPM_LONG_WINDOW];
    volatile uint8_t short_index;
    volatile uint8_t long_index;
    volatile uint8_t intervals_filled;       // How many intervals we've collected
} unified_midi_clock_t;

static unified_midi_clock_t midi_clock_state;

// Initialize MIDI clock state to default values
static void init_midi_clock_state(void) {
    midi_clock_state.current_bpm_x1000 = 120000;  // Default 120 BPM
    midi_clock_state.transport_state = TRANSPORT_STOPPED;
    midi_clock_state.active_source = MIDI_NONE;
    midi_clock_state.last_clock_timestamp[MIDI_NONE] = 0;
    midi_clock_state.last_clock_timestamp[MIDI_UART] = 0;
    midi_clock_state.last_clock_timestamp[MIDI_USB] = 0;
    midi_clock_state.clock_count = -1;
    midi_clock_state.beat_count = -1;
    midi_clock_state.source_priority[MIDI_NONE] = 255;  // None has lowest priority
    midi_clock_state.source_priority[MIDI_UART] = 1;    // UART first
    midi_clock_state.source_priority[MIDI_USB] = 2;     // USB second
    midi_clock_state.last_source_seen[MIDI_NONE] = 0;
    midi_clock_state.last_source_seen[MIDI_UART] = 0;
    midi_clock_state.last_source_seen[MIDI_USB] = 0;
    midi_clock_state.core1_clock_count = 0;
    midi_clock_state.core0_filtered_count = 0;

    // Initialize two-stage filter
    for (int i = 0; i < BPM_SHORT_WINDOW; i++) {
        midi_clock_state.short_intervals[i] = 0;
    }
    for (int i = 0; i < BPM_LONG_WINDOW; i++) {
        midi_clock_state.long_intervals[i] = 0;
    }
    midi_clock_state.short_index = 0;
    midi_clock_state.long_index = 0;
    midi_clock_state.intervals_filled = 0;
}

// Clock source timeout in microseconds (500ms)
#define CLOCK_SOURCE_TIMEOUT_US 500000

static volatile uint64_t last_usb_poll_time = 0;

// MIDI parsing state for SPP messages
typedef struct {
    uint8_t state;           // 0=waiting for status, 1=got F2, need LSB, 2=got F2+LSB, need MSB
    uint8_t spp_lsb;         // LSB of SPP position
    uint8_t spp_msb;         // MSB of SPP position
} midi_spp_parser_state_t;

static midi_spp_parser_state_t spp_parsers[3] = {0}; // Indexed by MIDI_*

// USB MIDI clock tracking state
static struct {
    uint32_t last_clock_count;
    uint32_t last_start_count;
    uint32_t last_continue_count;
    uint32_t last_stop_count;
    uint32_t last_spp_count;
} usb_midi_tracking = {0};


// UART MIDI parser state
typedef struct {
    uint8_t state;          // Parser state
    uint8_t status;         // Running status byte
    uint8_t channel;        // Channel from status byte (0-15)
    uint8_t data1;          // First data byte
    uint8_t expected_bytes; // Number of data bytes expected
    uint8_t received_bytes; // Number of data bytes received
} midi_parser_t;

// UART MIDI state - 16 channels
static midi_channel_t uart_midi_state[16];
static midi_parser_t uart_midi_parser = {0};

static void init_usb_midi_tracking(void) {
    last_usb_poll_time = 0;
    usb_midi_tracking.last_clock_count = 0;
    usb_midi_tracking.last_start_count = 0;
    usb_midi_tracking.last_continue_count = 0;
    usb_midi_tracking.last_stop_count = 0;
    usb_midi_tracking.last_spp_count = 0;
}

// Initialize UART MIDI state
static void init_uart_midi_state(void) {
    for (int ch = 0; ch < 16; ch++) {
        // Clear all note velocities
        memset((void *)uart_midi_state[ch].notes, 0, 128);

        // Initialize CC values to 0
        memset((void *)uart_midi_state[ch].cc, 0, 128);

        // Clear polyphonic aftertouch
        memset((void *)uart_midi_state[ch].poly_pressure, 0, 128);

        // Center pitch bend
        uart_midi_state[ch].pitch_bend = 8192;

        // Clear aftertouch
        uart_midi_state[ch].channel_pressure = 0;

        // Program 0
        uart_midi_state[ch].program = 0;

        // Clear note status
        memset((void *)uart_midi_state[ch].note_status, 0, sizeof(uart_midi_state[ch].note_status));

        // Clear timestamp
        uart_midi_state[ch].last_update = 0;
    }

    // Reset parser
    uart_midi_parser.state = 0;
    uart_midi_parser.status = 0;
    uart_midi_parser.channel = 0;
    uart_midi_parser.data1 = 0;
    uart_midi_parser.expected_bytes = 0;
    uart_midi_parser.received_bytes = 0;
}

//
// Forward declarations
static void __not_in_flash_func(denkioto_core1_main)(void);
static void setup_dma_for_ring(int ring);
static void update_stable_ring_data(void);
static void midi_uart_irq_handler(void);

static inline void process_midi_clock(uint64_t timestamp, uint8_t source);
static inline void process_midi_start(uint64_t timestamp, uint8_t source);
static inline void process_midi_stop(uint64_t timestamp, uint8_t source);
static inline void process_midi_continue(uint64_t timestamp, uint8_t source);
static inline void process_midi_spp(uint16_t spp_position, uint64_t timestamp, uint8_t source);
static inline void setup_uart_midi(void);
static inline void core1_usb_midi_poll(void);



// Touch ring DMA interrupt handler (uses DMA_IRQ_1)
static void __isr __not_in_flash_func(touch_ring_dma_irq_handler)(void) {
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

// MIDI UART interrupt handler
static void __isr __not_in_flash_func(midi_uart_irq_handler)(void) {
    uint64_t timestamp = time_us_64();  // Microsecond precision
    uint32_t byte_counter = 0;
    while (uart_is_readable(uart0)) {
        uint8_t byte = uart_getc(uart0);
        byte_counter++;

        // Handle System Real Time messages (can occur anywhere)
        if (byte == MIDI_CLOCK) {
            if (byte_counter > 1) {
                timestamp = time_us_64();
            }
            process_midi_clock(timestamp, MIDI_UART);
            continue;
        } else if (byte == MIDI_START) {
            process_midi_start(timestamp, MIDI_UART);
            continue;
        } else if (byte == MIDI_CONTINUE) {
            process_midi_continue(timestamp, MIDI_UART);
            continue;
        } else if (byte == MIDI_STOP) {
            process_midi_stop(timestamp, MIDI_UART);
            continue;
        }

        // Handle System Common messages
        if (byte == MIDI_SPP) {
            spp_parsers[MIDI_UART].state = 1;  // Expecting LSB next
            uart_midi_parser.state = 0;  // Reset channel message parser
            continue;
        } else if (spp_parsers[MIDI_UART].state > 0) {
            // Continue SPP parsing
            if ((byte & 0x80) == 0) {  // Data byte
                if (spp_parsers[MIDI_UART].state == 1) {
                    spp_parsers[MIDI_UART].spp_lsb = byte;
                    spp_parsers[MIDI_UART].state = 2;
                } else if (spp_parsers[MIDI_UART].state == 2) {
                    spp_parsers[MIDI_UART].spp_msb = byte;
                    uint16_t spp_position = spp_parsers[MIDI_UART].spp_lsb | (spp_parsers[MIDI_UART].spp_msb << 7);
                    process_midi_spp(spp_position, timestamp, MIDI_UART);
                    spp_parsers[MIDI_UART].state = 0;
                }
                continue;
            } else {
                // Status byte resets SPP parser
                spp_parsers[MIDI_UART].state = 0;
                // Fall through to process this status byte
            }
        }

        // Handle channel messages and running status
        if (byte & 0x80) {
            // Status byte
            uint8_t msg_type = byte & 0xF0;

            // Check if it's a channel message
            if (msg_type >= 0x80 && msg_type <= 0xE0) {
                uart_midi_parser.status = byte;
                uart_midi_parser.channel = byte & 0x0F;
                uart_midi_parser.received_bytes = 0;

                // Determine expected data bytes
                switch (msg_type) {
                    case 0xC0:  // Program Change
                    case 0xD0:  // Channel Pressure
                        uart_midi_parser.expected_bytes = 1;
                        break;
                    case 0x80:  // Note Off
                    case 0x90:  // Note On
                    case 0xA0:  // Poly Aftertouch
                    case 0xB0:  // Control Change
                    case 0xE0:  // Pitch Bend
                        uart_midi_parser.expected_bytes = 2;
                        break;
                    default:
                        uart_midi_parser.expected_bytes = 0;
                }
                uart_midi_parser.state = 1;  // Expecting data
            } else {
                // System message - reset parser
                uart_midi_parser.state = 0;
            }
        } else if (uart_midi_parser.state == 1 && uart_midi_parser.status != 0) {
            // Data byte with running status
            if (uart_midi_parser.received_bytes == 0) {
                uart_midi_parser.data1 = byte;
                uart_midi_parser.received_bytes = 1;

                if (uart_midi_parser.expected_bytes == 1) {
                    // Single data byte message complete
                    process_midi_message(uart_midi_state, uart_midi_parser.status, uart_midi_parser.data1, 0, timestamp);
                    uart_midi_parser.received_bytes = 0;  // Ready for next message with running status
                }
            } else if (uart_midi_parser.received_bytes == 1 && uart_midi_parser.expected_bytes == 2) {
                // Second data byte
                process_midi_message(uart_midi_state, uart_midi_parser.status, uart_midi_parser.data1, byte, timestamp);
                uart_midi_parser.received_bytes = 0;  // Ready for next message with running status
            }
        }
    }
    if (uart_irq_byte_counter < byte_counter) {
        uart_irq_byte_counter = byte_counter;
    }
}

// Process MIDI clock from UART source
// Helper function to calculate average of an interval array
static uint64_t calculate_interval_average(const volatile uint64_t *intervals, uint8_t count) {
    if (count == 0) {
        return 0;
    }

    uint64_t sum = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (intervals[i] > 0) {  // Only count valid intervals
            sum += intervals[i];
        }
    }
    return sum / count;
}

// Two-stage filter for BPM calculation (only for active source)
static inline void update_bpm_with_interval(uint64_t interval, uint8_t source) {
    if (interval == 0 || interval > 1000000) {
        return;  // Sanity check
    }

    // Only update BPM calculation if this is the active source
    if (midi_clock_state.active_source != source) {
        return;  // Ignore intervals from inactive sources
    }
    // Store in both windows
    midi_clock_state.short_intervals[midi_clock_state.short_index] = interval;
    midi_clock_state.short_index = (midi_clock_state.short_index + 1) % BPM_SHORT_WINDOW;

    midi_clock_state.long_intervals[midi_clock_state.long_index] = interval;
    midi_clock_state.long_index = (midi_clock_state.long_index + 1) % BPM_LONG_WINDOW;

    // Track how many samples we've collected
    if (midi_clock_state.intervals_filled < BPM_LONG_WINDOW) {
        midi_clock_state.intervals_filled++;
    }

    // Calculate averages based on how many samples we have
    uint64_t short_avg, long_avg;

    if (midi_clock_state.intervals_filled >= BPM_SHORT_WINDOW) {
        short_avg = calculate_interval_average(midi_clock_state.short_intervals, BPM_SHORT_WINDOW);
    } else {
        short_avg = calculate_interval_average(midi_clock_state.short_intervals, midi_clock_state.intervals_filled);
    }

    if (midi_clock_state.intervals_filled >= BPM_LONG_WINDOW) {
        long_avg = calculate_interval_average(midi_clock_state.long_intervals, BPM_LONG_WINDOW);
    } else {
        long_avg = short_avg;  // Not enough samples for long window yet
    }

    // Choose which average to use
    uint64_t final_interval;

    if (midi_clock_state.intervals_filled >= BPM_LONG_WINDOW) {
        // Check if short and long averages agree within 0.5%
        uint64_t diff = (short_avg > long_avg) ? (short_avg - long_avg) : (long_avg - short_avg);
        if (diff < long_avg / 200) {
            // Stable tempo - use long average
            final_interval = long_avg;
        } else {
            // Tempo is changing - use short average for responsiveness
            final_interval = short_avg;
        }
    } else {
        // Still filling buffers - use what we have
        final_interval = short_avg;
    }

    // Calculate BPM from final interval
    // BPM = 60,000,000 / (interval * 24)
    // BPM * 1000 = 60,000,000,000 / (interval * 24) = 2,500,000,000 / interval
    midi_clock_state.current_bpm_x1000 = (uint32_t)(2500000000ULL / final_interval);
}

// Unified MIDI clock processing function
static inline void process_midi_clock(uint64_t timestamp, uint8_t source) {
    if (source > 2) {
        return;              // Invalid source
    }
    // Always update last seen timestamp for timeout detection
    midi_clock_state.last_source_seen[source] = timestamp;

    // Update active source based on configured priorities BEFORE incrementing counters
    if (midi_clock_state.active_source != source) {
        // Get priorities directly using source as index
        uint8_t incoming_priority = midi_clock_state.source_priority[source];
        uint8_t current_priority = midi_clock_state.source_priority[midi_clock_state.active_source];

        // Check if we should switch to the incoming source
        bool should_switch = false;

        if (midi_clock_state.active_source == MIDI_NONE) {
            // No active source, always switch
            should_switch = true;
        } else if (incoming_priority < current_priority) {
            // Incoming has higher priority (lower number = higher priority)
            should_switch = true;
        } else {
            // Incoming has lower (or same) priority, only switch if current has timed out
            uint64_t current_last_seen = midi_clock_state.last_source_seen[midi_clock_state.active_source];
            // Guard against timestamp being older than last_seen (due to interrupt timing)
            if (timestamp > current_last_seen && timestamp - current_last_seen > CLOCK_SOURCE_TIMEOUT_US) {
                should_switch = true;
            }
        }

        if (should_switch) {
            midi_clock_state.active_source = source;
            // Reset filter state when switching
            midi_clock_state.intervals_filled = 0;
            midi_clock_state.short_index = 0;
            midi_clock_state.long_index = 0;
        }
    }

    // Only increment counters and calculate BPM if this is the active source
    if (midi_clock_state.active_source == source) {
        // Calculate BPM from clock interval (24 clocks per quarter note)
        if (midi_clock_state.last_clock_timestamp[source] > 0) {
            uint64_t interval = timestamp - midi_clock_state.last_clock_timestamp[source];
            update_bpm_with_interval(interval, source);
        }

        midi_clock_state.last_clock_timestamp[source] = timestamp;
        midi_clock_state.clock_count++;

        // Update beat count (6 clocks = 1 beat)
        if (midi_clock_state.clock_count % 6 == 0) {
            midi_clock_state.beat_count++;
        }

        midi_clock_state.core1_clock_count++;
    }
}

// Process MIDI START from any source
static inline void process_midi_start(uint64_t timestamp, uint8_t source) {
    midi_clock_state.transport_state = TRANSPORT_PLAYING;
    midi_clock_state.clock_count = -1;
    midi_clock_state.beat_count = -1;
    midi_clock_state.last_clock_timestamp[MIDI_NONE] = 0;
    midi_clock_state.last_clock_timestamp[MIDI_UART] = 0;  // Reset for new BPM calculation
    midi_clock_state.last_clock_timestamp[MIDI_USB] = 0;   // Reset for new BPM calculation
    // Reset filter state for fresh start
    midi_clock_state.intervals_filled = 0;
    midi_clock_state.short_index = 0;
    midi_clock_state.long_index = 0;
    if (source <= 2) {
        midi_clock_state.last_source_seen[source] = timestamp;
    }
}

// Process MIDI STOP from any source
static inline void process_midi_stop(uint64_t timestamp, uint8_t source) {
    midi_clock_state.transport_state = TRANSPORT_STOPPED;
    // Reset counts to -1 so next clock after START will be beat 0
    midi_clock_state.clock_count = -1;
    midi_clock_state.beat_count = -1;
    if (source <= 2) {
        midi_clock_state.last_source_seen[source] = timestamp;
    }
}

// Process MIDI CONTINUE from any source
static inline void process_midi_continue(uint64_t timestamp, uint8_t source) {
    midi_clock_state.transport_state = TRANSPORT_PLAYING;
    if (source <= 2) {
        midi_clock_state.last_source_seen[source] = timestamp;
    }
}

// Process Song Position Pointer message
static inline void process_midi_spp(uint16_t spp_position, uint64_t timestamp, uint8_t source) {
    // SPP position is in MIDI beats (1/16 notes)
    // 1 MIDI beat = 6 MIDI clocks
    // So clock_count = spp_position * 6
    // Decrement by 1 so next clock after CONTINUE snaps position in place
    int32_t clock_position = (int32_t)(spp_position * 6) - 1;
    int32_t beat_position = (int32_t)spp_position - 1;

    // Update unified clock and beat counts
    midi_clock_state.clock_count = clock_position;
    midi_clock_state.beat_count = beat_position;
    if (source <= 2) {
        midi_clock_state.last_source_seen[source] = timestamp;
    }
}

// Setup UART for MIDI
__attribute__((used))
static inline void setup_uart_midi(void) {
    // First, ensure UART0 is deinitialized in case CircuitPython was using it
    uart_deinit(uart0);

    // Initialize UART0 for MIDI IN only (RX)
    uart_init(uart0, MIDI_BAUDRATE);

    // Set RX pin only - TX will be handled by PIO
    // gpio_set_function(TX_PIN_MIDI, GPIO_FUNC_UART);
    gpio_set_function(RX_PIN_MIDI, GPIO_FUNC_UART);

    // Set UART flow control off
    uart_set_hw_flow(uart0, false, false);

    // Set data format: 8 data bits, 1 stop bit, no parity
    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);

    // Clear any pending data in the UART RX FIFO
    while (uart_is_readable(uart0)) {
        uart_getc(uart0);
    }

    // Enable UART RX interrupt on Core 1
    irq_set_exclusive_handler(UART0_IRQ, midi_uart_irq_handler);
    irq_set_enabled(UART0_IRQ, true);
    irq_set_priority(UART0_IRQ, PICO_LOWEST_IRQ_PRIORITY);
    uart_set_irq_enables(uart0, true, false);  // RX only
}

// Transport event structure for ordered processing
typedef struct {
    uint32_t counter;
    uint8_t type; // MIDI_START, MIDI_STOP, MIDI_CONTINUE, or MIDI_SPP
} transport_event_t;

// Helper function to insert event in sorted order
static inline void insert_transport_event(transport_event_t *events, int *count, uint32_t counter, uint8_t type) {
    int i = *count;
    // Find insertion point
    while (i > 0 && events[i - 1].counter > counter) {
        events[i] = events[i - 1];
        i--;
    }
    events[i].counter = counter;
    events[i].type = type;
    (*count)++;
}

// Core 1 USB MIDI clock monitoring using atomic counters
static inline void core1_usb_midi_poll(void) {
    uint64_t now = time_us_64();
    uint64_t last_poll_time = last_usb_poll_time;
    last_usb_poll_time = now;

    // Check if USB is enumerated
    if (!tud_ready()) {
        return;
    }

    // Check for new clock messages using atomic counters
    // TinyUSB updates these when it receives clock messages
    uint32_t current_clock_count = denkioto_get_usb_midi_clock_count();
    uint32_t current_start_count = denkioto_get_usb_midi_start_count();
    uint32_t current_continue_count = denkioto_get_usb_midi_continue_count();
    uint32_t current_stop_count = denkioto_get_usb_midi_stop_count();
    uint32_t current_spp_count = denkioto_get_usb_midi_spp_count();

    // Process multiple clock messages with smeared timestamps
    int32_t clock_delta = current_clock_count - usb_midi_tracking.last_clock_count;
    if (clock_delta > 0) {
        if (clock_delta == 1) {
            // Single clock: use current timestamp
            process_midi_clock(now, MIDI_USB);
        } else {
            // Multiple clocks: smear timestamps evenly over the polling interval
            uint64_t time_per_pulse = (now - last_poll_time) / clock_delta;
            for (uint32_t i = 0; i < (uint32_t)clock_delta; i++) {
                uint64_t synthetic_timestamp = last_poll_time + (i + 1) * time_per_pulse;
                process_midi_clock(synthetic_timestamp, MIDI_USB);
            }
        }
        usb_midi_tracking.last_clock_count = current_clock_count;
    } else {
        // If last_clock_count is greater, it means either core0 was reset or the counter overflowed.
        // Therefore, restore sanity.
        usb_midi_tracking.last_clock_count = current_clock_count;
    }

    // Process transport messages in the order they occurred
    transport_event_t events[4];
    int event_count = 0;

    // Collect events that need processing, inserting in sorted order
    if (current_start_count != usb_midi_tracking.last_start_count) {
        insert_transport_event(events, &event_count, current_start_count, MIDI_START);
    }
    if (current_continue_count != usb_midi_tracking.last_continue_count) {
        insert_transport_event(events, &event_count, current_continue_count, MIDI_CONTINUE);
    }
    if (current_stop_count != usb_midi_tracking.last_stop_count) {
        insert_transport_event(events, &event_count, current_stop_count, MIDI_STOP);
    }
    if (current_spp_count != usb_midi_tracking.last_spp_count) {
        insert_transport_event(events, &event_count, current_spp_count, MIDI_SPP);
    }

    // Process events in order
    for (int i = 0; i < event_count; i++) {
        switch (events[i].type) {
            case MIDI_START:
                process_midi_start(now, MIDI_USB);
                usb_midi_tracking.last_start_count = current_start_count;
                break;
            case MIDI_CONTINUE:
                process_midi_continue(now, MIDI_USB);
                usb_midi_tracking.last_continue_count = current_continue_count;
                break;
            case MIDI_STOP:
                process_midi_stop(now, MIDI_USB);
                usb_midi_tracking.last_stop_count = current_stop_count;
                break;
            case MIDI_SPP: {
                uint16_t spp_position = denkioto_get_usb_midi_spp_position();
                process_midi_spp(spp_position, now, MIDI_USB);
                usb_midi_tracking.last_spp_count = current_spp_count;
            }
            break;
        }
    }
}

// Core1 main function that runs the infinite loop
static void __not_in_flash_func(denkioto_core1_main)(void) {
    core1_running = true;

    // Disable all interrupts on core1
    for (unsigned int irq_num = 0; irq_num < NUM_IRQS; irq_num++) {
        irq_set_enabled(irq_num, false);
    }

    // Initialize Core 1 to be interruptible for flash write protection
    multicore_lockout_victim_init();
    irq_set_priority(SIO_FIFO_IRQ_NUM(1), PICO_HIGHEST_IRQ_PRIORITY);

    // Enable DMA_IRQ_1 for touch ring channels
    irq_set_exclusive_handler(DMA_IRQ_1, touch_ring_dma_irq_handler);
    irq_set_enabled(DMA_IRQ_1, true);
    irq_set_priority(DMA_IRQ_1, PICO_LOWEST_IRQ_PRIORITY);

    setup_uart_midi();
    uint32_t timeout_check_counter = 0;

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

        // Poll USB MIDI for clock messages
        core1_usb_midi_poll();

        if (++timeout_check_counter > 10000) {
            timeout_check_counter = 0;

            // Check if active source has timed out
            uint8_t current_source = midi_clock_state.active_source;
            if (current_source > 0 && current_source <= 2) {
                uint64_t now = time_us_64();
                uint64_t last_seen = midi_clock_state.last_source_seen[current_source];
                if (now - last_seen > CLOCK_SOURCE_TIMEOUT_US) {
                    // Active source has timed out, reset to none
                    midi_clock_state.active_source = MIDI_NONE;
                }
            }
        }

        tight_loop_contents();
    }

    // Disable used IRQs before exiting
    irq_set_enabled(UART0_IRQ, false);
    irq_set_enabled(DMA_IRQ_1, false);
    multicore_lockout_victim_deinit();

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

    // Reset DMA channels to -1 to ensure proper initialization
    for (int i = 0; i < 4; i++) {
        dma_channels[i] = -1;
    }

    // Core0 also needs this
    multicore_lockout_victim_init();
    irq_set_priority(SIO_FIFO_IRQ_NUM(0), PICO_HIGHEST_IRQ_PRIORITY);
}

void denkioto_multicore_start_core1(void) {
    // Only start if not already running
    if (core1_running) {
        return;
    }

    // Reset stop flag
    core1_should_stop = false;

    // Enable 5V power for DIN MIDI IN circuit
    gpio_init(POWENABLE_PIN);
    gpio_set_dir(POWENABLE_PIN, GPIO_OUT);
    gpio_put(POWENABLE_PIN, true);

    // Initialize MIDI clock state
    init_midi_clock_state();
    init_usb_midi_tracking();
    init_uart_midi_state();

    // Set up PIO to read touch rings
    pio_sm_claim(uart_pio, uart0_sm);
    pio_sm_claim(uart_pio, uart1_sm);
    pio_sm_claim(uart_pio, uart2_sm);
    pio_sm_claim(uart_pio, uart3_sm);
    uart_pio_offset = pio_add_program(uart_pio, &uart_rx_program);
    uart_rx_program_init(uart_pio, uart0_sm, uart_pio_offset, RX_PIN0, 100000);
    uart_rx_program_init(uart_pio, uart1_sm, uart_pio_offset, RX_PIN1, 100000);
    uart_rx_program_init(uart_pio, uart2_sm, uart_pio_offset, RX_PIN2, 100000);
    uart_rx_program_init(uart_pio, uart3_sm, uart_pio_offset, RX_PIN3, 100000);

    // Set up DMA for each ring - start with single byte transfers to find frame alignment
    for (int ring = 0; ring < 4; ring++) {
        setup_dma_for_ring(ring);
    }

    // Initialize MIDI OUT on PIO1 SM0
    denkioto_multicore_init_midi_out();

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

    // This looks like it might wait forever, but we need to let core1 clean up its IRQs
    long cycles_waiting = 0;
    while (core1_running) {
        cycles_waiting++;
        tight_loop_contents();
    }

    printf("Before core reset: core1_running: %d, core1_should_stop: %d, cycles waiting: %ld\n",
        core1_running, core1_should_stop, cycles_waiting);

    // Stop all DMA channels
    for (int ring = 0; ring < 4; ring++) {
        if (dma_channels[ring] >= 0) {
            dma_channel_abort(dma_channels[ring]);
            dma_channel_set_irq1_enabled(dma_channels[ring], false);
            dma_channel_unclaim(dma_channels[ring]);
            dma_channels[ring] = -1;
        }
    }

    // Disable IRQs on Core 0 that were set up for multicore operation
    irq_set_enabled(DMA_IRQ_1, false);
    irq_set_enabled(UART0_IRQ, false);

    // Deinitialize UART0 to clean up MIDI UART state
    uart_deinit(uart0);

    // Reset MIDI clock state and USB tracking to clean state
    init_midi_clock_state();
    init_usb_midi_tracking();

    // Disable 5V power to MIDI IN circuit
    gpio_put(POWENABLE_PIN, false);
    gpio_deinit(POWENABLE_PIN);

    // Clean up MIDI OUT
    denkioto_multicore_deinit_midi_out();

    // Stop and reset PIO state machines
    pio_sm_set_enabled(uart_pio, uart0_sm, false);
    pio_sm_set_enabled(uart_pio, uart1_sm, false);
    pio_sm_set_enabled(uart_pio, uart2_sm, false);
    pio_sm_set_enabled(uart_pio, uart3_sm, false);

    pio_sm_clear_fifos(uart_pio, uart0_sm);
    pio_sm_clear_fifos(uart_pio, uart1_sm);
    pio_sm_clear_fifos(uart_pio, uart2_sm);
    pio_sm_clear_fifos(uart_pio, uart3_sm);

    if (uart_pio_offset >= 0) {
        pio_remove_program(uart_pio, &uart_rx_program, uart_pio_offset);
        uart_pio_offset = -1;
    }

    pio_sm_unclaim(uart_pio, uart0_sm);
    pio_sm_unclaim(uart_pio, uart1_sm);
    pio_sm_unclaim(uart_pio, uart2_sm);
    pio_sm_unclaim(uart_pio, uart3_sm);

    // Reset the core (this will also reset its stack and state)
    multicore_reset_core1();
    spin_locks_reset();

    // Force reset our state variables after core reset
    denkioto_multicore_init();

    printf("denkioto_multicore_stop_core1() called, where: %d, cycles waiting: %ld\n", where, cycles_waiting);

    return cycles_waiting;
}

uint32_t denkioto_multicore_get_counter(void) {
    return __atomic_load_n(&core1_counter, __ATOMIC_SEQ_CST);
}

bool denkioto_multicore_is_core1_running(void) {
    return core1_running;
}

// Reset TinyUSB atomic MIDI counters (called from core0 during port reset)
void denkioto_reset_usb_midi_counters(void) {
    __atomic_store_n(&usb_midi_clock_count, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&usb_midi_start_count, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&usb_midi_continue_count, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&usb_midi_stop_count, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&usb_midi_spp_count, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&usb_midi_spp_position, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&usb_midi_transport_max, 0, __ATOMIC_SEQ_CST);

    // Also reset USB MIDI channel state
    denkioto_init_usb_midi_state();
}

void denkioto_multicore_print_debug_stats(void) {
    printf("=== Core1 Debug Statistics ===\n");
    printf("core1_running: %d\n", core1_running);
    printf("core1_should_stop: %d\n", core1_should_stop);
    printf("core1_counter: %lu\n", (unsigned long)denkioto_multicore_get_counter());

    printf("\n=== IRQ Status ===\n");
    for (unsigned int irq_num = 0; irq_num < NUM_IRQS; irq_num++) {
        if (irq_is_enabled(irq_num)) {
            printf("IRQ %u: enabled\n", irq_num);
        }
    }

    printf("\n=== MIDI Clock Status ===\n");
    printf("BPM: %lu.%03lu\n",
        (unsigned long)(midi_clock_state.current_bpm_x1000 / 1000),
        (unsigned long)(midi_clock_state.current_bpm_x1000 % 1000));
    printf("Transport: %d (0=stop, 1=play, 2=pause)\n", midi_clock_state.transport_state);
    printf("Active source: %d (0=none, 1=uart, 2=usb)\n", midi_clock_state.active_source);
    printf("Unified clock count: %ld\n", (long)midi_clock_state.clock_count);
    printf("Unified beat count: %ld\n", (long)midi_clock_state.beat_count);
    printf("Core1 clock count: %lu\n", (unsigned long)midi_clock_state.core1_clock_count);
    printf("Core0 filtered count: %lu\n", (unsigned long)midi_clock_state.core0_filtered_count);

    printf("\n=== Touch Ring Status ===\n");
    for (int ring = 0; ring < 4; ring++) {
        int non_zero = 0;
        for (int i = 0; i < 25; i++) {
            if (RingVals_stable[ring][i] > 0) {
                non_zero++;
            }
        }
        printf("Ring %d: %d/25 active, state=%lu, resync=%ld, ready=%ld\n",
            ring, non_zero, (unsigned long)ring_data_state[ring],
            ring_buffers[ring].resync_count, ring_buffers[ring].data_ready_count);
    }
    printf("===============================\n");
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

// Update filtered count (called from Core 0)
void denkioto_multicore_update_filtered_count(void) {
    __atomic_add_fetch(&midi_clock_state.core0_filtered_count, 1, __ATOMIC_SEQ_CST);
}


uint32_t denkioto_multicore_get_midi_bpm_x1000(void) {
    return __atomic_load_n(&midi_clock_state.current_bpm_x1000, __ATOMIC_SEQ_CST);
}

uint8_t denkioto_multicore_get_transport_state(void) {
    return __atomic_load_n(&midi_clock_state.transport_state, __ATOMIC_SEQ_CST);
}

uint8_t denkioto_multicore_get_clock_source(void) {
    uint8_t source = __atomic_load_n(&midi_clock_state.active_source, __ATOMIC_SEQ_CST);

    // Check for source timeout (read-only check, no state modification)
    uint64_t now = time_us_64();
    if (source > 0 && source <= 2) {
        uint64_t last_seen = __atomic_load_n(&midi_clock_state.last_source_seen[source], __ATOMIC_SEQ_CST);
        if (now - last_seen > CLOCK_SOURCE_TIMEOUT_US) {
            // Source has timed out but don't modify state from Core 0
            return 0;  // Report no active source
        }
    }

    return source;
}

int32_t denkioto_multicore_get_clock_count(void) {
    return __atomic_load_n(&midi_clock_state.clock_count, __ATOMIC_SEQ_CST);
}

int32_t denkioto_multicore_get_beat_count(void) {
    return __atomic_load_n(&midi_clock_state.beat_count, __ATOMIC_SEQ_CST);
}

void denkioto_multicore_set_clock_source_priority(uint8_t uart_priority, uint8_t usb_priority) {
    // Ensure priorities are valid (1 or 2) and different
    if ((uart_priority == 1 || uart_priority == 2) &&
        (usb_priority == 1 || usb_priority == 2) &&
        uart_priority != usb_priority) {
        midi_clock_state.source_priority[MIDI_UART] = uart_priority;
        midi_clock_state.source_priority[MIDI_USB] = usb_priority;
    }
}

// Pause Core 1 before flash writes
void denkioto_multicore_pause(void) {
    if (!core1_running) {
        return;  // Nothing to pause
    }

    if (!multicore_lockout_victim_is_initialized(1)) {
        printf("Core 1 not initialized to be interruptible, cannot pause.\n");
        return;
    }

    multicore_lockout_start_blocking();
    printf("Core 1 paused for flash writes.\n");
}

// Resume Core 1 after flash writes
void denkioto_multicore_resume(void) {
    if (!core1_running) {
        return;  // Nothing to resume
    }

    if (!multicore_lockout_victim_is_initialized(1)) {
        printf("Core 1 not initialized to be interruptible, cannot resume.\n");
        return;
    }

    multicore_lockout_end_blocking();
    printf("Core 1 resumed after flash writes.\n");
}

// MIDI state accessor functions
uint8_t denkioto_multicore_get_note(uint8_t source, uint8_t channel, uint8_t note) {
    if (channel >= 16 || note >= 128) {
        return 0;
    }

    if (source == MIDI_UART) {
        return uart_midi_state[channel].notes[note];
    } else if (source == MIDI_USB) {
        return denkioto_usb_midi_get_note(channel, note);
    }

    return 0;
}

void denkioto_multicore_get_notes(uint8_t source, uint8_t channel, uint8_t *notes) {
    if (channel >= 16 || !notes) {
        memset(notes, 0, 128);
        return;
    }

    if (source == MIDI_UART) {
        memcpy(notes, (void *)uart_midi_state[channel].notes, 128);
    } else if (source == MIDI_USB) {
        denkioto_usb_midi_get_notes(channel, notes);
    } else {
        memset(notes, 0, 128);
    }
}

uint8_t denkioto_multicore_get_cc(uint8_t source, uint8_t channel, uint8_t cc) {
    if (channel >= 16 || cc >= 128) {
        return 0;
    }

    if (source == MIDI_UART) {
        return uart_midi_state[channel].cc[cc];
    } else if (source == MIDI_USB) {
        return denkioto_usb_midi_get_cc(channel, cc);
    }

    return 0;
}

void denkioto_multicore_get_cc_all(uint8_t source, uint8_t channel, uint8_t *values) {
    if (channel >= 16 || !values) {
        memset(values, 0, 128);
        return;
    }

    if (source == MIDI_UART) {
        memcpy(values, (void *)uart_midi_state[channel].cc, 128);
    } else if (source == MIDI_USB) {
        denkioto_usb_midi_get_cc_all(channel, values);
    } else {
        memset(values, 0, 128);
    }
}

uint8_t denkioto_multicore_get_poly_pressure(uint8_t source, uint8_t channel, uint8_t note) {
    if (channel >= 16 || note >= 128) {
        return 0;
    }

    if (source == MIDI_UART) {
        return uart_midi_state[channel].poly_pressure[note];
    } else if (source == MIDI_USB) {
        return denkioto_usb_midi_get_poly_pressure(channel, note);
    }

    return 0;
}

void denkioto_multicore_get_poly_pressure_all(uint8_t source, uint8_t channel, uint8_t *pressure) {
    if (channel >= 16 || !pressure) {
        memset(pressure, 0, 128);
        return;
    }

    if (source == MIDI_UART) {
        memcpy(pressure, (void *)uart_midi_state[channel].poly_pressure, 128);
    } else if (source == MIDI_USB) {
        denkioto_usb_midi_get_poly_pressure_all(channel, pressure);
    } else {
        memset(pressure, 0, 128);
    }
}

uint16_t denkioto_multicore_get_pitch_bend(uint8_t source, uint8_t channel) {
    if (channel >= 16) {
        return 8192;  // Center value
    }

    if (source == MIDI_UART) {
        return uart_midi_state[channel].pitch_bend;
    } else if (source == MIDI_USB) {
        return denkioto_usb_midi_get_pitch_bend(channel);
    }

    return 8192;  // Center value
}

uint8_t denkioto_multicore_get_channel_pressure(uint8_t source, uint8_t channel) {
    if (channel >= 16) {
        return 0;
    }

    if (source == MIDI_UART) {
        return uart_midi_state[channel].channel_pressure;
    } else if (source == MIDI_USB) {
        return denkioto_usb_midi_get_channel_pressure(channel);
    }

    return 0;
}

uint8_t denkioto_multicore_get_program(uint8_t source, uint8_t channel) {
    if (channel >= 16) {
        return 0;
    }

    if (source == MIDI_UART) {
        return uart_midi_state[channel].program;
    } else if (source == MIDI_USB) {
        return denkioto_usb_midi_get_program(channel);
    }

    return 0;
}

void denkioto_multicore_get_note_status(uint8_t source, uint8_t channel, uint32_t *status) {
    if (channel >= 16 || !status) {
        memset(status, 0, 4 * sizeof(uint32_t));
        return;
    }

    if (source == MIDI_UART) {
        memcpy(status, (void *)uart_midi_state[channel].note_status, 4 * sizeof(uint32_t));
    } else if (source == MIDI_USB) {
        denkioto_usb_midi_get_note_status(channel, status);
    } else {
        memset(status, 0, 4 * sizeof(uint32_t));
    }
}

uint64_t denkioto_multicore_get_last_update(uint8_t source, uint8_t channel) {
    if (channel >= 16) {
        return 0;
    }

    if (source == MIDI_UART) {
        return uart_midi_state[channel].last_update;
    } else if (source == MIDI_USB) {
        return denkioto_usb_midi_get_last_update(channel);
    }

    return 0;
}

void denkioto_multicore_init_midi_out(void) {
    if (midi_out_initialized) {
        return;
    }

    pio_sm_claim(midi_out_pio, midi_out_sm);
    midi_out_pio_offset = pio_add_program(midi_out_pio, &uart_tx_program);

    printf("UART MIDI OUT: PIO program loaded at offset %d\n", midi_out_pio_offset);
    printf("UART MIDI OUT: Using PIO1 SM%d on GPIO%d\n", midi_out_sm, TX_PIN_MIDI);
    uint32_t sys_clk = clock_get_hz(clk_sys);
    uint32_t clk_div = sys_clk / (8 * MIDI_BAUDRATE);
    printf("UART MIDI OUT: System clock %lu Hz, baud %d, clock div %lu\n",
        (unsigned long)sys_clk, MIDI_BAUDRATE, (unsigned long)clk_div);

    // Initialize the state machine for UART MIDI OUT on GPIO0 (TX_PIN_MIDI)
    uart_tx_program_init(midi_out_pio, midi_out_sm, midi_out_pio_offset, TX_PIN_MIDI, MIDI_BAUDRATE);

    // Double-check the pin is set correctly
    uint32_t pin_ctrl = iobank0_hw->io[TX_PIN_MIDI].ctrl;
    printf("UART MIDI OUT: GPIO%d control register: 0x%08lx (funcsel=%lu)\n",
        TX_PIN_MIDI, (unsigned long)pin_ctrl,
        (unsigned long)(pin_ctrl & IO_BANK0_GPIO0_CTRL_FUNCSEL_BITS));

    // Check if the state machine is enabled
    uint32_t sm_enabled = midi_out_pio->ctrl & (1u << midi_out_sm);
    printf("UART MIDI OUT: SM%d enabled: %s (CTRL=0x%08lx)\n",
        midi_out_sm, sm_enabled ? "yes" : "no", (unsigned long)midi_out_pio->ctrl);

    midi_out_initialized = true;
    printf("UART MIDI OUT: Initialized successfully\n");
}

void denkioto_multicore_deinit_midi_out(void) {
    if (!midi_out_initialized) {
        return;
    }

    pio_sm_set_enabled(midi_out_pio, midi_out_sm, false);
    pio_sm_clear_fifos(midi_out_pio, midi_out_sm);
    if (midi_out_pio_offset >= 0) {
        pio_remove_program(midi_out_pio, &uart_tx_program, midi_out_pio_offset);
        midi_out_pio_offset = -1;
    }

    pio_sm_unclaim(midi_out_pio, midi_out_sm);
    midi_out_initialized = false;
}

size_t denkioto_multicore_midi_out_write(uint8_t destination, const uint8_t *data, size_t len) {
    size_t bytes_written = 0;

    if (destination == MIDI_USB && tud_midi_mounted()) {
        bytes_written = tud_midi_stream_write(0, data, len);
    } else if (destination == MIDI_UART && midi_out_initialized) {
        #ifdef DEBUG_MIDI_OUT_PIO
        // Check if our PIO program has been corrupted
        uint32_t pc = pio_sm_get_pc(midi_out_pio, midi_out_sm);
        int32_t pc_offset = (int32_t)pc - (int32_t)midi_out_pio_offset;

        // If PC is way out of range, our program has been overwritten
        if (pc_offset < 0 || pc_offset > 3) {
            printf("MIDI OUT: PIO program corrupted (PC offset=%ld), reinitializing...\n", (long)pc_offset);
            denkioto_multicore_deinit_midi_out();
            denkioto_multicore_init_midi_out();
        }
        #endif
        // Send byte by byte (lower 8 bits of a 32-bit int) through PIO
        for (size_t i = 0; i < len; i++) {
            // Check if TX FIFO has space (non-blocking)
            if (!pio_sm_is_tx_fifo_full(midi_out_pio, midi_out_sm)) {
                pio_sm_put(midi_out_pio, midi_out_sm, (uint32_t)data[i]);
                bytes_written = i + 1;
            } else {
                // FIFO is full, stop here
                break;
            }
        }

        #ifdef DEBUG_MIDI_OUT_PIO
        // Debug: Show detailed state after writing
        if (bytes_written > 0) {
            uint32_t final_pc = pio_sm_get_pc(midi_out_pio, midi_out_sm);
            uint32_t txstall = midi_out_pio->fdebug & (1u << (PIO_FDEBUG_TXSTALL_LSB + midi_out_sm));
            uint32_t txlevel = (midi_out_pio->flevel >> (PIO_FLEVEL_TX0_LSB + midi_out_sm * 8)) & 0xF;
            int32_t final_pc_offset = (int32_t)final_pc - (int32_t)midi_out_pio_offset;

            printf("MIDI OUT: Sent %lu bytes, first byte=0x%02X\n",
                (unsigned long)bytes_written, data[0]);
            printf("  PIO state: PC offset=%ld, TXSTALL=%s, TX FIFO level=%lu\n",
                (long)final_pc_offset,
                txstall ? "yes" : "no",
                (unsigned long)txlevel);

            // Clear TXSTALL flag for next check
            midi_out_pio->fdebug = 1u << (PIO_FDEBUG_TXSTALL_LSB + midi_out_sm);
        }
        #endif
    }

    return bytes_written;
}

bool denkioto_multicore_midi_out_ready(uint8_t destination) {
    if (destination == MIDI_USB) {
        return tud_midi_mounted();
    } else if (destination == MIDI_UART && midi_out_initialized) {
        return !pio_sm_is_tx_fifo_full(midi_out_pio, midi_out_sm);
    }

    return false;
}

void denkioto_multicore_midi_panic(void) {
    printf("MIDI PANIC: Sending All Notes Off on all channels\n");

    // Control Change message format: 0xBn cc vv
    // where n = channel (0-15), cc = controller number, vv = value
    // CC 123 = All Notes Off, value = 0
    uint8_t all_notes_off[3];
    all_notes_off[1] = 123;  // All Notes Off controller
    all_notes_off[2] = 0;    // Value (usually 0 for this controller)

    // Send to all 16 channels on both outputs
    for (uint8_t channel = 0; channel < 16; channel++) {
        all_notes_off[0] = 0xB0 | channel;  // Control Change on channel

        // Send to USB MIDI
        if (tud_midi_mounted()) {
            tud_midi_stream_write(0, all_notes_off, 3);
        }

        // Send to UART MIDI
        if (midi_out_initialized) {
            // Send all 3 bytes, waiting for each to complete
            for (int i = 0; i < 3; i++) {
                uart_tx_program_putc(midi_out_pio, midi_out_sm, all_notes_off[i]);
            }
        }
    }

    // Also send All Sound Off (CC 120) for good measure
    all_notes_off[1] = 120;  // All Sound Off controller
    for (uint8_t channel = 0; channel < 16; channel++) {
        all_notes_off[0] = 0xB0 | channel;

        if (tud_midi_mounted()) {
            tud_midi_stream_write(0, all_notes_off, 3);
        }

        if (midi_out_initialized) {
            for (int i = 0; i < 3; i++) {
                uart_tx_program_putc(midi_out_pio, midi_out_sm, all_notes_off[i]);
            }
        }
    }

    // Flush USB MIDI
    if (tud_midi_mounted()) {
        tud_task();  // Process USB stack to ensure messages are sent
    }

    // Wait for UART MIDI to finish transmitting
    if (midi_out_initialized) {
        // Wait for TX FIFO to empty
        while (!pio_sm_is_tx_fifo_empty(midi_out_pio, midi_out_sm)) {
            tight_loop_contents();
        }
        // Wait for the last byte to finish transmitting (10 bits at 31.25 kbaud = ~0.32ms)
        // Add a small delay to ensure the last byte completes
        sleep_us(500);  // 0.5ms should be enough for the last byte
    }

    printf("MIDI PANIC: Complete\n");
}
