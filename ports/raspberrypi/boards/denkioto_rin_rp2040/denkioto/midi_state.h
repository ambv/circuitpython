// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 Łukasz Langa
//
// SPDX-License-Identifier: MIT

#ifndef DENKIOTO_MIDI_STATE_H
#define DENKIOTO_MIDI_STATE_H

#include <stdint.h>
#include <string.h>

// MIDI channel state structure shared between UART and USB implementations
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

// Helper functions for MIDI state updates
static inline void update_note_status(midi_channel_t *state, uint8_t channel, uint8_t note) {
    // Update the bit in note_status for this note
    uint8_t array_index = note / 32;  // Which uint32_t (0-3)
    uint8_t bit_index = note % 32;     // Which bit within that uint32_t

    if (state[channel].notes[note] > 0) {
        // Note is on - set the bit
        state[channel].note_status[array_index] |= (1U << bit_index);
    } else {
        // Note is off - clear the bit
        state[channel].note_status[array_index] &= ~(1U << bit_index);
    }
}

// Helper function to clear all notes for a channel
static inline void clear_all_notes(midi_channel_t *state, uint8_t channel) {
    for (int note = 0; note < 128; note++) {
        state[channel].notes[note] = 0;
        update_note_status(state, channel, note);
    }
}

// Helper function to reset controllers according to MIDI RP-015
static inline void reset_all_controllers(midi_channel_t *state, uint8_t channel) {
    // Set Expression (#11) to 127
    state[channel].cc[11] = 127;

    // Set Modulation (#1) to 0
    state[channel].cc[1] = 0;

    // Set Pedals (#64, #65, #66, #67) to 0
    state[channel].cc[64] = 0;  // Sustain
    state[channel].cc[65] = 0;  // Portamento
    state[channel].cc[66] = 0;  // Sostenuto
    state[channel].cc[67] = 0;  // Soft Pedal

    // Set Registered and Non-registered parameter number LSB and MSB (#98-#101) to null (127)
    state[channel].cc[98] = 127;   // NRPN LSB
    state[channel].cc[99] = 127;   // NRPN MSB
    state[channel].cc[100] = 127;  // RPN LSB
    state[channel].cc[101] = 127;  // RPN MSB

    // Set pitch bender to center (8192)
    state[channel].pitch_bend = 8192;

    // Reset channel pressure to 0
    state[channel].channel_pressure = 0;

    // Reset polyphonic pressure for all notes to 0
    memset((void *)state[channel].poly_pressure, 0, 128);
}

// Process MIDI channel message and update state
static inline void process_midi_message(midi_channel_t *state, uint8_t status, uint8_t data1, uint8_t data2, uint64_t timestamp) {
    uint8_t msg_type = status & 0xF0;
    uint8_t channel = status & 0x0F;

    switch (msg_type) {
        case 0x80:  // Note Off
            state[channel].notes[data1] = 0;
            update_note_status(state, channel, data1);
            break;

        case 0x90:  // Note On
            state[channel].notes[data1] = (data2 == 0) ? 0 : data2;
            update_note_status(state, channel, data1);
            break;

        case 0xA0:  // Polyphonic Aftertouch
            state[channel].poly_pressure[data1] = data2;
            break;

        case 0xB0:  // Control Change
            // Handle channel mode messages
            if (data1 >= 120 && data1 <= 127) {
                switch (data1) {
                    case 120:  // All Sound Off
                        clear_all_notes(state, channel);
                        break;
                    case 121:  // Reset All Controllers
                        reset_all_controllers(state, channel);
                        break;
                    case 123:  // All Notes Off
                        clear_all_notes(state, channel);
                        break;
                        // Other channel mode messages (122, 124-127) are ignored
                        // as they don't affect stored state
                }
            } else {
                // Regular control change
                state[channel].cc[data1] = data2;
            }
            break;

        case 0xC0:  // Program Change
            state[channel].program = data1;
            break;

        case 0xD0:  // Channel Pressure
            state[channel].channel_pressure = data1;
            break;

        case 0xE0:  // Pitch Bend
            state[channel].pitch_bend = data1 | (data2 << 7);
            break;
    }

    state[channel].last_update = timestamp;
}

#endif // DENKIOTO_MIDI_STATE_H
