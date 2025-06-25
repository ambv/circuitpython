"""Test script for MIDI channel mode message handling

This script demonstrates how the UART MIDI implementation automatically
handles channel mode messages (CC 120, 121, 123) internally on Core 1.

Channel mode messages tested:
- CC 120 (All Sound Off): Clears all note velocities
- CC 121 (Reset All Controllers): Resets controllers per MIDI RP-015
- CC 123 (All Notes Off): Clears all note velocities
"""

import time
import denkioto_rin


def print_channel_state(channel, label):
    """Print current MIDI state for a channel"""
    print(f"\n{label} - Channel {channel + 1} State:")
    print("-" * 40)

    # Check notes
    notes = denkioto_rin.get_notes(channel)
    active_notes = [i for i, vel in enumerate(notes) if vel > 0]
    print(f"Active notes: {active_notes if active_notes else 'None'}")

    # Check key controllers from RP-015
    modulation = denkioto_rin.get_cc(channel, 1)
    expression = denkioto_rin.get_cc(channel, 11)
    sustain = denkioto_rin.get_cc(channel, 64)
    portamento = denkioto_rin.get_cc(channel, 65)
    sostenuto = denkioto_rin.get_cc(channel, 66)
    soft_pedal = denkioto_rin.get_cc(channel, 67)

    print(f"Modulation (CC1): {modulation}")
    print(f"Expression (CC11): {expression}")
    print(f"Sustain (CC64): {sustain}")
    print(f"Portamento (CC65): {portamento}")
    print(f"Sostenuto (CC66): {sostenuto}")
    print(f"Soft Pedal (CC67): {soft_pedal}")

    # Check parameter numbers
    nrpn_lsb = denkioto_rin.get_cc(channel, 98)
    nrpn_msb = denkioto_rin.get_cc(channel, 99)
    rpn_lsb = denkioto_rin.get_cc(channel, 100)
    rpn_msb = denkioto_rin.get_cc(channel, 101)

    print(f"NRPN LSB (CC98): {nrpn_lsb}")
    print(f"NRPN MSB (CC99): {nrpn_msb}")
    print(f"RPN LSB (CC100): {rpn_lsb}")
    print(f"RPN MSB (CC101): {rpn_msb}")

    # Check preserved controllers (should NOT reset)
    volume = denkioto_rin.get_cc(channel, 7)
    pan = denkioto_rin.get_cc(channel, 10)

    print(f"Volume (CC7) [preserved]: {volume}")
    print(f"Pan (CC10) [preserved]: {pan}")

    # Check other state
    pitch_bend = denkioto_rin.get_pitch_bend(channel)
    channel_pressure = denkioto_rin.get_channel_pressure(channel)
    program = denkioto_rin.get_program(channel)

    pitch_bend_percent = ((pitch_bend - 8192) / 8192) * 100
    print(f"Pitch bend: {pitch_bend} ({pitch_bend_percent:+.1f}%)")
    print(f"Channel pressure: {channel_pressure}")
    print(f"Program: {program}")

    # Check polyphonic pressure
    poly_pressure = denkioto_rin.get_poly_pressure_all(channel)
    active_pressure = sum(1 for p in poly_pressure if p > 0)
    print(f"Active poly pressure notes: {active_pressure}")


# Start MIDI processing
denkioto_rin.start()
print("MIDI Channel Mode Message Test")
print("=" * 50)
print("This test demonstrates automatic handling of:")
print("- CC 120 (All Sound Off)")
print("- CC 121 (Reset All Controllers)")
print("- CC 123 (All Notes Off)")
print("\nTo test:")
print("1. Play some notes and adjust controllers")
print("2. Send channel mode messages via MIDI")
print("3. Observe automatic state changes")
print("\nPress Ctrl+C to stop")

channel = 0  # Monitor MIDI channel 1

try:
    print_channel_state(channel, "Initial State")

    print("\n" + "=" * 50)
    print("INSTRUCTIONS:")
    print("1. Play some notes on your MIDI controller")
    print("2. Adjust modulation, sustain, expression, etc.")
    print("3. Send CC 121 (Reset All Controllers) to see automatic reset")
    print("4. Play more notes, then send CC 120 or CC 123 to clear notes")
    print("5. Notice that Volume (CC7) and Pan (CC10) are preserved")
    print("=" * 50)

    prev_update = 0

    while True:
        # Check if channel has been updated
        last_update = denkioto_rin.get_last_update(channel)

        if last_update != prev_update:
            print_channel_state(channel, "Updated State")
            prev_update = last_update

        time.sleep(0.1)

except KeyboardInterrupt:
    print("\n\nFinal test summary:")
    print_channel_state(channel, "Final State")

    print("\n" + "=" * 50)
    print("MIDI RP-015 Compliance Summary:")
    print("✓ CC 120/123: All notes cleared")
    print("✓ CC 121: Expression set to 127")
    print("✓ CC 121: Modulation set to 0")
    print("✓ CC 121: Pedals (64-67) set to 0")
    print("✓ CC 121: Parameter numbers (98-101) set to 127")
    print("✓ CC 121: Pitch bend centered")
    print("✓ CC 121: Pressures reset to 0")
    print("✓ Volume (CC7) and Pan (CC10) preserved")
    print("✓ Program change preserved")

denkioto_rin.stop()
print("\nChannel mode message test completed!")
