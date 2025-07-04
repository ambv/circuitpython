// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 Łukasz Langa
//
// SPDX-License-Identifier: MIT

#include "neopixel_nb.h"
#include "bindings/rp2pio/StateMachine.h"
#include "common-hal/rp2pio/StateMachine.h"
#include "shared-bindings/microcontroller/__init__.h"
#include "supervisor/port.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"

// NeoPixel PIO program (same as in neopixel_write)
static const uint16_t neopixel_program_instructions[] = {
    0x6621,  // out x 1        side 0 [6]; Drive low
    0x1323,  // jmp !x do_zero side 1 [3]; Branch on bit, drive high
    0x1400,  // jmp  bitloop   side 1 [4]; Continue high for 1
    0xa442   // nop            side 0 [4]; Drive low for 0
};

static const pio_program_t neopixel_program = {
    .instructions = neopixel_program_instructions,
    .length = 4,
    .origin = -1,
    .pio_version = 0
};

// State for tracking two permanently allocated state machines
typedef struct {
    uint8_t sm;              // State machine number (1 or 2)
    PIO pio;                 // PIO instance (will be pio1)
    uint offset;             // Program offset in PIO memory
    int dma_channel;         // DMA channel for this SM
    bool busy;               // Currently transmitting
    uint32_t current_pin;    // Current pin being driven (0 if none)
    uint32_t start_time;     // Start time of current transmission (32-bit microseconds)
} nb_neopixel_channel_t;

static nb_neopixel_channel_t channels[2];
static bool nb_initialized = false;

// Helper for 32-bit timestamp comparisons with wrap-around handling
static inline bool time32_elapsed(uint32_t start, uint32_t now, uint32_t duration) {
    return (now - start) >= duration;
}

// Forward declarations
static void check_channels_complete(void);
static void configure_state_machine_for_pin(int channel_idx, uint32_t pin_number);
static void unconfigure_state_machine(int channel_idx);
static inline void start_channel_transmission(int channel_idx, uint32_t pin_number,
    const uint8_t *pixels, uint32_t num_bytes);

void denkioto_neopixel_nb_init(void) {
    if (nb_initialized) {
        return;
    }

    // Use PIO1 (PIO0 is used by touch rings)
    PIO pio = pio1;

    // Add program to PIO1 once (both SMs will use the same program)
    uint offset = pio_add_program(pio, &neopixel_program);

    // Initialize both channels with permanently allocated state machines
    for (int i = 0; i < 2; i++) {
        channels[i].sm = 3 - i;  // SM1 and SM2
        channels[i].pio = pio;
        channels[i].offset = offset;
        channels[i].busy = false;
        channels[i].current_pin = 0;
        channels[i].start_time = 0;

        // Claim the PIO state machine
        pio_sm_claim(pio, channels[i].sm);

        // Claim DMA channels
        channels[i].dma_channel = dma_claim_unused_channel(true);
        if (channels[i].dma_channel < 0) {
            // Clean up and fail
            pio_sm_unclaim(pio, channels[i].sm);
            for (int j = 0; j < i; j++) {
                if (channels[j].dma_channel >= 0) {
                    dma_channel_unclaim(channels[j].dma_channel);
                }
                pio_sm_unclaim(pio, channels[j].sm);
            }
            pio_remove_program(pio, &neopixel_program, offset);
            return;
        }

        // Initialize the state machine (but don't configure for any specific pin yet)
        pio_sm_init(pio, channels[i].sm, offset, NULL);

        // Configure state machine
        pio_sm_config c = pio_get_default_sm_config();
        sm_config_set_wrap(&c, offset, offset + neopixel_program.length - 1);

        // Set frequency to 12.8MHz for proper NeoPixel timing
        sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / 12800000);

        // Configure TX FIFO
        sm_config_set_out_shift(&c, false, true, 8);  // Shift left, autopull every 8 bits
        sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

        // Apply configuration
        pio_sm_set_config(pio, channels[i].sm, &c);

        // Don't enable yet - will enable when actually used
    }

    nb_initialized = true;
}

void denkioto_neopixel_nb_deinit(void) {
    if (!nb_initialized) {
        return;
    }

    // Wait for any in-progress transfers to complete
    while (denkioto_neopixel_nb_is_busy()) {
        check_channels_complete();
    }

    // Disable state machines and release resources
    for (int i = 0; i < 2; i++) {
        pio_sm_set_enabled(channels[i].pio, channels[i].sm, false);

        if (channels[i].current_pin != 0) {
            unconfigure_state_machine(i);
        }

        if (channels[i].dma_channel >= 0) {
            dma_channel_unclaim(channels[i].dma_channel);
        }

        // Unclaim the PIO state machine
        pio_sm_unclaim(channels[i].pio, channels[i].sm);
    }

    // Remove program from PIO
    pio_remove_program(channels[0].pio, &neopixel_program, channels[0].offset);

    nb_initialized = false;
}

