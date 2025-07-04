// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 Łukasz Langa
//
// SPDX-License-Identifier: MIT

#ifndef MICROPY_INCLUDED_DENKIOTO_NEOPIXEL_NB_H
#define MICROPY_INCLUDED_DENKIOTO_NEOPIXEL_NB_H

#include <stdbool.h>
#include <stdint.h>
#include "common-hal/digitalio/DigitalInOut.h"

// Initialize non-blocking neopixel system
void denkioto_neopixel_nb_init(void);

// Deinitialize and clean up resources
void denkioto_neopixel_nb_deinit(void);

// Start non-blocking write to two LED pins simultaneously
// Returns true if write started, false if still busy from previous write
bool denkioto_neopixel_nb_write_dual(
    const digitalio_digitalinout_obj_t *pin1,
    const uint8_t *pixels1,
    uint32_t num_bytes1,
    const digitalio_digitalinout_obj_t *pin2,
    const uint8_t *pixels2,
    uint32_t num_bytes2
    );

// Check if the non-blocking write is still in progress
bool denkioto_neopixel_nb_is_busy(void);

#endif // MICROPY_INCLUDED_DENKIOTO_NEOPIXEL_NB_H
