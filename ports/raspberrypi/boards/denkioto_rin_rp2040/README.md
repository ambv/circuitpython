# Denkioto Rin RP2040 Board - Multicore Support

This board configuration extends the standard CircuitPython RP2040 implementation with dual-core functionality, allowing you to utilize both cores of the RP2040 microcontroller with IRQ-based double-buffered DMA for high-performance touch ring data acquisition.

## Overview

The RP2040 microcontroller has two ARM Cortex-M0+ cores, but standard CircuitPython only uses one core (core0) for the main interpreter. This board implementation adds support for running background tasks on the second core (core1) with microsecond-precise DMA-based data collection.

## Features

- **IRQ-Based DMA Data Collection**: Core1 uses DMA_IRQ_1 interrupts for microsecond-precise touch ring data acquisition
- **Double-Buffered DMA**: Prevents data loss when core1 gets busy with additional functionality
- **Automatic Frame Synchronization**: Single-byte sync detection followed by 26-byte frame capture
- **Thread-Safe Operations**: Three-state atomic system ensures safe data access between cores
- **Background Counter**: Core1 runs an infinite loop incrementing an internal counter
- **Python API**: Control and monitor core1 from your CircuitPython code, including access to touch ring data
- **Debug Counters**: Monitor sync events and data flow without printf overhead

## Hardware

- **Board**: denki-oto rin
- **MCU**: RP2040
- **Cores**: Dual ARM Cortex-M0+ @ 200MHz (allowed since pico-sdk 2.1.1)
- **Memory**: 264KB SRAM, 2MB Flash
- **Status LED**: GPIO16
- **Touch Rings**: 4 rings connected via PIO-based UART at 100kHz

## Touch Ring Pin Configuration

- **Ring 0**: RX=GPIO3, TX=GPIO2
- **Ring 1**: RX=GPIO5, TX=GPIO4
- **Ring 2**: RX=GPIO7, TX=GPIO6
- **Ring 3**: RX=GPIO9, TX=GPIO8
- **MIDI**: RX=GPIO1, TX=GPIO0

## Multicore API

The board provides a `denkioto_rin` module with the following functions:

### Core Control Functions

#### `denkioto_rin.start()`
Start core1 execution with IRQ-based DMA data collection. Core1 will begin running with automatic frame synchronization.

#### `denkioto_rin.stop() -> int`
Stop core1 execution. Returns the number of retries it took to stop core1, or -1 if core1 was not running.

#### `denkioto_rin.get_counter() -> int`
Get the current counter value from core1. This value is read atomically.

#### `denkioto_rin.is_running() -> bool`
Check if core1 is currently running.

#### `denkioto_rin.reset_counter()`
Reset the counter to zero. This operation is atomic.

### Touch Ring Data Functions

#### `denkioto_rin.get_ring_value(ring: int, index: int) -> int`
Get a single value from a ring's data array.
- `ring`: Ring number (0-3)
- `index`: Index in the ring's data array (0-24)

#### `denkioto_rin.get_ring_values(ring: int) -> list[int]`
Get all values from a ring's data array. Returns a list of 25 values.
- `ring`: Ring number (0-3)

#### `denkioto_rin.clear_ring_values(ring: int)`
Clear all values for a specific ring.
- `ring`: Ring number (0-3)

### Debug Functions

#### `denkioto_rin.get_resync_count(ring: int) -> int`
Get the number of times a ring has been resynchronized. Should be 0 with IRQ-based sync.
- `ring`: Ring number (0-3)

#### `denkioto_rin.get_data_ready_count(ring: int) -> int`
Get the number of times data was marked ready for a ring. Should increase steadily.
- `ring`: Ring number (0-3)

## Example Usage

```python
import time
import denkioto_rin

# Start core1 with IRQ-based DMA
denkioto_rin.start()

# Monitor for 10 seconds
for i in range(10):
    counter = denkioto_rin.get_counter()
    print(f"Counter: {counter}")

    # Check data from all 4 rings
    for ring in range(4):
        values = denkioto_rin.get_ring_values(ring)
        non_zero = sum(1 for v in values if v > 0)

        # Get debug counters
        resync_count = denkioto_rin.get_resync_count(ring)
        data_ready_count = denkioto_rin.get_data_ready_count(ring)

        sync_status = "synced" if data_ready_count > 0 else "searching"
        print(f"Ring {ring}: {values[:5]}... ({non_zero}/25 non-zero) "
              f"[{sync_status}, resync: {resync_count}, ready: {data_ready_count}]")

    time.sleep(1)

denkioto_rin.stop()
```

