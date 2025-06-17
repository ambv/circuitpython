// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2018 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include "shared-bindings/usb_midi/PortIn.h"
#include "shared-module/usb_midi/PortIn.h"
#include "supervisor/shared/translate/translate.h"
#include "tusb.h"

// Conditionally include multicore support for denkioto_rin board
#ifdef BOARD_DENKIOTO_RIN_RP2040
#include "denkioto/multicore.h"

// External function to update filtered count
extern void denkioto_multicore_update_filtered_count(void);
#endif

// MIDI message definitions
#define MIDI_CLOCK 0xF8
#define MIDI_START 0xFA
#define MIDI_CONTINUE 0xFB
#define MIDI_STOP 0xFC

size_t common_hal_usb_midi_portin_read(usb_midi_portin_obj_t *self, uint8_t *data, size_t len, int *errcode) {
    #ifdef BOARD_DENKIOTO_RIN_RP2040
    // Check if Core 1 is running (MIDI clock processing is always active when Core 1 runs)
    if (denkioto_multicore_is_core1_running()) {
        // Core 1 is handling clocks - filter them from Python
        size_t total_read = 0;

        while (total_read < len && tud_midi_available()) {
            // Read one byte at a time to filter clock messages
            size_t bytes_read = tud_midi_stream_read(&data[total_read], 1);
            if (bytes_read > 0) {
                uint8_t midi_byte = data[total_read];

                // Filter out clock messages - handled by Core 1
                if (midi_byte == MIDI_CLOCK || midi_byte == MIDI_START ||
                    midi_byte == MIDI_CONTINUE || midi_byte == MIDI_STOP) {
                    // Track filtered count
                    denkioto_multicore_update_filtered_count();
                    // Don't increment total_read, effectively discarding this byte from Python
                    continue;
                }

                total_read += bytes_read;
            } else {
                break;  // No more data available
            }
        }

        return total_read;
    } else {
        // Normal operation - Core 1 not handling clocks
        return tud_midi_stream_read(data, len);
    }
    #else
    // Non-denkioto boards: normal operation
    return tud_midi_stream_read(data, len);
    #endif
}

uint32_t common_hal_usb_midi_portin_bytes_available(usb_midi_portin_obj_t *self) {
    return tud_midi_available();
}
