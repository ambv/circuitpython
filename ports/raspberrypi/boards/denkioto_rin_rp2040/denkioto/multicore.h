// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#ifndef DENKIOTO_MULTICORE_H
#define DENKIOTO_MULTICORE_H

#include <stdint.h>
#include <stdbool.h>

void denkioto_multicore_init(void);
void denkioto_multicore_start_core1(void);
int32_t denkioto_multicore_stop_core1(int where);
uint32_t denkioto_multicore_get_counter(void);
bool denkioto_multicore_is_core1_running(void);
void denkioto_multicore_reset_counter(void);

// RingVals accessor functions
int denkioto_multicore_get_ring_value(int ring, int index);
void denkioto_multicore_get_ring_values(int ring, int *values, int count);
void denkioto_multicore_clear_ring_values(int ring);

// Debug counter accessor functions
long denkioto_multicore_get_resync_count(int ring);
long denkioto_multicore_get_data_ready_count(int ring);

// MIDI clock accessor functions
uint32_t denkioto_multicore_get_midi_bpm_x1000(void);
uint8_t denkioto_multicore_get_transport_state(void);
uint8_t denkioto_multicore_get_clock_source(void);
uint32_t denkioto_multicore_get_clock_count(uint8_t source);
uint32_t denkioto_multicore_get_beat_count(uint8_t source);
void denkioto_multicore_set_clock_source_priority(uint8_t uart_priority, uint8_t usb_priority);

// Error monitoring
void denkioto_multicore_update_filtered_count(void);

// Debug functions
void denkioto_multicore_print_debug_stats(void);

// Access to TinyUSB atomic clock counters (declared in TinyUSB patch)
extern volatile uint32_t usb_midi_clock_count;
extern volatile uint32_t usb_midi_start_count;
extern volatile uint32_t usb_midi_continue_count;
extern volatile uint32_t usb_midi_stop_count;

static inline uint32_t denkioto_get_usb_midi_clock_count(void) {
    return __atomic_load_n(&usb_midi_clock_count, __ATOMIC_SEQ_CST);
}

static inline uint32_t denkioto_get_usb_midi_start_count(void) {
    return __atomic_load_n(&usb_midi_start_count, __ATOMIC_SEQ_CST);
}

static inline uint32_t denkioto_get_usb_midi_continue_count(void) {
    return __atomic_load_n(&usb_midi_continue_count, __ATOMIC_SEQ_CST);
}

static inline uint32_t denkioto_get_usb_midi_stop_count(void) {
    return __atomic_load_n(&usb_midi_stop_count, __ATOMIC_SEQ_CST);
}

// Reset TinyUSB atomic MIDI counters (called during board reset)
void denkioto_reset_usb_midi_counters(void);

// Flash write protection - lockout Core 1 during flash operations
void denkioto_multicore_pause(void);
void denkioto_multicore_resume(void);

// MIDI state accessor functions
uint8_t denkioto_multicore_get_note(uint8_t source, uint8_t channel, uint8_t note);
void denkioto_multicore_get_notes(uint8_t source, uint8_t channel, uint8_t *notes);
uint8_t denkioto_multicore_get_cc(uint8_t source, uint8_t channel, uint8_t cc);
void denkioto_multicore_get_cc_all(uint8_t source, uint8_t channel, uint8_t *values);
uint8_t denkioto_multicore_get_poly_pressure(uint8_t source, uint8_t channel, uint8_t note);
void denkioto_multicore_get_poly_pressure_all(uint8_t source, uint8_t channel, uint8_t *pressure);
uint16_t denkioto_multicore_get_pitch_bend(uint8_t source, uint8_t channel);
uint8_t denkioto_multicore_get_channel_pressure(uint8_t source, uint8_t channel);
uint8_t denkioto_multicore_get_program(uint8_t source, uint8_t channel);
void denkioto_multicore_get_note_status(uint8_t source, uint8_t channel, uint32_t *status);
uint64_t denkioto_multicore_get_last_update(uint8_t source, uint8_t channel);

#endif // DENKIOTO_MULTICORE_H
