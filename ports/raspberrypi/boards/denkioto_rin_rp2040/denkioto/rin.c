// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include "py/obj.h"
#include "py/runtime.h"
#include "py/builtin.h"

#include "multicore.h"

// Define MIDI source constants for Python module
#define MIDI_SOURCE_NONE 0
#define MIDI_SOURCE_UART 1
#define MIDI_SOURCE_USB 2

//| """Multicore functionality for the denkioto_rin_rp2040 board.
//|
//| This module provides access to the RP2040's second core (core1) functionality.
//| Core1 runs independently of the main CircuitPython interpreter and can be used
//| for background tasks or real-time processing.
//| """

static mp_obj_t denkioto_rin_start(void) {
    // | def start() -> None:
    // |     """Start core1 execution.
    // |
    // |     Core1 will begin running an infinite loop that increments an internal counter.
    // |     If core1 is already running, this function has no effect.
    // |     """
    // |     ...
    // |
    denkioto_multicore_start_core1();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(denkioto_rin_start_obj, denkioto_rin_start);

static mp_obj_t denkioto_rin_stop(void) {
    // | def stop() -> int:
    // |     """Stop core1 execution.
    // |
    // |     Core1 will be stopped and reset. If core1 is not running, this function
    // |     has no effect. Returns the number of cycles of waiting it took to stop core1.
    // |     In case core1 was not running, it will return -1.
    // |     """
    // |     ...
    // |
    return mp_obj_new_int(denkioto_multicore_stop_core1(0));
}
static MP_DEFINE_CONST_FUN_OBJ_0(denkioto_rin_stop_obj, denkioto_rin_stop);

static mp_obj_t denkioto_rin_get_counter_func(void) {
    // | def get_counter() -> int:
    // |     """Get the current counter value from core1.
    // |
    // |     Returns the current value of the counter that core1 is incrementing.
    // |     This value is read atomically, so it's safe to call from core0 while
    // |     core1 is running.
    // |
    // |     :return: The current counter value
    // |     """
    // |     ...
    // |
    return mp_obj_new_int_from_uint(denkioto_multicore_get_counter());
}
static MP_DEFINE_CONST_FUN_OBJ_0(denkioto_rin_get_counter_obj, denkioto_rin_get_counter_func);

static mp_obj_t denkioto_rin_is_running_func(void) {
    // | def is_running() -> bool:
    // |     """Check if core1 is currently running.
    // |
    // |     :return: True if core1 is running, False otherwise
    // |     """
    // |     ...
    // |
    return mp_obj_new_bool(denkioto_multicore_is_core1_running());
}
static MP_DEFINE_CONST_FUN_OBJ_0(denkioto_rin_is_running_obj, denkioto_rin_is_running_func);

static mp_obj_t denkioto_rin_reset_counter_func(void) {
    // | def reset_counter() -> None:
    // |     """Reset the counter to zero.
    // |
    // |     This operation is atomic and can be called safely while core1 is running.
    // |     """
    // |     ...
    // |
    denkioto_multicore_reset_counter();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(denkioto_rin_reset_counter_obj, denkioto_rin_reset_counter_func);

static mp_obj_t denkioto_rin_print_debug_stats_func(void) {
    // | def print_debug_stats() -> None:
    // |     """Print comprehensive debug statistics for Core 1 and MIDI processing.
    // |
    // |     Displays information about:
    // |     - Core 1 running state and counter
    // |     - IRQ status for all enabled interrupts
    // |     - MIDI clock status including BPM, transport state, and source
    // |     - Touch ring status with active sensors and debug counters
    // |
    // |     This is useful for debugging multicore and MIDI functionality.
    // |     """
    // |     ...
    // |
    denkioto_multicore_print_debug_stats();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(denkioto_rin_print_debug_stats_obj, denkioto_rin_print_debug_stats_func);


static mp_obj_t denkioto_rin_get_midi_bpm_func(void) {
    // | def get_midi_bpm() -> int:
    // |     """Get the current MIDI tempo in BPM * 1000.
    // |
    // |     The BPM is calculated from incoming MIDI clock messages (24 clocks per quarter note).
    // |     The value is returned multiplied by 1000 for precision (e.g., 120000 = 120.0 BPM).
    // |
    // |     :return: Current BPM * 1000, or 120000 if no clock detected
    // |     """
    // |     ...
    // |
    return mp_obj_new_int_from_uint(denkioto_multicore_get_midi_bpm_x1000());
}
static MP_DEFINE_CONST_FUN_OBJ_0(denkioto_rin_get_midi_bpm_obj, denkioto_rin_get_midi_bpm_func);

static mp_obj_t denkioto_rin_get_transport_state_func(void) {
    // | def get_transport_state() -> int:
    // |     """Get the current MIDI transport state.
    // |
    // |     :return: 0=stopped, 1=playing, 2=paused
    // |     """
    // |     ...
    // |
    return mp_obj_new_int(denkioto_multicore_get_transport_state());
}
static MP_DEFINE_CONST_FUN_OBJ_0(denkioto_rin_get_transport_state_obj, denkioto_rin_get_transport_state_func);

static mp_obj_t denkioto_rin_get_clock_source_func(void) {
    // | def get_clock_source() -> int | None:
    // |     """Get the currently active MIDI clock source.
    // |
    // |     :return: MIDI_UART, MIDI_USB, or None
    // |     """
    // |     ...
    // |
    uint8_t source = denkioto_multicore_get_clock_source();
    if (source == 0) {
        return mp_const_none;
    }
    return mp_obj_new_int(source);
}
static MP_DEFINE_CONST_FUN_OBJ_0(denkioto_rin_get_clock_source_obj, denkioto_rin_get_clock_source_func);



static mp_obj_t denkioto_rin_get_clock_count_func(mp_obj_t source_obj) {
    // | def get_clock_count(source: int) -> int:
    // |     """Get the MIDI clock count for a specific source.
    // |
    // |     :param source: MIDI source (MIDI_UART or MIDI_USB)
    // |     :return: Clock count since last transport start
    // |     """
    // |     ...
    // |
    mp_int_t source = mp_obj_get_int(source_obj);
    if (source < 0 || source > 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("Invalid MIDI source"));
    }
    return mp_obj_new_int(denkioto_multicore_get_clock_count((uint8_t)source));
}
static MP_DEFINE_CONST_FUN_OBJ_1(denkioto_rin_get_clock_count_obj, denkioto_rin_get_clock_count_func);

static mp_obj_t denkioto_rin_get_beat_count_func(mp_obj_t source_obj) {
    // | def get_beat_count(source: int) -> int:
    // |     """Get the MIDI beat count for a specific source.
    // |
    // |     :param source: MIDI source (MIDI_UART or MIDI_USB)
    // |     :return: Beat count since last transport start (6 clocks = 1 beat)
    // |     """
    // |     ...
    // |
    mp_int_t source = mp_obj_get_int(source_obj);
    if (source < 0 || source > 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("Invalid MIDI source"));
    }
    return mp_obj_new_int(denkioto_multicore_get_beat_count((uint8_t)source));
}
static MP_DEFINE_CONST_FUN_OBJ_1(denkioto_rin_get_beat_count_obj, denkioto_rin_get_beat_count_func);

static mp_obj_t denkioto_rin_set_clock_source_priority_func(mp_obj_t sources_obj) {
    // | def set_clock_source_priority(sources: list[int]) -> None:
    // |     """Set the priority order for MIDI clock sources.
    // |
    // |     :param sources: List of source constants in priority order, e.g. [MIDI_UART, MIDI_USB]
    // |     """
    // |     ...
    // |
    mp_obj_t *items;
    size_t len;
    mp_obj_get_array(sources_obj, &len, &items);

    if (len >= 2) {
        uint8_t uart_priority = 0;
        uint8_t usb_priority = 0;

        for (size_t i = 0; i < len && i < 2; i++) {
            mp_int_t source = mp_obj_get_int(items[i]);
            if (source == MIDI_SOURCE_UART) {
                uart_priority = i + 1;
            } else if (source == MIDI_SOURCE_USB) {
                usb_priority = i + 1;
            }
        }

        if (uart_priority > 0 && usb_priority > 0) {
            denkioto_multicore_set_clock_source_priority(uart_priority, usb_priority);
        }
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(denkioto_rin_set_clock_source_priority_obj, denkioto_rin_set_clock_source_priority_func);

static mp_obj_t denkioto_rin_get_ring_value_func(mp_obj_t ring_obj, mp_obj_t index_obj) {
    // | def get_ring_value(ring: int, index: int) -> int:
    // |     """Get a single value from a ring's data array.
    // |
    // |     :param ring: Ring number (0-3)
    // |     :param index: Index in the ring's data array (0-24)
    // |     :return: The value at the specified position, or -1 if invalid parameters
    // |     """
    // |     ...
    // |
    int ring = mp_obj_get_int(ring_obj);
    int index = mp_obj_get_int(index_obj);
    return mp_obj_new_int(denkioto_multicore_get_ring_value(ring, index));
}
static MP_DEFINE_CONST_FUN_OBJ_2(denkioto_rin_get_ring_value_obj, denkioto_rin_get_ring_value_func);

static mp_obj_t denkioto_rin_get_ring_values_func(mp_obj_t ring_obj) {
    // | def get_ring_values(ring: int) -> list[int]:
    // |     """Get all values from a ring's data array.
    // |
    // |     :param ring: Ring number (0-3)
    // |     :return: A list of 25 values from the ring's data array
    // |     """
    // |     ...
    // |
    int ring = mp_obj_get_int(ring_obj);
    if (ring < 0 || ring >= 4) {
        mp_raise_ValueError(MP_ERROR_TEXT("Ring number must be 0-3"));
    }

    mp_obj_t values_list = mp_obj_new_list(0, NULL);
    int values[25];
    denkioto_multicore_get_ring_values(ring, values, 25);

    for (int i = 0; i < 25; i++) {
        mp_obj_list_append(values_list, mp_obj_new_int(values[i]));
    }

    return values_list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(denkioto_rin_get_ring_values_obj, denkioto_rin_get_ring_values_func);

static mp_obj_t denkioto_rin_clear_ring_values_func(mp_obj_t ring_obj) {
    // | def clear_ring_values(ring: int) -> None:
    // |     """Clear all values for a specific ring.
    // |
    // |     :param ring: Ring number (0-3)
    // |     """
    // |     ...
    // |
    int ring = mp_obj_get_int(ring_obj);
    if (ring < 0 || ring >= 4) {
        mp_raise_ValueError(MP_ERROR_TEXT("Ring number must be 0-3"));
    }
    denkioto_multicore_clear_ring_values(ring);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(denkioto_rin_clear_ring_values_obj, denkioto_rin_clear_ring_values_func);

static mp_obj_t denkioto_rin_get_resync_count_func(mp_obj_t ring_obj) {
    // | def get_resync_count(ring: int) -> int:
    // |     """Get the number of times a ring has been resynchronized.
    // |
    // |     :param ring: Ring number (0-3)
    // |     :return: Number of resync operations performed on this ring
    // |     """
    // |     ...
    // |
    int ring = mp_obj_get_int(ring_obj);
    if (ring < 0 || ring >= 4) {
        mp_raise_ValueError(MP_ERROR_TEXT("Ring number must be 0-3"));
    }
    return mp_obj_new_int(denkioto_multicore_get_resync_count(ring));
}
static MP_DEFINE_CONST_FUN_OBJ_1(denkioto_rin_get_resync_count_obj, denkioto_rin_get_resync_count_func);

static mp_obj_t denkioto_rin_get_data_ready_count_func(mp_obj_t ring_obj) {
    // | def get_data_ready_count(ring: int) -> int:
    // |     """Get the number of times data was marked ready for a ring.
    // |
    // |     :param ring: Ring number (0-3)
    // |     :return: Number of times data was marked ready for this ring
    // |     """
    // |     ...
    // |
    int ring = mp_obj_get_int(ring_obj);
    if (ring < 0 || ring >= 4) {
        mp_raise_ValueError(MP_ERROR_TEXT("Ring number must be 0-3"));
    }
    return mp_obj_new_int(denkioto_multicore_get_data_ready_count(ring));
}
static MP_DEFINE_CONST_FUN_OBJ_1(denkioto_rin_get_data_ready_count_obj, denkioto_rin_get_data_ready_count_func);

// MIDI state functions
static mp_obj_t denkioto_rin_get_note_func(mp_obj_t source_obj, mp_obj_t channel_obj, mp_obj_t note_obj) {
    // | def get_note(source: int, channel: int, note: int) -> int:
    // |     """Get the velocity of a specific note on a MIDI channel.
    // |
    // |     :param source: MIDI source (MIDI_UART or MIDI_USB)
    // |     :param channel: MIDI channel (0-15)
    // |     :param note: Note number (0-127)
    // |     :return: Velocity (0=off, 1-127=on with velocity)
    // |     """
    // |     ...
    // |
    mp_int_t source = mp_obj_get_int(source_obj);
    mp_int_t channel = mp_obj_get_int(channel_obj);
    mp_int_t note = mp_obj_get_int(note_obj);
    if (source < 1 || source > 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("Source must be MIDI_UART or MIDI_USB"));
    }
    if (channel < 0 || channel >= 16) {
        mp_raise_ValueError(MP_ERROR_TEXT("Channel must be 0-15"));
    }
    if (note < 0 || note >= 128) {
        mp_raise_ValueError(MP_ERROR_TEXT("Note must be 0-127"));
    }
    return mp_obj_new_int(denkioto_multicore_get_note(source, channel, note));
}
static MP_DEFINE_CONST_FUN_OBJ_3(denkioto_rin_get_note_obj, denkioto_rin_get_note_func);

static mp_obj_t denkioto_rin_get_notes_func(mp_obj_t source_obj, mp_obj_t channel_obj) {
    // | def get_notes(source: int, channel: int) -> dict[int, int]:
    // |     """Get all active note velocities for a MIDI channel.
    // |
    // |     :param source: MIDI source (MIDI_UART or MIDI_USB)
    // |     :param channel: MIDI channel (0-15)
    // |     :return: Dictionary of {note_num: velocity} for notes with non-zero velocity
    // |     """
    // |     ...
    // |
    mp_int_t source = mp_obj_get_int(source_obj);
    mp_int_t channel = mp_obj_get_int(channel_obj);
    if (source < 1 || source > 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("Source must be MIDI_UART or MIDI_USB"));
    }
    if (channel < 0 || channel >= 16) {
        mp_raise_ValueError(MP_ERROR_TEXT("Channel must be 0-15"));
    }

    uint8_t notes[128];
    denkioto_multicore_get_notes(source, channel, notes);

    mp_obj_t dict = mp_obj_new_dict(0);
    for (int i = 0; i < 128; i++) {
        if (notes[i] > 0) {
            mp_obj_dict_store(dict, mp_obj_new_int(i), mp_obj_new_int(notes[i]));
        }
    }
    return dict;
}
static MP_DEFINE_CONST_FUN_OBJ_2(denkioto_rin_get_notes_obj, denkioto_rin_get_notes_func);

static mp_obj_t denkioto_rin_get_cc_func(mp_obj_t source_obj, mp_obj_t channel_obj, mp_obj_t cc_obj) {
    // | def get_cc(source: int, channel: int, cc: int) -> int:
    // |     """Get a control change value for a MIDI channel.
    // |
    // |     :param source: MIDI source (MIDI_UART or MIDI_USB)
    // |     :param channel: MIDI channel (0-15)
    // |     :param cc: Control change number (0-127)
    // |     :return: CC value (0-127)
    // |     """
    // |     ...
    // |
    mp_int_t source = mp_obj_get_int(source_obj);
    mp_int_t channel = mp_obj_get_int(channel_obj);
    mp_int_t cc = mp_obj_get_int(cc_obj);
    if (source < 1 || source > 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("Source must be MIDI_UART or MIDI_USB"));
    }
    if (channel < 0 || channel >= 16) {
        mp_raise_ValueError(MP_ERROR_TEXT("Channel must be 0-15"));
    }
    if (cc < 0 || cc >= 128) {
        mp_raise_ValueError(MP_ERROR_TEXT("CC must be 0-127"));
    }
    return mp_obj_new_int(denkioto_multicore_get_cc(source, channel, cc));
}
static MP_DEFINE_CONST_FUN_OBJ_3(denkioto_rin_get_cc_obj, denkioto_rin_get_cc_func);

static mp_obj_t denkioto_rin_get_cc_all_func(mp_obj_t source_obj, mp_obj_t channel_obj) {
    // | def get_cc_all(source: int, channel: int) -> list[int]:
    // |     """Get all control change values for a MIDI channel.
    // |
    // |     :param source: MIDI source (MIDI_UART or MIDI_USB)
    // |     :param channel: MIDI channel (0-15)
    // |     :return: List of 128 CC values (0-127)
    // |     """
    // |     ...
    // |
    mp_int_t source = mp_obj_get_int(source_obj);
    mp_int_t channel = mp_obj_get_int(channel_obj);
    if (source < 1 || source > 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("Source must be MIDI_UART or MIDI_USB"));
    }
    if (channel < 0 || channel >= 16) {
        mp_raise_ValueError(MP_ERROR_TEXT("Channel must be 0-15"));
    }

    uint8_t values[128];
    denkioto_multicore_get_cc_all(source, channel, values);

    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (int i = 0; i < 128; i++) {
        mp_obj_list_append(list, mp_obj_new_int(values[i]));
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_2(denkioto_rin_get_cc_all_obj, denkioto_rin_get_cc_all_func);

static mp_obj_t denkioto_rin_get_poly_pressure_func(mp_obj_t source_obj, mp_obj_t channel_obj, mp_obj_t note_obj) {
    // | def get_poly_pressure(source: int, channel: int, note: int) -> int:
    // |     """Get the polyphonic aftertouch pressure for a specific note.
    // |
    // |     :param source: MIDI source (MIDI_UART or MIDI_USB)
    // |     :param channel: MIDI channel (0-15)
    // |     :param note: Note number (0-127)
    // |     :return: Pressure value (0-127)
    // |     """
    // |     ...
    // |
    mp_int_t source = mp_obj_get_int(source_obj);
    mp_int_t channel = mp_obj_get_int(channel_obj);
    mp_int_t note = mp_obj_get_int(note_obj);
    if (source < 1 || source > 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("Source must be MIDI_UART or MIDI_USB"));
    }
    if (channel < 0 || channel >= 16) {
        mp_raise_ValueError(MP_ERROR_TEXT("Channel must be 0-15"));
    }
    if (note < 0 || note >= 128) {
        mp_raise_ValueError(MP_ERROR_TEXT("Note must be 0-127"));
    }
    return mp_obj_new_int(denkioto_multicore_get_poly_pressure(source, channel, note));
}
static MP_DEFINE_CONST_FUN_OBJ_3(denkioto_rin_get_poly_pressure_obj, denkioto_rin_get_poly_pressure_func);

static mp_obj_t denkioto_rin_get_poly_pressure_all_func(mp_obj_t source_obj, mp_obj_t channel_obj) {
    // | def get_poly_pressure_all(source: int, channel: int) -> list[int]:
    // |     """Get all polyphonic aftertouch pressure values for a MIDI channel.
    // |
    // |     :param source: MIDI source (MIDI_UART or MIDI_USB)
    // |     :param channel: MIDI channel (0-15)
    // |     :return: List of 128 pressure values (0-127)
    // |     """
    // |     ...
    // |
    mp_int_t source = mp_obj_get_int(source_obj);
    mp_int_t channel = mp_obj_get_int(channel_obj);
    if (source < 1 || source > 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("Source must be MIDI_UART or MIDI_USB"));
    }
    if (channel < 0 || channel >= 16) {
        mp_raise_ValueError(MP_ERROR_TEXT("Channel must be 0-15"));
    }

    uint8_t pressure[128];
    denkioto_multicore_get_poly_pressure_all(source, channel, pressure);

    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (int i = 0; i < 128; i++) {
        mp_obj_list_append(list, mp_obj_new_int(pressure[i]));
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_2(denkioto_rin_get_poly_pressure_all_obj, denkioto_rin_get_poly_pressure_all_func);

static mp_obj_t denkioto_rin_get_pitch_bend_func(mp_obj_t source_obj, mp_obj_t channel_obj) {
    // | def get_pitch_bend(source: int, channel: int) -> int:
    // |     """Get the pitch bend value for a MIDI channel.
    // |
    // |     :param source: MIDI source (MIDI_UART or MIDI_USB)
    // |     :param channel: MIDI channel (0-15)
    // |     :return: Pitch bend value (0-16383, center=8192)
    // |     """
    // |     ...
    // |
    mp_int_t source = mp_obj_get_int(source_obj);
    mp_int_t channel = mp_obj_get_int(channel_obj);
    if (source < 1 || source > 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("Source must be MIDI_UART or MIDI_USB"));
    }
    if (channel < 0 || channel >= 16) {
        mp_raise_ValueError(MP_ERROR_TEXT("Channel must be 0-15"));
    }
    return mp_obj_new_int(denkioto_multicore_get_pitch_bend(source, channel));
}
static MP_DEFINE_CONST_FUN_OBJ_2(denkioto_rin_get_pitch_bend_obj, denkioto_rin_get_pitch_bend_func);

static mp_obj_t denkioto_rin_get_channel_pressure_func(mp_obj_t source_obj, mp_obj_t channel_obj) {
    // | def get_channel_pressure(source: int, channel: int) -> int:
    // |     """Get the channel pressure (aftertouch) for a MIDI channel.
    // |
    // |     :param source: MIDI source (MIDI_UART or MIDI_USB)
    // |     :param channel: MIDI channel (0-15)
    // |     :return: Channel pressure value (0-127)
    // |     """
    // |     ...
    // |
    mp_int_t source = mp_obj_get_int(source_obj);
    mp_int_t channel = mp_obj_get_int(channel_obj);
    if (source < 1 || source > 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("Source must be MIDI_UART or MIDI_USB"));
    }
    if (channel < 0 || channel >= 16) {
        mp_raise_ValueError(MP_ERROR_TEXT("Channel must be 0-15"));
    }
    return mp_obj_new_int(denkioto_multicore_get_channel_pressure(source, channel));
}
static MP_DEFINE_CONST_FUN_OBJ_2(denkioto_rin_get_channel_pressure_obj, denkioto_rin_get_channel_pressure_func);

static mp_obj_t denkioto_rin_get_program_func(mp_obj_t source_obj, mp_obj_t channel_obj) {
    // | def get_program(source: int, channel: int) -> int:
    // |     """Get the program (patch) number for a MIDI channel.
    // |
    // |     :param source: MIDI source (MIDI_UART or MIDI_USB)
    // |     :param channel: MIDI channel (0-15)
    // |     :return: Program number (0-127)
    // |     """
    // |     ...
    // |
    mp_int_t source = mp_obj_get_int(source_obj);
    mp_int_t channel = mp_obj_get_int(channel_obj);
    if (source < 1 || source > 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("Source must be MIDI_UART or MIDI_USB"));
    }
    if (channel < 0 || channel >= 16) {
        mp_raise_ValueError(MP_ERROR_TEXT("Channel must be 0-15"));
    }
    return mp_obj_new_int(denkioto_multicore_get_program(source, channel));
}
static MP_DEFINE_CONST_FUN_OBJ_2(denkioto_rin_get_program_obj, denkioto_rin_get_program_func);

