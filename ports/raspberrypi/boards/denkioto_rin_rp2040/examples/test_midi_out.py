"""Test script for MIDI OUT functionality on Denkioto Rin board.

This script tests the unified MIDI OUT API that sends data to both
USB and UART MIDI outputs.
"""

import denkioto_rin
import time

# Start core1 to enable all MIDI functionality
print("Starting core1...")
denkioto_rin.start()

# Wait a moment for initialization
time.sleep(0.1)

# Test sending MIDI Note On message (C4, velocity 100)
# MIDI message format: [status, note, velocity]
note_on = bytes([0x90, 60, 100])  # Note On, channel 1, middle C, velocity 100
note_off = bytes([0x80, 60, 0])  # Note Off, channel 1, middle C

print("Testing MIDI OUT...")

# Check if MIDI OUT is ready
if denkioto_rin.midi_out_ready(denkioto_rin.MIDI_USB):
    print("USB MIDI OUT is ready")
else:
    print("USB MIDI OUT is not ready")

if denkioto_rin.midi_out_ready(denkioto_rin.MIDI_UART):
    print("UART MIDI OUT is ready")
else:
    print("UART MIDI OUT is not ready")

# Send Note On to USB
print("\nSending Note On (C4) to USB...")
bytes_written = denkioto_rin.midi_out_write(denkioto_rin.MIDI_USB, note_on)
print(f"Wrote {bytes_written} bytes to USB")

time.sleep(1)

# Send Note Off to USB
print("Sending Note Off to USB...")
bytes_written = denkioto_rin.midi_out_write(denkioto_rin.MIDI_USB, note_off)
print(f"Wrote {bytes_written} bytes to USB")

# Test sending to UART
print("\nSending Note On to UART...")
bytes_written = denkioto_rin.midi_out_write(denkioto_rin.MIDI_UART, note_on)
print(f"Wrote {bytes_written} bytes to UART")

time.sleep(1)

# Send Note Off to UART
print("Sending Note Off to UART...")
bytes_written = denkioto_rin.midi_out_write(denkioto_rin.MIDI_UART, note_off)
print(f"Wrote {bytes_written} bytes to UART")

# Test sending MIDI clock messages
print("\nSending MIDI clock pulses...")
clock_msg = bytes([0xF8])  # MIDI Clock

for i in range(24):  # Send 24 clocks (1 beat at 24 PPQ) to both outputs
    denkioto_rin.midi_out_write(denkioto_rin.MIDI_USB, clock_msg)
    denkioto_rin.midi_out_write(denkioto_rin.MIDI_UART, clock_msg)
    time.sleep(0.02083)  # 120 BPM timing (20.83ms per clock)

print("\nMIDI OUT test complete!")

# Clean up
denkioto_rin.stop()
print("Core1 stopped")
