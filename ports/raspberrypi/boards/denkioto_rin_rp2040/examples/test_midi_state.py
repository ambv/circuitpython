"""Test script for UART MIDI state tracking on Core 1

This script tests the new MIDI state handling functionality that runs on Core 1.
It demonstrates how to:
1. Monitor note on/off events
2. Track control change values
3. Monitor pitch bend
4. Use the efficient note_status bitmap for quick checks
5. Observe automatic channel mode message handling (CC 120, 121, 123)
"""

import time
import denkioto_rin

# Start core 1 with MIDI processing
denkioto_rin.start()
print("Core 1 started - UART MIDI state tracking active")
print("Note: All API calls now use denkioto_rin.MIDI_UART source parameter")

# Give core 1 time to initialize
time.sleep(0.5)

# Main monitoring loop
print("\nMonitoring MIDI state (send MIDI to DIN MIDI IN):")
print(
    "Try channel mode messages: CC 120 (All Sound Off), CC 121 (Reset All), CC 123 (All Notes Off)"
)
print("Press Ctrl+C to stop\n")

# Track previous note status for each channel to detect changes
prev_note_status = [[0, 0, 0, 0] for _ in range(16)]

try:
    while True:
        # Check each MIDI channel
        for channel in range(16):
            # Get note status bitmap - efficient way to check for activity
            note_status = denkioto_rin.get_note_status(denkioto_rin.MIDI_UART, channel)

            # If note status changed, report details
            if note_status != prev_note_status[channel]:
                print(f"\nChannel {channel + 1} activity detected:")

                # Get all note velocities for this channel (using UART MIDI source)
                notes = denkioto_rin.get_notes(denkioto_rin.MIDI_UART, channel)

                # Find and display active notes
                active_notes = []
                for note in range(128):
                    if notes[note] > 0:
                        active_notes.append(f"Note {note} (vel={notes[note]})")

                if active_notes:
                    print(f"  Active notes: {', '.join(active_notes)}")
                else:
                    print("  All notes off")

                # Also check other MIDI data
                pitch_bend = denkioto_rin.get_pitch_bend(denkioto_rin.MIDI_UART, channel)
                if pitch_bend != 8192:  # 8192 is center position
                    print(f"  Pitch bend: {pitch_bend}")

                program = denkioto_rin.get_program(denkioto_rin.MIDI_UART, channel)
                if program > 0:
                    print(f"  Program: {program}")

                # Check some common CCs
                cc_volume = denkioto_rin.get_cc(denkioto_rin.MIDI_UART, channel, 7)  # Volume
                cc_pan = denkioto_rin.get_cc(denkioto_rin.MIDI_UART, channel, 10)  # Pan
                cc_expr = denkioto_rin.get_cc(denkioto_rin.MIDI_UART, channel, 11)  # Expression

                if cc_volume > 0:
                    print(f"  Volume (CC7): {cc_volume}")
                if cc_pan != 64:  # 64 is center
                    print(f"  Pan (CC10): {cc_pan}")
                if cc_expr > 0:
                    print(f"  Expression (CC11): {cc_expr}")

                # Check sustain pedal
                cc_sustain = denkioto_rin.get_cc(denkioto_rin.MIDI_UART, channel, 64)
                if cc_sustain >= 64:
                    print("  Sustain pedal: ON")

                # Check for polyphonic aftertouch
                poly_pressure = denkioto_rin.get_poly_pressure_all(denkioto_rin.MIDI_UART, channel)
                pressure_notes = [(i, p) for i, p in enumerate(poly_pressure) if p > 0]
                if pressure_notes:
                    pressure_str = ", ".join(
                        [f"note {n}:{p}" for n, p in pressure_notes[:3]]
                    )  # Show first 3
                    if len(pressure_notes) > 3:
                        pressure_str += f" (+{len(pressure_notes) - 3} more)"
                    print(f"  Poly pressure: {pressure_str}")

                prev_note_status[channel] = note_status

        # Small delay to avoid overwhelming the output
        time.sleep(0.1)

except KeyboardInterrupt:
    print("\nStopping...")
    denkioto_rin.stop()
    print("Core 1 stopped")
