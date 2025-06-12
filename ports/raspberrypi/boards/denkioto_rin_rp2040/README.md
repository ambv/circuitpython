# Denkioto Rin RP2040 Board - Multicore Support

This board configuration extends the standard CircuitPython RP2040 implementation with dual-core functionality, allowing you to utilize both cores of the RP2040 microcontroller.

## Overview

The RP2040 microcontroller has two ARM Cortex-M0+ cores, but standard CircuitPython only uses one core (core0) for the main interpreter. This board implementation adds support for running background tasks on the second core (core1).

## Features

- **Automatic Core1 Initialization**: Core1 starts automatically when the board boots
- **Background Counter**: Core1 runs an infinite loop incrementing an internal counter
- **Python API**: Control and monitor core1 from your CircuitPython code
- **Thread-Safe Operations**: Safe communication between cores

## Hardware

- **Board**: denki-oto rin
- **MCU**: RP2040
- **Cores**: Dual ARM Cortex-M0+ @ 125MHz
- **Memory**: 264KB SRAM, 2MB Flash
- **Status LED**: GPIO16

## Default Pin Configuration

- **I2C**: SDA=GPIO10, SCL=GPIO11
- **UART**: TX=GPIO0, RX=GPIO1
- **Status LED**: GPIO16

## Multicore API

The board provides a `denkioto_rin` module with the following functions:

### Functions

#### `denkioto_rin.start()`
Start core1 execution. Core1 will begin running an infinite loop that increments an internal counter. If core1 is already running, this function has no effect.

#### `denkioto_rin.stop()`
Stop core1 execution. Core1 will be stopped and reset. If core1 is not running, this function has no effect.

#### `denkioto_rin.get_counter() -> int`
Get the current counter value from core1. Returns the current value of the counter that core1 is incrementing. This value is read safely, so it's safe to call from core0 while core1 is running.

#### `denkioto_rin.is_running() -> bool`
Check if core1 is currently running. Returns `True` if core1 is running, `False` otherwise.

#### `denkioto_rin.reset_counter()`
Reset the counter to zero. This operation can be called safely while core1 is running.

## Example Usage

```python
import time
import denkioto_rin

# Check if core1 is running (it starts automatically)
if denkioto_rin.is_running():
    print("Core1 is running!")

# Monitor the counter
for i in range(10):
    counter = denkioto_rin.get_counter()
    print(f"Counter value: {counter}")
    time.sleep(1)

# Reset the counter
denkioto_rin.reset_counter()

# Stop core1
denkioto_rin.stop()

# Restart core1
denkioto_rin.start()
```

## Example Script

See `multicore_example.py` in this directory for a complete demonstration of the multicore functionality.

## Implementation Details

### Core1 Behavior

- Core1 runs in an infinite loop incrementing a 32-bit counter
- The counter wraps around at 2^32 (approximately 4.3 billion)
- Core1 includes small delays to prevent overwhelming the system
- Core1 can be safely started and stopped multiple times

### Memory Safety

- All inter-core communication uses appropriate synchronization
- The counter variable is accessed safely between cores
- Core1 state is properly managed during start/stop operations

### Performance

- Core1 increments the counter at approximately 1-10 million times per second (depending on system load)
- The exact rate varies based on other system activities and power management
- Counter access from core0 has minimal performance impact

## Building

To build this board configuration:

```bash
cd circuitpython/ports/raspberrypi
make BOARD=denkioto_rin_rp2040
```

## Technical Notes

- Core1 starts automatically in `board_init()`
- Core1 is stopped automatically in `board_deinit()`
- The implementation uses the Pico SDK's multicore functionality
- Thread safety is ensured through proper synchronization primitives

## Development Environment

### VSCode Configuration
Complete VSCode setup for CircuitPython development is available in the repository root `.vscode/` directory:

- **C++ IntelliSense**: Configured to use clangd with ARM GCC toolchain detection
- **Build Tasks**: Automated build with configuration tracking (OPT vs DEBUG)
- **Multi-core Debugging**: Hardware debugging support via Raspberry Pi Debug Probe
- **Smart Rebuilding**: Automatic clean when switching between optimized and debug builds

### Hardware Debugging Setup
- **Debug Probe**: Raspberry Pi Debug Probe (connect D port to device SWD pins)
- **OpenOCD**: Required for debugging (`brew install openocd`)
- **Multi-core Support**: Separate debug sessions for core0 (port 50000) and core1 (port 50003)
- **Debug Configurations**:
  - "Debug RP2040 Core0" - Launch debugging with automatic build
  - "Attach to RP2040 Core0" - Attach to running firmware
  - "Debug RP2040 Core1" - Debug second core

### Build Configurations
- **DEBUG Build**: `-Og -g3` optimization with debug symbols for effective debugging
- **OPT Build**: `-O3` optimization for production (default)
- **Build Tracking**: Automatic detection of configuration changes triggers clean rebuild

### Board-Specific Settings
- **Python Environment**: Uses `/Users/ambv/.virtualenvs/circuitpython-build` virtualenv
- **Multicore Support**: Custom implementation in `boards/denkioto_rin_rp2040/denkioto/`

### Key Development Notes
- Debug builds are required for effective debugging (variables, stepping, breakpoints)
- Core1 debugging requires external servertype connecting to existing OpenOCD instance
- CIRCUITPY drive appears when firmware boots normally (not when halted in debugger)
- VSCode tasks automatically handle virtualenv activation and configuration tracking

## License

This code follows the same MIT license as CircuitPython.