static void configure_state_machine_for_pin(int channel_idx, uint32_t pin_number) {
    nb_neopixel_channel_t *ch = &channels[channel_idx];

    // Ensure state machine is disabled before reconfiguring
    pio_sm_set_enabled(ch->pio, ch->sm, false);

    // Configure the pin for PIO use
    pio_gpio_init(ch->pio, pin_number);

    // Configure sideset for this pin
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, ch->offset, ch->offset + neopixel_program.length - 1);
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / 12800000);
    sm_config_set_out_shift(&c, false, true, 8);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_sideset(&c, 1, false, false);
    sm_config_set_sideset_pins(&c, pin_number);

    // Apply configuration
    pio_sm_set_config(ch->pio, ch->sm, &c);

    // Now set the pin direction and initial state using masked operations
    uint32_t pin_mask = 1u << pin_number;
    pio_sm_set_pindirs_with_mask(ch->pio, ch->sm, pin_mask, pin_mask);  // Set as output
    pio_sm_set_pins_with_mask(ch->pio, ch->sm, 0, pin_mask);  // Set low initially

    // Clear FIFOs
    pio_sm_clear_fifos(ch->pio, ch->sm);

    ch->current_pin = pin_number;
}

static void unconfigure_state_machine(int channel_idx) {
    nb_neopixel_channel_t *ch = &channels[channel_idx];

    if (ch->current_pin == 0) {
        return;
    }

    // Reset the pin to standard GPIO
    gpio_init(ch->current_pin);
    gpio_put(ch->current_pin, 0);
    gpio_set_dir(ch->current_pin, GPIO_OUT);

    ch->current_pin = 0;
}

static void check_channels_complete(void) {
    uint32_t now = time_us_32();

    for (int i = 0; i < 2; i++) {
        nb_neopixel_channel_t *ch = &channels[i];

        if (!ch->busy) {
            continue;
        }

        // First check if enough time has passed
        // For 192 bytes (64 LEDs * 3 bytes) at 800kHz: 192 * 8 * 1.25us = 1920us
        if (!time32_elapsed(ch->start_time, now, 1920)) {
            continue;
        }

        // Only after enough time has passed, check hardware status
        bool dma_busy = dma_channel_is_busy(ch->dma_channel);
        bool tx_empty = pio_sm_is_tx_fifo_empty(ch->pio, ch->sm);

        // Check TXSTALL to ensure all bits have been shifted out
        uint32_t stall_mask = 1 << (PIO_FDEBUG_TXSTALL_LSB + ch->sm);
        bool tx_stalled = (ch->pio->fdebug & stall_mask) != 0;

        if (!dma_busy && tx_empty && tx_stalled) {
            // Transmission complete
            // pio_sm_set_enabled(ch->pio, ch->sm, false);

            // Clear the stall flag
            ch->pio->fdebug = stall_mask;

            // Unconfigure the pin
            unconfigure_state_machine(i);

            ch->busy = false;
        }
    }
}

static inline void start_channel_transmission(int channel_idx, uint32_t pin_number,
    const uint8_t *pixels, uint32_t num_bytes) {
    nb_neopixel_channel_t *ch = &channels[channel_idx];

    // Configure state machine for this pin
    configure_state_machine_for_pin(channel_idx, pin_number);

    // Configure DMA
    dma_channel_config c = dma_channel_get_default_config(ch->dma_channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_dreq(&c, pio_get_dreq(ch->pio, ch->sm, true));
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);

    dma_channel_configure(ch->dma_channel, &c,
        &ch->pio->txf[ch->sm],  // Write to PIO TX FIFO
        pixels,                  // Read from pixel buffer
        num_bytes,
        false);                  // Don't start yet

    // Clear TX FIFO and stall flag
    pio_sm_clear_fifos(ch->pio, ch->sm);
    ch->pio->fdebug = 1 << (PIO_FDEBUG_TXSTALL_LSB + ch->sm);

    // Enable state machine and start DMA
    pio_sm_set_enabled(ch->pio, ch->sm, true);
    dma_channel_start(ch->dma_channel);

    ch->busy = true;
    ch->start_time = time_us_32();
}

bool denkioto_neopixel_nb_write_dual(
    const digitalio_digitalinout_obj_t *pin1,
    const uint8_t *pixels1,
    uint32_t num_bytes1,
    const digitalio_digitalinout_obj_t *pin2,
    const uint8_t *pixels2,
    uint32_t num_bytes2) {

    if (!nb_initialized) {
        denkioto_neopixel_nb_init();
        if (!nb_initialized) {
            return false;  // Init failed
        }
    }

    // Check and clean up any completed transfers
    check_channels_complete();

    // If either channel is still busy, drop this request
    if (channels[0].busy || channels[1].busy) {
        return false;
    }

    // Start transmission on channel 1 if requested
    if (pin1 && pixels1 && num_bytes1 > 0) {
        start_channel_transmission(0, pin1->pin->number, pixels1, num_bytes1);
    }

    // Start transmission on channel 2 if requested
    if (pin2 && pixels2 && num_bytes2 > 0) {
        start_channel_transmission(1, pin2->pin->number, pixels2, num_bytes2);
    }

    return true;
}

bool denkioto_neopixel_nb_is_busy(void) {
    check_channels_complete();
    return channels[0].busy || channels[1].busy;
}
