// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include "py/obj.h"
#include "py/runtime.h"
#include "py/builtin.h"

#include "multicore.h"

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
    // | def get_clock_source() -> str:
    // |     """Get the currently active MIDI clock source.
    // |
    // |     :return: "uart", "usb", or "none"
    // |     """
    // |     ...
    // |
    uint8_t source = denkioto_multicore_get_clock_source();
    switch (source) {
        case 1:
            return mp_obj_new_str("uart", 4);
        case 2:
            return mp_obj_new_str("usb", 3);
        default:
            return mp_obj_new_str("none", 4);
    }
}
static MP_DEFINE_CONST_FUN_OBJ_0(denkioto_rin_get_clock_source_obj, denkioto_rin_get_clock_source_func);

static mp_obj_t denkioto_rin_get_clock_precision_func(void) {
    // | def get_clock_precision() -> str:
    // |     """Get the precision level of the current clock source.
    // |
    // |     :return: "high" for UART (microsecond precision) or "standard" for USB
    // |     """
    // |     ...
    // |
    uint8_t source = denkioto_multicore_get_clock_source();
    if (source == 1) {
        return mp_obj_new_str("high", 4);  // UART has microsecond precision
    } else if (source == 2) {
        return mp_obj_new_str("standard", 8);  // USB has polling-based precision
    } else {
        return mp_obj_new_str("none", 4);  // No active source
    }
}
static MP_DEFINE_CONST_FUN_OBJ_0(denkioto_rin_get_clock_precision_obj, denkioto_rin_get_clock_precision_func);

static mp_obj_t denkioto_rin_set_clock_source_priority_func(mp_obj_t sources_obj) {
    // | def set_clock_source_priority(sources: list[str]) -> None:
    // |     """Set the priority order for MIDI clock sources.
    // |
    // |     :param sources: List of source names in priority order, e.g. ['uart', 'usb']
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
            const char *source = mp_obj_str_get_str(items[i]);
            if (strcmp(source, "uart") == 0) {
                uart_priority = i + 1;
            } else if (strcmp(source, "usb") == 0) {
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
    { MP_ROM_QSTR(MP_QSTR_get_clock_precision), MP_ROM_PTR(&denkioto_rin_get_clock_precision_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_clock_source_priority), MP_ROM_PTR(&denkioto_rin_set_clock_source_priority_obj) },
};

static MP_DEFINE_CONST_DICT(denkioto_rin_module_globals, denkioto_rin_module_globals_table);

const mp_obj_module_t denkioto_rin_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&denkioto_rin_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_denkioto_rin, denkioto_rin_module);
