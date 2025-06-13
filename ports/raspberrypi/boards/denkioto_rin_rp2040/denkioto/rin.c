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
    // |     has no effect. Returns the number of retries it took to stop core1.
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

static const mp_rom_map_elem_t denkioto_rin_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_denkioto_rin) },
    { MP_ROM_QSTR(MP_QSTR_start), MP_ROM_PTR(&denkioto_rin_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&denkioto_rin_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_counter), MP_ROM_PTR(&denkioto_rin_get_counter_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_running), MP_ROM_PTR(&denkioto_rin_is_running_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset_counter), MP_ROM_PTR(&denkioto_rin_reset_counter_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_ring_value), MP_ROM_PTR(&denkioto_rin_get_ring_value_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_ring_values), MP_ROM_PTR(&denkioto_rin_get_ring_values_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear_ring_values), MP_ROM_PTR(&denkioto_rin_clear_ring_values_obj) },
};

static MP_DEFINE_CONST_DICT(denkioto_rin_module_globals, denkioto_rin_module_globals_table);

const mp_obj_module_t denkioto_rin_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&denkioto_rin_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_denkioto_rin, denkioto_rin_module);