static mp_obj_t denkioto_rin_get_note_status_func(mp_obj_t source_obj, mp_obj_t channel_obj) {
    // | def get_note_status(source: int, channel: int) -> tuple[int, ...]:
    // |     """Get the note status bitmap for a MIDI channel.
    // |
    // |     Each bit represents one note: status[0] bits 0-31 = notes 0-31, etc.
    // |     Use (status[note//32] & (1 << (note%32))) to check if note is on.
    // |
    // |     :param source: MIDI source (MIDI_UART or MIDI_USB)
    // |     :param channel: MIDI channel (0-15)
    // |     :return: Tuple of 4 uint32 values representing 128 note bits
    // |     """
    // |     ...
    // |
    mp_int_t source = mp_obj_get_int(source_obj);
    mp_int_t channel = mp_obj_get_int(channel_obj);
    if (source < 1 || source > 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("Source must be MIDI_UART or MIDI_USB"));
    }
    if (channel < 0 || channel >= 16) {
        mp_raise_ValueError(MP_ERROR_TEXT("Channel must be 0-15"));
    }

    uint32_t status[4];
    denkioto_multicore_get_note_status(source, channel, status);

    mp_obj_t tuple_items[4] = {
        mp_obj_new_int_from_uint(status[0]),
        mp_obj_new_int_from_uint(status[1]),
        mp_obj_new_int_from_uint(status[2]),
        mp_obj_new_int_from_uint(status[3])
    };
    return mp_obj_new_tuple(4, tuple_items);
}
static MP_DEFINE_CONST_FUN_OBJ_2(denkioto_rin_get_note_status_obj, denkioto_rin_get_note_status_func);

