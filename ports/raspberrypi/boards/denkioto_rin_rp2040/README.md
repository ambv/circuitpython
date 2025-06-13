# Denkioto Rin RP2040 Board - Multicore Support

This board configuration extends the standard CircuitPython RP2040 implementation with dual-core functionality, allowing you to utilize both cores of the RP2040 microcontroller.

## Overview

The RP2040 microcontroller has two ARM Cortex-M0+ cores, but standard CircuitPython only uses one core (core0) for the main interpreter. This board implementation adds support for running background tasks on the second core (core1).

## Features

- **Background Counter**: once started from Python, core1 runs an infinite loop incrementing an internal counter
- **Touch Ring Data Collection**: Core1 continuously reads data from 4 touch rings via PIO-based UART
- **Python API**: Control and monitor core1 from your CircuitPython code, including access to touch ring data
- **Thread-Safe Operations**: Safe communication between cores, core1 shuts down on Python code reload or when Python code finishes running

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

#### `denkioto_rin.stop() -> int`
Stop core1 execution. Core1 will be stopped and reset. If core1 is not running, this function has no effect. Returns the number of retries it took to stop core1. In case core1 was not running, it will return -1.

#### `denkioto_rin.get_counter() -> int`
Get the current counter value from core1. Returns the current value of the counter that core1 is incrementing. This value is read safely, so it's safe to call from core0 while core1 is running.

#### `denkioto_rin.is_running() -> bool`
Check if core1 is currently running. Returns `True` if core1 is running, `False` otherwise.

#### `denkioto_rin.reset_counter()`
Reset the counter to zero. This operation can be called safely while core1 is running.

#### `denkioto_rin.get_ring_value(ring: int, index: int) -> int`
Get a single value from a ring's data array. Returns the value at the specified position, or -1 if invalid parameters.
- `ring`: Ring number (0-3)
- `index`: Index in the ring's data array (0-24)

#### `denkioto_rin.get_ring_values(ring: int) -> list[int]`
Get all values from a ring's data array. Returns a list of 25 values from the specified ring.
- `ring`: Ring number (0-3)

#### `denkioto_rin.clear_ring_values(ring: int)`
Clear all values for a specific ring, setting them to zero.
- `ring`: Ring number (0-3)

## Example Usage

```python
import time
import denkioto_rin

denkioto_rin.start()

# Check if core1 is running
if denkioto_rin.is_running():
    print("Core1 is running!")

# Monitor the counter and touch ring data
for i in range(10):
    counter = denkioto_rin.get_counter()
    print(f"Counter value: {counter}")

    # Read data from all 4 touch rings
    for ring in range(4):
        values = denkioto_rin.get_ring_values(ring)
        # Print first 5 values from each ring
        print(f"Ring {ring}: {values[:5]}...")

    time.sleep(1)

# Clear data from ring 0
denkioto_rin.clear_ring_values(0)

# Reset the counter
denkioto_rin.reset_counter()

# Stop core1
denkioto_rin.stop()  # unnecessary at the end of a script, this port will stop core1 from executing automatically
```

## Example Script

See `multicore_example.py` in this directory for a complete demonstration of the multicore functionality.

## Implementation Details

### Core1 Behavior

- Core1 runs in an infinite loop incrementing a 32-bit counter
- The counter wraps around at 2^32 (approximately 4.3 billion)
- Core1 continuously reads data from 4 touch rings via PIO-based UART
- Touch ring data is stored in 4x25 arrays (one array per ring)
- Core1 includes small delays to prevent overwhelming the system
- Core1 can be safely started and stopped multiple times

### Memory Safety

- All inter-core communication uses appropriate synchronization
- The counter variable is accessed safely between cores
- Touch ring data arrays are accessed safely from both cores
- Core1 state is properly managed during start/stop operations
- PIO state machines are properly initialized and cleaned up

### Performance

- With pico-sdk 2.1.1 the company Raspberry Pi allowed for RP2040 to run at 200MHz; this port uses that capability
- Core1 increments the counter at approximately 3.5 million times per second (depending on system load)
- Touch ring data is read continuously via PIO-based UART at 100kHz
- The exact rate varies based on other system activities and power management
- Counter and touch ring data access from core0 has minimal performance impact

## Building

To build this board configuration:

```bash
cd circuitpython/ports/raspberrypi
make BOARD=denkioto_rin_rp2040
```

## Technical Notes

- Core1 does not start automatically in `board_init()` to avoid race conditions in IRQ handling before Python code is initialized
- Core1 is stopped automatically in `reset_board()`, which is called on every code reload
- Core1 is also stopped automatically in `board_deinit()`, which is called on board shutdown
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
  - "Debug RP2040 Core1" - Debug second core, requires a launched debugger for Core0

### Build Configurations
- **DEBUG Build**: `-Og -g3` optimization with debug symbols for effective debugging
- **OPT Build**: `-O3` optimization for production (default)
- **Build Tracking**: Automatic detection of configuration changes triggers clean rebuild

### Board-Specific Settings
- **Python Environment**: Uses `/Users/ambv/.virtualenvs/circuitpython-build` virtualenv
- **Multicore Support**: Custom implementation in `boards/denkioto_rin_rp2040/denkioto/`

### Key Development Notes
- Debug builds are required for effective debugging (variables, stepping, breakpoints)
- Core1 debugging requires the "external" servertype connecting to an existing OpenOCD instance
- CIRCUITPY drive appears when firmware boots normally, but disappears when the debugger is stopped on a breakpoint in Core0
- VSCode tasks automatically handle virtualenv activation and configuration tracking

## License

This code follows the same MIT license as CircuitPython.

## Current Implementation Status

The multicore functionality is fully implemented with the following features:

- ✅ Core1 runs independently, incrementing a counter
- ✅ Touch ring data collection via PIO-based UART from 4 rings
- ✅ Python API for controlling core1 and accessing touch ring data
- ✅ Proper shutdown and cleanup of PIO state machines
- ✅ Thread-safe access to shared data between cores

### Technical Implementation Details

- Touch ring UART communication uses the RP2040's PIO (Programmable I/O) feature
- Each ring uses a dedicated PIO state machine configured for UART RX at 100kHz
- Data from each ring is stored in a 25-element array
- The PIO program (`uart_rx.pio`) is compiled into the firmware
- All PIO resources are properly cleaned up when core1 is stopped
