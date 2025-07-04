// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 Łukasz Langa
//
// SPDX-License-Identifier: MIT

#ifndef MICROPY_INCLUDED_DENKIOTO_RIN_H
#define MICROPY_INCLUDED_DENKIOTO_RIN_H

#include "py/obj.h"

extern const mp_obj_module_t denkioto_rin_module;

// Module functions
extern mp_obj_t denkioto_rin_start(void);
extern mp_obj_t denkioto_rin_stop(void);
extern mp_obj_t denkioto_rin_get_counter(void);
extern mp_obj_t denkioto_rin_is_running(void);
extern mp_obj_t denkioto_rin_reset_counter(void);

#endif // MICROPY_INCLUDED_DENKIOTO_RIN_H
