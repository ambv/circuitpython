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

#endif // DENKIOTO_MULTICORE_H
