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

The board provides a `denkioto_multicore` module with the following functions:

### Functions

#### `denkioto_multicore.start()`
Start core1 execution. Core1 will begin running an infinite loop that increments an internal counter. If core1 is already running, this function has no effect.

#### `denkioto_multicore.stop()`
Stop core1 execution. Core1 will be stopped and reset. If core1 is not running, this function has no effect.

#### `denkioto_multicore.get_counter() -> int`
Get the current counter value from core1. Returns the current value of the counter that core1 is incrementing. This value is read safely, so it's safe to call from core0 while core1 is running.

#### `denkioto_multicore.is_running() -> bool`
Check if core1 is currently running. Returns `True` if core1 is running, `False` otherwise.

#### `denkioto_multicore.reset_counter()`
Reset the counter to zero. This operation can be called safely while core1 is running.

## Example Usage

```python
import time
import denkioto_multicore

# Check if core1 is running (it starts automatically)
if denkioto_multicore.is_running():
    print("Core1 is running!")

# Monitor the counter
for i in range(10):
    counter = denkioto_multicore.get_counter()
    print(f"Counter value: {counter}")
    time.sleep(1)

# Reset the counter
denkioto_multicore.reset_counter()

# Stop core1
denkioto_multicore.stop()

# Restart core1
denkioto_multicore.start()
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

## Files Added

- `denkioto_multicore.h` - Header file for multicore functionality
- `denkioto_multicore.c` - Core multicore implementation
- `denkioto/multicore.h` - Python module header
- `denkioto/multicore.c` - Python module implementation
- `multicore_example.py` - Example usage script
- `README.md` - This documentation

## Technical Notes

- Core1 starts automatically in `board_init()`
- Core1 is stopped automatically in `board_deinit()`
- The implementation uses the Pico SDK's multicore functionality
- Thread safety is ensured through proper synchronization primitives

## Future Enhancements

This implementation provides a foundation for more advanced multicore features:

- Custom user functions on core1
- Inter-core message passing
- Shared memory regions
- Real-time processing capabilities
- Hardware-specific optimizations

## License

This code follows the same MIT license as CircuitPython.