## IRQ-Based DMA Implementation

### Architecture Overview

The current implementation uses an advanced IRQ-based DMA approach that provides microsecond-precise timing and perfect frame synchronization:

1. **Single-byte sync detection**: DMA starts with transfer_count=1, repeatedly writing to buffer[0] until zero byte found
2. **Frame completion**: Once zero found at buffer[0], DMA switches to transfer_count=25 starting at buffer[1]
3. **Double-buffered operation**: After sync, normal 26-byte transfers with automatic buffer switching
4. **Thread-safe access**: Three-state atomic system (0=not ready, 1=ready, 2=being read)

### Key Technical Features

- **DMA_IRQ_1 interrupts**: Avoids conflicts with audio_dma (which uses DMA_IRQ_0)
- **Microsecond precision**: IRQ-driven buffer switching prevents frame boundary drift
- **Zero resync overhead**: Once synchronized, no costly polling or timeout operations
- **Board-contained solution**: No changes to core CircuitPython required
- **Latest data priority**: Core1 overwrites unread data for real-time behavior

### Frame Format

Each touch ring emits 26-byte frames at 100kHz:
- **Byte 0**: Always 0 (frame start marker)
- **Bytes 1-25**: Touch sensitivity data (values 1-255, never 0)

### Thread Safety Implementation

```c
// Three-state atomic flags per ring:
// 0 = data not ready (stale or being written)
// 1 = data ready for reading
// 2 = data being read (core0 has exclusive access)
static volatile uint32_t ring_data_state[4];
```

Core1 (writer) transitions: 0→0 (write) or 1→0 (overwrite)
Core0 (reader) transitions: 1→2 (claim) → 0 (release)

## Building

To build this board configuration for debugging:

```bash
$ cd circuitpython/ports/raspberrypi
$ source /Users/ambv/.virtualenvs/circuitpython-build/bin/activate
$ make -j10 BOARD=denkioto_rin_rp2040 DEBUG=1 OPTIMIZATION_FLAGS="-Og -g3"
```

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

## Implementation Status: COMPLETED ✅

The IRQ-based DMA implementation is fully working with the following features:

- ✅ **IRQ-based synchronization**: Single-byte transfers until zero found, then 26-byte mode
- ✅ **Perfect frame alignment**: Every processed frame starts with zero at index 0
- ✅ **Double-buffered DMA**: Prevents data loss during core1 processing
- ✅ **Thread-safe atomic access**: Three-state system for safe data sharing
- ✅ **Debug counters**: Monitor sync events and data flow without printf overhead
- ✅ **Board-contained solution**: Zero changes to core CircuitPython required
- ✅ **Microsecond precision**: DMA_IRQ_1 provides precise timing
- ✅ **Latest data priority**: Core1 overwrites unread data for real-time behavior

### Performance Results

The IRQ-based approach has eliminated the timing issues that plagued previous implementations:

- **Instant synchronization**: No more "takes quite a long while sometimes"
- **Zero resync overhead**: No costly polling or timeout-based recovery needed
- **Perfect frame boundaries**: Automatic alignment without drift
- **Scalable**: Core1 can now safely handle additional tasks

### Key Files

- `denkioto/multicore.c`: IRQ-based DMA implementation with touch_ring_dma_irq_handler()
- `denkioto/multicore.h`: Function declarations for multicore API
- `denkioto/rin.c`: Python module bindings including debug counter access
- `denkioto/uart_rx.pio`: PIO program for 100kHz UART reception
- `test_double_buffer.py`: Test script demonstrating IRQ-based synchronization

### Debug Counter Interpretation

- **resync_count**: Should be 0 (no resync needed with IRQ-based sync)
- **data_ready_count**: Should increase steadily (indicates data flow)
- **Note**: During initial sync, ready counter may be 0 until first zero found

### Implementation Evolution

1. **Initial polling approach**: Suffered from timing issues and data loss
2. **DMA with polling**: Still had frame sync problems due to busy-wait timing
3. **Complex resync logic**: Added three-tier recovery but was slow and unreliable
4. **IRQ-based sync (current)**: Single-byte detection + frame completion = perfect alignment

The current IRQ-based implementation represents the optimal solution for this hardware, providing both perfect timing and clean architecture.

## License

This code follows the same MIT license as CircuitPython.
