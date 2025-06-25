"""Example demonstrating polyphonic aftertouch support

This script shows how to monitor and use polyphonic aftertouch (per-note pressure)
data from MIDI controllers that support it.

Polyphonic aftertouch allows each pressed key to have independent pressure sensing,
unlike channel aftertouch which affects the entire channel.
"""

import time
import denkioto_rin


def note_to_name(note):
    """Convert MIDI note number to note name"""
    names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    octave = note // 12 - 1
    return f"{names[note % 12]}{octave}"


def get_pressure_notes(pressure_values):
    """Get list of notes with non-zero polyphonic pressure"""
    pressure_notes = []
    for note, pressure in enumerate(pressure_values):
        if pressure > 0:
            pressure_notes.append((note, pressure))
    return pressure_notes


# Start MIDI processing
denkioto_rin.start()
print("Polyphonic Aftertouch Monitor")
print("=" * 40)
print("Play notes with pressure-sensitive keys on MIDI channel 1")
print("Press Ctrl+C to stop\n")

channel = 0  # MIDI channel 1 (0-indexed)
prev_pressure_notes = []

try:
    while True:
        # Get all polyphonic pressure values
        pressure_values = denkioto_rin.get_poly_pressure_all(channel)
        current_pressure_notes = get_pressure_notes(pressure_values)

        # Check for changes in polyphonic pressure
        if current_pressure_notes != prev_pressure_notes:
            if current_pressure_notes:
                print("Polyphonic Aftertouch Activity:")
                for note, pressure in current_pressure_notes:
                    note_name = note_to_name(note)
                    # Also show note velocity for context
                    velocity = denkioto_rin.get_note(channel, note)
                    print(f"  {note_name} (note {note}): pressure={pressure}, velocity={velocity}")
                print()
            else:
                print("No polyphonic pressure\n")

            prev_pressure_notes = current_pressure_notes

        # Also demonstrate individual note pressure checking
        # Check pressure on some common notes (C4, E4, G4 - C major chord)
        for note in [60, 64, 67]:  # C4, E4, G4
            pressure = denkioto_rin.get_poly_pressure(channel, note)
            if pressure > 0:
                velocity = denkioto_rin.get_note(channel, note)
                if velocity > 0:  # Note is currently being played
                    note_name = note_to_name(note)
                    print(f"Real-time: {note_name} vel={velocity} pressure={pressure}")

        time.sleep(0.1)

except KeyboardInterrupt:
    print("\nStopping monitor...")

    # Show final state
    print("\nFinal polyphonic pressure state:")
    pressure_values = denkioto_rin.get_poly_pressure_all(channel)
    pressure_notes = get_pressure_notes(pressure_values)

    if pressure_notes:
        for note, pressure in pressure_notes:
            print(f"  {note_to_name(note)}: {pressure}")
    else:
        print("  No pressure detected")

denkioto_rin.stop()
print("Done!")

print("\nNOTE: Polyphonic aftertouch requires a MIDI controller that supports")
print("per-key pressure sensing. Many controllers only support channel aftertouch.")
print("Test with a high-end synthesizer or controller with aftertouch capabilities.")
