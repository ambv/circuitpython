"""Example demonstrating the updated MIDI API with source parameter

This script shows how to use the new API that includes a 'source' parameter
for future support of both UART and USB MIDI sources.

Currently only UART MIDI is implemented, so all calls use denkioto_rin.MIDI_UART.
"""

import time
import denkioto_rin

# Start MIDI processing
denkioto_rin.start()
print("MIDI State API with Source Parameter Example")
print("=" * 50)
print(f"Available sources: UART={denkioto_rin.MIDI_UART}, USB={denkioto_rin.MIDI_USB}")
print("Note: Currently only UART MIDI is implemented")
print()

# Use UART MIDI source for all operations
source = denkioto_rin.MIDI_UART
channel = 0  # MIDI channel 1 (0-indexed)

print("Monitoring UART MIDI channel 1 - play some notes and adjust controllers")
print("Press Ctrl+C to stop\n")

try:
    for _ in range(100):  # Monitor for 10 seconds
        # Check for notes using the new API
        notes = denkioto_rin.get_notes(source, channel)
        active_notes = [i for i, vel in enumerate(notes) if vel > 0]

        if active_notes:
            print("Active notes:", end=" ")
            for note in active_notes[:5]:  # Show first 5 notes
                velocity = denkioto_rin.get_note(source, channel, note)
                print(f"{note}(v{velocity})", end=" ")
            if len(active_notes) > 5:
                print(f"(+{len(active_notes) - 5} more)", end="")
            print()

        # Check some common controllers
        volume = denkioto_rin.get_cc(source, channel, 7)
        modulation = denkioto_rin.get_cc(source, channel, 1)
        sustain = denkioto_rin.get_cc(source, channel, 64)

        # Only print if there's activity
        if volume > 0 or modulation > 0 or sustain > 0:
            print(f"Controllers: Vol={volume} Mod={modulation} Sus={sustain}")

        # Check pitch bend
        pitch_bend = denkioto_rin.get_pitch_bend(source, channel)
        if pitch_bend != 8192:  # Not centered
            offset = pitch_bend - 8192
            print(f"Pitch bend: {pitch_bend} (offset: {offset:+d})")

        # Check polyphonic aftertouch
        poly_pressure = denkioto_rin.get_poly_pressure_all(source, channel)
        pressure_notes = sum(1 for p in poly_pressure if p > 0)
        if pressure_notes > 0:
            print(f"Polyphonic pressure on {pressure_notes} notes")

        # Check program
        program = denkioto_rin.get_program(source, channel)
        if program > 0:
            print(f"Program: {program}")

        # Use efficient note status checking
        status = denkioto_rin.get_note_status(source, channel)
        has_notes = any(s > 0 for s in status)

        if (
            not has_notes
            and all(v == 0 for v in [volume, modulation, sustain])
            and pitch_bend == 8192
        ):
            # No activity, just show a dot
            print(".", end="", flush=True)

        time.sleep(0.1)

except KeyboardInterrupt:
    print("\n\nFinal state summary:")
    print("-" * 20)

    # Show final state using new API
    notes = denkioto_rin.get_notes(source, channel)
    active_count = sum(1 for vel in notes if vel > 0)
    print(f"Active notes: {active_count}")

    cc_values = denkioto_rin.get_cc_all(source, channel)
    active_ccs = sum(1 for val in cc_values if val > 0)
    print(f"Non-zero CCs: {active_ccs}")

    pitch_bend = denkioto_rin.get_pitch_bend(source, channel)
    print(f"Pitch bend: {pitch_bend} ({'centered' if pitch_bend == 8192 else 'offset'})")

    program = denkioto_rin.get_program(source, channel)
    print(f"Program: {program}")

denkioto_rin.stop()
print("\nDone!")

print("\nAPI Migration Notes:")
print("- All MIDI state functions now require a 'source' parameter")
print("- Use denkioto_rin.MIDI_UART for UART MIDI (currently implemented)")
print("- Use denkioto_rin.MIDI_USB for USB MIDI (future implementation)")
print("- This prepares the API for unified UART/USB MIDI access")
