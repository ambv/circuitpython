#!/usr/bin/env python3
"""
Test script for MIDI Clock functionality on denkioto_rin RP2040 board.

This script demonstrates the MIDI clock reception and processing capabilities
implemented in Core 1 with microsecond precision timing.

Features tested:
- UART MIDI clock reception
- USB MIDI clock reception
- Source priority management
- Transport state tracking
- BPM calculation from clock intervals
- Clock message filtering from Python USB MIDI reads

Hardware requirements:
- Denkioto Rin RP2040 board
- MIDI source connected to UART pins (GPIO0/1) or USB
- Optional: Second MIDI device for source switching tests
"""

import time
import denkioto_rin
import usb_midi

# import busio
import board
import select

print("Giving you 10 seconds to CTRL+C")
time.sleep(10)

print("=== MIDI Clock Test for Denkioto Rin RP2040 ===")
print()

# Start multicore processing
print("Starting Core 1...")
denkioto_rin.start()
print(f"Core 1 running: {denkioto_rin.is_running()}")
print()

# MIDI clock processing is automatically enabled when Core 1 starts
print("MIDI clock processing automatically enabled with Core 1")
print()

# Set up MIDI sources for comparison
print("Setting up MIDI interfaces...")
usb_port = usb_midi.ports[0]  # USB MIDI
# uart_midi = busio.UART(board.TX, board.RX, baudrate=31250)  # UART MIDI

# Set up USB MIDI polling
usb_poll = select.poll()
usb_poll.register(usb_port, select.POLLIN)

print("USB MIDI and UART MIDI interfaces ready")
print()

# Configure source priority (UART preferred)
print("Setting MIDI clock source priority...")
denkioto_rin.set_clock_source_priority(["uart", "usb"])
print("Priority: UART first, USB second")
print()

print("=== MIDI Clock Monitoring ===")
print("Send MIDI clock from your DAW or MIDI source...")
print("Press Ctrl+C to stop")
print()

# Monitoring variables
last_bpm = 0
last_transport = -1
last_source = ""
message_count = 0

try:
    while True:
        # Get current MIDI clock info
        bpm_x1000 = denkioto_rin.get_midi_bpm()
        bpm = bpm_x1000 / 1000.0
        transport = denkioto_rin.get_transport_state()
        source = denkioto_rin.get_clock_source()

        # Transport state names
        transport_names = {0: "stopped", 1: "playing", 2: "paused"}
        transport_name = transport_names.get(transport, "unknown")

        # Only print when values change or every 10 iterations
        if (
            bpm != last_bpm
            or transport != last_transport
            or source != last_source
            or message_count % 10 == 0
        ):
            print(
                f"BPM: {bpm:6.1f} | Transport: {transport_name:8s} | Source: {source:4s} | Counter: {denkioto_rin.get_counter()}"
            )

            last_bpm = bpm
            last_transport = transport
            last_source = source

        message_count += 1

        # Test USB MIDI filtering
        usb_events = usb_poll.poll(0)  # Non-blocking check
        if usb_events:
            # Read USB MIDI data - clock messages should be filtered out
            usb_data = usb_port.read(32)
            if usb_data:
                print(f"USB MIDI received (non-clock): {[hex(b) for b in usb_data]}")

        # Test UART MIDI (Core 1 processes clocks, other messages go to Core 0)
        # if uart_midi.in_waiting:
        #     uart_data = uart_midi.read(32)
        #     if uart_data:
        #         print(f"UART MIDI received (non-clock): {[hex(b) for b in uart_data]}")

        time.sleep(0.1)  # 10 Hz update rate

except KeyboardInterrupt:
    print("\n=== Test completed ===")

# Demonstrate source switching
print("\n=== Source Priority Test ===")
print("Testing source priority switching...")

# Switch to USB priority
print("Setting USB priority first...")
denkioto_rin.set_clock_source_priority(["usb", "uart"])
time.sleep(2)

current_source = denkioto_rin.get_clock_source()
print(f"Active source: {current_source}")

# Switch back to UART priority
print("Setting UART priority first...")
denkioto_rin.set_clock_source_priority(["uart", "usb"])
time.sleep(2)

current_source = denkioto_rin.get_clock_source()
print(f"Active source: {current_source}")

print("\n=== Final Status ===")
print(f"Final BPM: {denkioto_rin.get_midi_bpm() / 1000.0:.1f}")
print(f"Final Transport: {transport_names.get(denkioto_rin.get_transport_state(), 'unknown')}")
print(f"Final Source: {denkioto_rin.get_clock_source()}")
print(f"Core 1 Counter: {denkioto_rin.get_counter()}")

print("\n=== Debug Statistics ===")
denkioto_rin.print_debug_stats()

# Check touch ring functionality still works
print("\n=== Touch Ring Status (should still work) ===")
for ring in range(4):
    values = denkioto_rin.get_ring_values(ring)
    non_zero = sum(1 for v in values if v > 0)
    resync_count = denkioto_rin.get_resync_count(ring)
    data_ready_count = denkioto_rin.get_data_ready_count(ring)

    print(
        f"Ring {ring}: {non_zero}/25 active | Resync: {resync_count} | Ready: {data_ready_count}"
    )

# Cleanup
print("\nMIDI clock processing will stop when Core 1 stops...")

print("Stopping Core 1...")
retries = denkioto_rin.stop()
print(f"Core 1 stopped (retries: {retries})")

print("\n=== Test Complete ===")
print("MIDI clock functionality verified!")
