// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include "shared-bindings/board/__init__.h"

static const mp_rom_map_elem_t board_module_globals_table[] = {
    CIRCUITPYTHON_BOARD_DICT_STANDARD_ITEMS

    { MP_ROM_QSTR(MP_QSTR_TX), MP_ROM_PTR(&pin_GPIO0) },
    { MP_ROM_QSTR(MP_QSTR_D0), MP_ROM_PTR(&pin_GPIO0) },
    { MP_ROM_QSTR(MP_QSTR_RX), MP_ROM_PTR(&pin_GPIO1) },
    { MP_ROM_QSTR(MP_QSTR_D1), MP_ROM_PTR(&pin_GPIO1) },

    { MP_ROM_QSTR(MP_QSTR_SDA1), MP_ROM_PTR(&pin_GPIO10) },
    { MP_ROM_QSTR(MP_QSTR_SCL1), MP_ROM_PTR(&pin_GPIO11) },

    { MP_ROM_QSTR(MP_QSTR_TXPIN0), MP_ROM_PTR(&pin_GPIO2) },
    { MP_ROM_QSTR(MP_QSTR_RXPIN0), MP_ROM_PTR(&pin_GPIO3) },
    { MP_ROM_QSTR(MP_QSTR_TXPIN1), MP_ROM_PTR(&pin_GPIO4) },
    { MP_ROM_QSTR(MP_QSTR_RXPIN1), MP_ROM_PTR(&pin_GPIO5) },
    { MP_ROM_QSTR(MP_QSTR_TXPIN2), MP_ROM_PTR(&pin_GPIO6) },
    { MP_ROM_QSTR(MP_QSTR_RXPIN2), MP_ROM_PTR(&pin_GPIO7) },
    { MP_ROM_QSTR(MP_QSTR_TXPIN3), MP_ROM_PTR(&pin_GPIO8) },
    { MP_ROM_QSTR(MP_QSTR_RXPIN3), MP_ROM_PTR(&pin_GPIO9) },


    { MP_ROM_QSTR(MP_QSTR_LED), MP_ROM_PTR(&pin_GPIO16) },

    { MP_ROM_QSTR(MP_QSTR_POWENABLE), MP_ROM_PTR(&pin_GPIO21) },
    { MP_ROM_QSTR(MP_QSTR_LEDPIN0), MP_ROM_PTR(&pin_GPIO24) },
    { MP_ROM_QSTR(MP_QSTR_LEDPIN1), MP_ROM_PTR(&pin_GPIO25) },
    { MP_ROM_QSTR(MP_QSTR_LEDPIN2), MP_ROM_PTR(&pin_GPIO23) },
    { MP_ROM_QSTR(MP_QSTR_LEDPIN3), MP_ROM_PTR(&pin_GPIO22) },
    { MP_ROM_QSTR(MP_QSTR_BATCHECK), MP_ROM_PTR(&pin_GPIO26) },
    { MP_ROM_QSTR(MP_QSTR_BUTTON_PIN1), MP_ROM_PTR(&pin_GPIO28) },
    { MP_ROM_QSTR(MP_QSTR_BUTTON_PIN2), MP_ROM_PTR(&pin_GPIO27) },

    { MP_ROM_QSTR(MP_QSTR_I2C), MP_ROM_PTR(&board_i2c_obj) },
    { MP_ROM_QSTR(MP_QSTR_UART), MP_ROM_PTR(&board_uart_obj) },
    { MP_ROM_QSTR(MP_QSTR_STEMMA_I2C), MP_ROM_PTR(&board_i2c_obj) },
};
MP_DEFINE_CONST_DICT(board_module_globals, board_module_globals_table);
