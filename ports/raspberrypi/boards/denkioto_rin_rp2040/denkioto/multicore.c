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
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "denkioto/uart_rx.pio.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "hardware/uart.h"
#include "hardware/timer.h"
#include "tusb.h"

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

// No longer need TinyUSB internal structures - using atomic counters instead

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

// MIDI source definitions
#define MIDI_SOURCE_NONE 0
#define MIDI_SOURCE_UART 1
#define MIDI_SOURCE_USB 2

// Two-stage filter constants for BPM stability
#define BPM_SHORT_WINDOW 8   // For detecting tempo changes (8 clocks = 1/3 beat)
#define BPM_LONG_WINDOW 24   // For stable output (24 clocks = 1 beat)

// MIDI Clock state structure
typedef struct {
    volatile uint32_t current_bpm_x1000;     // BPM * 1000 for precision
    volatile uint8_t transport_state;        // 0=stop, 1=play, 2=pause
    volatile uint8_t active_source;          // 0=none, 1=uart, 2=usb
    volatile uint64_t last_clock_timestamp_uart;  // UART-specific timestamp
    volatile uint64_t last_clock_timestamp_usb;   // USB-specific timestamp
    volatile uint32_t clock_count[3];        // Clock count per source [none, uart, usb]
    volatile uint32_t beat_count[3];         // Beat count per source [none, uart, usb] (6 clocks = 1 beat)
    volatile uint8_t source_priority[2];     // [uart=1, usb=2] priority order
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
    midi_clock_state.active_source = MIDI_SOURCE_NONE;
    midi_clock_state.last_clock_timestamp_uart = 0;
    midi_clock_state.last_clock_timestamp_usb = 0;
    midi_clock_state.clock_count[MIDI_SOURCE_NONE] = 0;
    midi_clock_state.clock_count[MIDI_SOURCE_UART] = 0;
    midi_clock_state.clock_count[MIDI_SOURCE_USB] = 0;
    midi_clock_state.beat_count[MIDI_SOURCE_NONE] = 0;
    midi_clock_state.beat_count[MIDI_SOURCE_UART] = 0;
    midi_clock_state.beat_count[MIDI_SOURCE_USB] = 0;
    midi_clock_state.source_priority[0] = 1;       // UART first
    midi_clock_state.source_priority[1] = 2;       // USB second
    midi_clock_state.last_source_seen[MIDI_SOURCE_NONE] = 0;
    midi_clock_state.last_source_seen[MIDI_SOURCE_UART] = 0;
    midi_clock_state.last_source_seen[MIDI_SOURCE_USB] = 0;
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

static midi_spp_parser_state_t spp_parsers[3] = {0}; // Indexed by MIDI_SOURCE_*

// USB MIDI clock tracking state
static struct {
    uint32_t last_clock_count;
    uint32_t last_start_count;
    uint32_t last_continue_count;
    uint32_t last_stop_count;
} usb_midi_tracking = {0};

// MIDI channel state structure
typedef struct {
    // Note velocities (0 = off, 1-127 = on with velocity)
    volatile uint8_t notes[128];

    // Control change values
    volatile uint8_t cc[128];

    // Polyphonic aftertouch (per-note pressure)
    volatile uint8_t poly_pressure[128];

    // Pitch bend (14-bit value: 0-16383, center = 8192)
    volatile uint16_t pitch_bend;

    // Channel pressure (aftertouch)
    volatile uint8_t channel_pressure;

    // Program change
    volatile uint8_t program;

    // Note status tracking for efficient Python polling
    // Each bit represents one note: note_status[0] bits 0-31 = notes 0-31, etc.
    volatile uint32_t note_status[4];  // 128 bits total, one per note

    // Timestamp of last update
    volatile uint64_t last_update;
} midi_channel_t;

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
static inline void process_midi_clock_uart(uint64_t timestamp);
static inline void process_transport_message_uart(uint8_t message, uint64_t timestamp);
static void process_spp_message(uint16_t spp_position, uint8_t source);
static void process_midi_clock_usb(uint8_t message, uint64_t timestamp);
static void setup_uart_midi(void);
static void midi_uart_irq_handler(void);
static void core1_usb_midi_poll(void);

// Helper functions for MIDI state updates
static inline void update_note_status(uint8_t channel, uint8_t note) {
    // Update the bit in note_status for this note
    uint8_t array_index = note / 32;  // Which uint32_t (0-3)
    uint8_t bit_index = note % 32;     // Which bit within that uint32_t

    if (uart_midi_state[channel].notes[note] > 0) {
        // Note is on - set the bit
        uart_midi_state[channel].note_status[array_index] |= (1U << bit_index);
    } else {
        // Note is off - clear the bit
        uart_midi_state[channel].note_status[array_index] &= ~(1U << bit_index);
    }
}

// Helper function to clear all notes for a channel
static void clear_all_notes(uint8_t channel) {
    for (int note = 0; note < 128; note++) {
        uart_midi_state[channel].notes[note] = 0;
        update_note_status(channel, note);
    }
}

// Helper function to reset controllers according to MIDI RP-015
static void reset_all_controllers(uint8_t channel) {
    // Set Expression (#11) to 127
    uart_midi_state[channel].cc[11] = 127;

    // Set Modulation (#1) to 0
    uart_midi_state[channel].cc[1] = 0;

    // Set Pedals (#64, #65, #66, #67) to 0
    uart_midi_state[channel].cc[64] = 0;  // Sustain
    uart_midi_state[channel].cc[65] = 0;  // Portamento
    uart_midi_state[channel].cc[66] = 0;  // Sostenuto
    uart_midi_state[channel].cc[67] = 0;  // Soft Pedal

    // Set Registered and Non-registered parameter number LSB and MSB (#98-#101) to null (127)
    uart_midi_state[channel].cc[98] = 127;   // NRPN LSB
    uart_midi_state[channel].cc[99] = 127;   // NRPN MSB
    uart_midi_state[channel].cc[100] = 127;  // RPN LSB
    uart_midi_state[channel].cc[101] = 127;  // RPN MSB

    // Set pitch bender to center (8192)
    uart_midi_state[channel].pitch_bend = 8192;

    // Reset channel pressure to 0
    uart_midi_state[channel].channel_pressure = 0;

    // Reset polyphonic pressure for all notes to 0
    memset((void *)uart_midi_state[channel].poly_pressure, 0, 128);
}

// Process complete MIDI messages
static void process_midi_message(uint8_t status, uint8_t data1, uint8_t data2, uint64_t timestamp) {
    uint8_t msg_type = status & 0xF0;
    uint8_t channel = status & 0x0F;

    switch (msg_type) {
        case 0x80:  // Note Off
            uart_midi_state[channel].notes[data1] = 0;
            update_note_status(channel, data1);
            break;

        case 0x90:  // Note On
            uart_midi_state[channel].notes[data1] = (data2 == 0) ? 0 : data2;
            update_note_status(channel, data1);
            break;

        case 0xA0:  // Polyphonic Aftertouch
            uart_midi_state[channel].poly_pressure[data1] = data2;
            break;

        case 0xB0:  // Control Change
            // Handle channel mode messages
            if (data1 >= 120 && data1 <= 127) {
                switch (data1) {
                    case 120:  // All Sound Off
                        clear_all_notes(channel);
                        break;
                    case 121:  // Reset All Controllers
                        reset_all_controllers(channel);
                        break;
                    case 123:  // All Notes Off
                        clear_all_notes(channel);
                        break;
                        // Other channel mode messages (122, 124-127) are ignored
                        // as they don't affect stored state
                }
            } else {
                // Regular control change
                uart_midi_state[channel].cc[data1] = data2;
            }
            break;

        case 0xC0:  // Program Change
            uart_midi_state[channel].program = data1;
            break;

        case 0xD0:  // Channel Pressure
            uart_midi_state[channel].channel_pressure = data1;
            break;

        case 0xE0:  // Pitch Bend
            uart_midi_state[channel].pitch_bend = data1 | (data2 << 7);
            break;
    }

    uart_midi_state[channel].last_update = timestamp;
}

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
    // Check if UART0 has data available
    while (uart_is_readable(uart0)) {
        uint64_t timestamp = time_us_64();  // Microsecond precision
        uint8_t byte = uart_getc(uart0);

        // Handle System Real Time messages (can occur anywhere)
        if (byte == MIDI_CLOCK) {
            process_midi_clock_uart(timestamp);
            continue;
        } else if (byte == MIDI_START || byte == MIDI_CONTINUE || byte == MIDI_STOP) {
            process_transport_message_uart(byte, timestamp);
            continue;
        }

        // Handle System Common messages
        if (byte == MIDI_SPP) {
            spp_parsers[MIDI_SOURCE_UART].state = 1;  // Expecting LSB next
            uart_midi_parser.state = 0;  // Reset channel message parser
            continue;
        } else if (spp_parsers[MIDI_SOURCE_UART].state > 0) {
            // Continue SPP parsing
            if ((byte & 0x80) == 0) {  // Data byte
                if (spp_parsers[MIDI_SOURCE_UART].state == 1) {
                    spp_parsers[MIDI_SOURCE_UART].spp_lsb = byte;
                    spp_parsers[MIDI_SOURCE_UART].state = 2;
                } else if (spp_parsers[MIDI_SOURCE_UART].state == 2) {
                    spp_parsers[MIDI_SOURCE_UART].spp_msb = byte;
                    uint16_t spp_position = spp_parsers[MIDI_SOURCE_UART].spp_lsb | (spp_parsers[MIDI_SOURCE_UART].spp_msb << 7);
                    process_spp_message(spp_position, MIDI_SOURCE_UART);
                    spp_parsers[MIDI_SOURCE_UART].state = 0;
                }
            } else {
                // Status byte resets SPP parser
                spp_parsers[MIDI_SOURCE_UART].state = 0;
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
                    process_midi_message(uart_midi_parser.status, uart_midi_parser.data1, 0, timestamp);
                    uart_midi_parser.received_bytes = 0;  // Ready for next message with running status
                }
            } else if (uart_midi_parser.received_bytes == 1 && uart_midi_parser.expected_bytes == 2) {
                // Second data byte
                process_midi_message(uart_midi_parser.status, uart_midi_parser.data1, byte, timestamp);
                uart_midi_parser.received_bytes = 0;  // Ready for next message with running status
            }
        }
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

static inline void process_midi_clock_uart(uint64_t timestamp) {
    // Calculate BPM from clock interval (24 clocks per quarter note)
    if (midi_clock_state.last_clock_timestamp_uart > 0) {
        uint64_t interval = timestamp - midi_clock_state.last_clock_timestamp_uart;
        update_bpm_with_interval(interval, MIDI_SOURCE_UART);
    }

    midi_clock_state.last_clock_timestamp_uart = timestamp;
    midi_clock_state.last_source_seen[MIDI_SOURCE_UART] = timestamp;
    midi_clock_state.clock_count[MIDI_SOURCE_UART]++;
    // Update beat count (6 clocks = 1 beat)
    if (midi_clock_state.clock_count[MIDI_SOURCE_UART] % 6 == 0) {
        midi_clock_state.beat_count[MIDI_SOURCE_UART]++;
    }
    midi_clock_state.core1_clock_count++;

    // Update active source if needed
    if (midi_clock_state.active_source != MIDI_SOURCE_UART) {
        midi_clock_state.active_source = MIDI_SOURCE_UART;
        // Reset filter state when switching to UART
        midi_clock_state.intervals_filled = 0;
        midi_clock_state.short_index = 0;
        midi_clock_state.long_index = 0;
    }
}

// Process transport messages (Start/Stop/Continue)
static inline void process_transport_message_uart(uint8_t message, uint64_t timestamp) {
    switch (message) {
        case MIDI_START:
            midi_clock_state.transport_state = TRANSPORT_PLAYING;
            midi_clock_state.clock_count[MIDI_SOURCE_NONE] = 0;
            midi_clock_state.clock_count[MIDI_SOURCE_UART] = 0;
            midi_clock_state.clock_count[MIDI_SOURCE_USB] = 0;
            midi_clock_state.beat_count[MIDI_SOURCE_NONE] = 0;
            midi_clock_state.beat_count[MIDI_SOURCE_UART] = 0;
            midi_clock_state.beat_count[MIDI_SOURCE_USB] = 0;
            midi_clock_state.last_clock_timestamp_uart = 0;  // Reset for new BPM calculation
            midi_clock_state.last_clock_timestamp_usb = 0;   // Reset for new BPM calculation
            // Reset filter state for fresh start
            midi_clock_state.intervals_filled = 0;
            midi_clock_state.short_index = 0;
            midi_clock_state.long_index = 0;
            break;
        case MIDI_STOP:
            midi_clock_state.transport_state = TRANSPORT_STOPPED;
            break;
        case MIDI_CONTINUE:
            midi_clock_state.transport_state = TRANSPORT_PLAYING;
            break;
    }
    midi_clock_state.last_source_seen[MIDI_SOURCE_UART] = timestamp;
}

// Process Song Position Pointer message
static void process_spp_message(uint16_t spp_position, uint8_t source) {
    // SPP position is in MIDI beats (1/16 notes)
    // 1 MIDI beat = 6 MIDI clocks
    // So clock_count = spp_position * 6
    uint32_t clock_position = spp_position * 6;
    uint32_t beat_position = spp_position;

    // Update clock and beat counts for the source
    if (source <= 2) {
        midi_clock_state.clock_count[source] = clock_position;
        midi_clock_state.beat_count[source] = beat_position;
    }
}

// Setup UART for MIDI
__attribute__((used))
static void setup_uart_midi(void) {
    // First, ensure UART0 is deinitialized in case CircuitPython was using it
    uart_deinit(uart0);

    // Initialize UART0 for MIDI
    uart_init(uart0, MIDI_BAUDRATE);

    // Set TX and RX pins
    gpio_set_function(TX_PIN_MIDI, GPIO_FUNC_UART);
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

// Process MIDI clock from USB source
static void __not_in_flash_func(process_midi_clock_usb)(uint8_t message, uint64_t timestamp) {
    if (message == MIDI_CLOCK) {
        // Calculate BPM from clock interval (24 clocks per quarter note)
        if (midi_clock_state.last_clock_timestamp_usb > 0) {
            uint64_t interval = timestamp - midi_clock_state.last_clock_timestamp_usb;
            update_bpm_with_interval(interval, MIDI_SOURCE_USB);
        }

        midi_clock_state.last_clock_timestamp_usb = timestamp;
        midi_clock_state.last_source_seen[MIDI_SOURCE_USB] = timestamp;
        midi_clock_state.clock_count[MIDI_SOURCE_USB]++;
        // Update beat count (6 clocks = 1 beat)
        if (midi_clock_state.clock_count[MIDI_SOURCE_USB] % 6 == 0) {
            midi_clock_state.beat_count[MIDI_SOURCE_USB]++;
        }
        midi_clock_state.core1_clock_count++;

        // Update active source based on priority
        if (midi_clock_state.active_source != MIDI_SOURCE_USB) {
            // Check if higher priority source (UART) is still active
            uint64_t uart_last_seen = midi_clock_state.last_source_seen[MIDI_SOURCE_UART];
            if (timestamp - uart_last_seen > CLOCK_SOURCE_TIMEOUT_US) {
                // UART has timed out, USB can take over
                midi_clock_state.active_source = MIDI_SOURCE_USB;
                // Reset filter state when switching to USB
                midi_clock_state.intervals_filled = 0;
                midi_clock_state.short_index = 0;
                midi_clock_state.long_index = 0;
            }
        }
    } else if (message == MIDI_START || message == MIDI_CONTINUE || message == MIDI_STOP) {
        // Update transport state
        switch (message) {
            case MIDI_START:
                midi_clock_state.transport_state = TRANSPORT_PLAYING;
                midi_clock_state.clock_count[MIDI_SOURCE_NONE] = 0;
                midi_clock_state.clock_count[MIDI_SOURCE_UART] = 0;
                midi_clock_state.clock_count[MIDI_SOURCE_USB] = 0;
                midi_clock_state.beat_count[MIDI_SOURCE_NONE] = 0;
                midi_clock_state.beat_count[MIDI_SOURCE_UART] = 0;
                midi_clock_state.beat_count[MIDI_SOURCE_USB] = 0;
                midi_clock_state.last_clock_timestamp_uart = 0;  // Reset for new BPM calculation
                midi_clock_state.last_clock_timestamp_usb = 0;   // Reset for new BPM calculation
                // Reset filter state for fresh start
                midi_clock_state.intervals_filled = 0;
                midi_clock_state.short_index = 0;
                midi_clock_state.long_index = 0;
                break;
            case MIDI_STOP:
                midi_clock_state.transport_state = TRANSPORT_STOPPED;
                break;
            case MIDI_CONTINUE:
                midi_clock_state.transport_state = TRANSPORT_PLAYING;
                break;
        }
        midi_clock_state.last_source_seen[MIDI_SOURCE_USB] = timestamp;
    }
}

// Core 1 USB MIDI clock monitoring using atomic counters
static void core1_usb_midi_poll(void) {
    uint64_t now = time_us_64();
    uint64_t last_poll_time = last_usb_poll_time;
    last_usb_poll_time = now;

    // Check if USB is enumerated
    if (!tud_ready()) {
        return;
    }

    // Check for new clock messages using atomic counters
    // TinyUSB increments these when it receives clock messages
    uint32_t current_clock_count = denkioto_get_usb_midi_clock_count();
    uint32_t current_start_count = denkioto_get_usb_midi_start_count();
    uint32_t current_continue_count = denkioto_get_usb_midi_continue_count();
    uint32_t current_stop_count = denkioto_get_usb_midi_stop_count();

    // Process multiple clock messages with smeared timestamps
    int32_t clock_delta = current_clock_count - usb_midi_tracking.last_clock_count;
    if (clock_delta > 0) {
        if (clock_delta == 1) {
            // Single clock: use current timestamp
            process_midi_clock_usb(MIDI_CLOCK, now);
        } else {
            // Multiple clocks: smear timestamps evenly over the polling interval
            uint64_t time_per_pulse = (now - last_poll_time) / clock_delta;
            for (uint32_t i = 0; i < (uint32_t)clock_delta; i++) {
                uint64_t synthetic_timestamp = last_poll_time + (i + 1) * time_per_pulse;
                process_midi_clock_usb(MIDI_CLOCK, synthetic_timestamp);
            }
        }
        usb_midi_tracking.last_clock_count = current_clock_count;
    } else {
        // If last_clock_count is greater, it means either core0 was reset or the counter overflowed.
        // Therefore, restore sanity.
        usb_midi_tracking.last_clock_count = current_clock_count;
    }

    // Process transport messages (these are infrequent, use current timestamp)
    if (current_start_count > usb_midi_tracking.last_start_count) {
        process_midi_clock_usb(MIDI_START, now);
        usb_midi_tracking.last_start_count = current_start_count;
    }

    if (current_continue_count > usb_midi_tracking.last_continue_count) {
        process_midi_clock_usb(MIDI_CONTINUE, now);
        usb_midi_tracking.last_continue_count = current_continue_count;
    }

    if (current_stop_count > usb_midi_tracking.last_stop_count) {
        process_midi_clock_usb(MIDI_STOP, now);
        usb_midi_tracking.last_stop_count = current_stop_count;
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
    printf("Clock count UART: %lu, USB: %lu\n",
        (unsigned long)midi_clock_state.clock_count[MIDI_SOURCE_UART],
        (unsigned long)midi_clock_state.clock_count[MIDI_SOURCE_USB]);
    printf("Beat count UART: %lu, USB: %lu\n",
        (unsigned long)midi_clock_state.beat_count[MIDI_SOURCE_UART],
        (unsigned long)midi_clock_state.beat_count[MIDI_SOURCE_USB]);
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

    // Check for source timeout
    uint64_t now = time_us_64();
    if (source > 0 && source <= 2) {
        uint64_t last_seen = __atomic_load_n(&midi_clock_state.last_source_seen[source], __ATOMIC_SEQ_CST);
        if (now - last_seen > CLOCK_SOURCE_TIMEOUT_US) {
            // Source has timed out
            __atomic_store_n(&midi_clock_state.active_source, 0, __ATOMIC_SEQ_CST);
            return 0;  // No active source
        }
    }

    return source;
}

uint32_t denkioto_multicore_get_clock_count(uint8_t source) {
    if (source > 2) {
        return 0;  // Invalid source
    }
    return __atomic_load_n(&midi_clock_state.clock_count[source], __ATOMIC_SEQ_CST);
}

uint32_t denkioto_multicore_get_beat_count(uint8_t source) {
    if (source > 2) {
        return 0;  // Invalid source
    }
    return __atomic_load_n(&midi_clock_state.beat_count[source], __ATOMIC_SEQ_CST);
}

void denkioto_multicore_set_clock_source_priority(uint8_t uart_priority, uint8_t usb_priority) {
    // Ensure priorities are valid (1 or 2) and different
    if ((uart_priority == 1 || uart_priority == 2) &&
        (usb_priority == 1 || usb_priority == 2) &&
        uart_priority != usb_priority) {
        midi_clock_state.source_priority[0] = uart_priority;
        midi_clock_state.source_priority[1] = usb_priority;
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
    if (source != MIDI_SOURCE_UART || channel >= 16 || note >= 128) {
        return 0;  // USB MIDI not implemented yet
    }
    return uart_midi_state[channel].notes[note];
}

void denkioto_multicore_get_notes(uint8_t source, uint8_t channel, uint8_t *notes) {
    if (source != MIDI_SOURCE_UART || channel >= 16 || !notes) {
        if (notes) {
            memset(notes, 0, 128);
        }
        return;
    }
    memcpy(notes, (void *)uart_midi_state[channel].notes, 128);
}

uint8_t denkioto_multicore_get_cc(uint8_t source, uint8_t channel, uint8_t cc) {
    if (source != MIDI_SOURCE_UART || channel >= 16 || cc >= 128) {
        return 0;  // USB MIDI not implemented yet
    }
    return uart_midi_state[channel].cc[cc];
}

void denkioto_multicore_get_cc_all(uint8_t source, uint8_t channel, uint8_t *values) {
    if (source != MIDI_SOURCE_UART || channel >= 16 || !values) {
        if (values) {
            memset(values, 0, 128);
        }
        return;
    }
    memcpy(values, (void *)uart_midi_state[channel].cc, 128);
}

uint8_t denkioto_multicore_get_poly_pressure(uint8_t source, uint8_t channel, uint8_t note) {
    if (source != MIDI_SOURCE_UART || channel >= 16 || note >= 128) {
        return 0;  // USB MIDI not implemented yet
    }
    return uart_midi_state[channel].poly_pressure[note];
}

void denkioto_multicore_get_poly_pressure_all(uint8_t source, uint8_t channel, uint8_t *pressure) {
    if (source != MIDI_SOURCE_UART || channel >= 16 || !pressure) {
        if (pressure) {
            memset(pressure, 0, 128);
        }
        return;
    }
    memcpy(pressure, (void *)uart_midi_state[channel].poly_pressure, 128);
}

uint16_t denkioto_multicore_get_pitch_bend(uint8_t source, uint8_t channel) {
    if (source != MIDI_SOURCE_UART || channel >= 16) {
        return 8192;  // Center value, USB MIDI not implemented yet
    }
    return uart_midi_state[channel].pitch_bend;
}

uint8_t denkioto_multicore_get_channel_pressure(uint8_t source, uint8_t channel) {
    if (source != MIDI_SOURCE_UART || channel >= 16) {
        return 0;  // USB MIDI not implemented yet
    }
    return uart_midi_state[channel].channel_pressure;
}

uint8_t denkioto_multicore_get_program(uint8_t source, uint8_t channel) {
    if (source != MIDI_SOURCE_UART || channel >= 16) {
        return 0;  // USB MIDI not implemented yet
    }
    return uart_midi_state[channel].program;
}

void denkioto_multicore_get_note_status(uint8_t source, uint8_t channel, uint32_t *status) {
    if (source != MIDI_SOURCE_UART || channel >= 16 || !status) {
        if (status) {
            memset(status, 0, 4 * sizeof(uint32_t));
        }
        return;  // USB MIDI not implemented yet
    }
    memcpy(status, (void *)uart_midi_state[channel].note_status, 4 * sizeof(uint32_t));
}

uint64_t denkioto_multicore_get_last_update(uint8_t source, uint8_t channel) {
    if (source != MIDI_SOURCE_UART || channel >= 16) {
        return 0;  // USB MIDI not implemented yet
    }
    return uart_midi_state[channel].last_update;
}