static const mp_rom_map_elem_t denkioto_rin_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_denkioto_rin) },
    { MP_ROM_QSTR(MP_QSTR_start), MP_ROM_PTR(&denkioto_rin_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&denkioto_rin_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_counter), MP_ROM_PTR(&denkioto_rin_get_counter_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_running), MP_ROM_PTR(&denkioto_rin_is_running_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset_counter), MP_ROM_PTR(&denkioto_rin_reset_counter_obj) },
    { MP_ROM_QSTR(MP_QSTR_print_debug_stats), MP_ROM_PTR(&denkioto_rin_print_debug_stats_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_ring_value), MP_ROM_PTR(&denkioto_rin_get_ring_value_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_ring_values), MP_ROM_PTR(&denkioto_rin_get_ring_values_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear_ring_values), MP_ROM_PTR(&denkioto_rin_clear_ring_values_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_resync_count), MP_ROM_PTR(&denkioto_rin_get_resync_count_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_data_ready_count), MP_ROM_PTR(&denkioto_rin_get_data_ready_count_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_midi_bpm), MP_ROM_PTR(&denkioto_rin_get_midi_bpm_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_transport_state), MP_ROM_PTR(&denkioto_rin_get_transport_state_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_clock_source), MP_ROM_PTR(&denkioto_rin_get_clock_source_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_clock_count), MP_ROM_PTR(&denkioto_rin_get_clock_count_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_beat_count), MP_ROM_PTR(&denkioto_rin_get_beat_count_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_clock_source_priority), MP_ROM_PTR(&denkioto_rin_set_clock_source_priority_obj) },

    // UART MIDI state functions
    { MP_ROM_QSTR(MP_QSTR_get_note), MP_ROM_PTR(&denkioto_rin_get_note_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_notes), MP_ROM_PTR(&denkioto_rin_get_notes_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_cc), MP_ROM_PTR(&denkioto_rin_get_cc_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_cc_all), MP_ROM_PTR(&denkioto_rin_get_cc_all_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_poly_pressure), MP_ROM_PTR(&denkioto_rin_get_poly_pressure_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_poly_pressure_all), MP_ROM_PTR(&denkioto_rin_get_poly_pressure_all_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_pitch_bend), MP_ROM_PTR(&denkioto_rin_get_pitch_bend_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_channel_pressure), MP_ROM_PTR(&denkioto_rin_get_channel_pressure_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_program), MP_ROM_PTR(&denkioto_rin_get_program_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_note_status), MP_ROM_PTR(&denkioto_rin_get_note_status_obj) },

    // MIDI source constants
    { MP_ROM_QSTR(MP_QSTR_MIDI_UART), MP_ROM_INT(MIDI_SOURCE_UART) },
    { MP_ROM_QSTR(MP_QSTR_MIDI_USB), MP_ROM_INT(MIDI_SOURCE_USB) },
};

static MP_DEFINE_CONST_DICT(denkioto_rin_module_globals, denkioto_rin_module_globals_table);

const mp_obj_module_t denkioto_rin_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&denkioto_rin_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_denkioto_rin, denkioto_rin_module);
