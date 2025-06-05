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
    // | def stop() -> None:
    // |     """Stop core1 execution.
    // |
    // |     Core1 will be stopped and reset. If core1 is not running, this function
    // |     has no effect.
    // |     """
    // |     ...
    // |
    denkioto_multicore_stop_core1();
    return mp_const_none;
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

static const mp_rom_map_elem_t denkioto_rin_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_denkioto_rin) },
    { MP_ROM_QSTR(MP_QSTR_start), MP_ROM_PTR(&denkioto_rin_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&denkioto_rin_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_counter), MP_ROM_PTR(&denkioto_rin_get_counter_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_running), MP_ROM_PTR(&denkioto_rin_is_running_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset_counter), MP_ROM_PTR(&denkioto_rin_reset_counter_obj) },
};

static MP_DEFINE_CONST_DICT(denkioto_rin_module_globals, denkioto_rin_module_globals_table);

const mp_obj_module_t denkioto_rin_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&denkioto_rin_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_denkioto_rin, denkioto_rin_module);
