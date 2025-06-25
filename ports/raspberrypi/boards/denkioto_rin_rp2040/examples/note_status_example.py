"""Example showing the improved note status with 1 bit per note

This demonstrates the new note_status array format where each of the 128 MIDI
notes has its own bit, providing instant per-note status checking.
"""

import time
import denkioto_rin


def is_note_on(note_status, note):
    """Check if a specific note is on using the 4-element status array

    Args:
        note_status: List of 4 uint32 values from get_note_status()
        note: MIDI note number (0-127)

    Returns:
        True if note is on, False if off
    """
    array_index = note // 32
    bit_index = note % 32
    return bool(note_status[array_index] & (1 << bit_index))


def get_active_notes(note_status):
    """Get list of all active notes from the status array

    Args:
        note_status: List of 4 uint32 values from get_note_status()

    Returns:
        List of active note numbers
    """
    active = []
    for i in range(4):
        if note_status[i] != 0:  # Quick check if any notes in this range
            for bit in range(32):
                if note_status[i] & (1 << bit):
                    active.append(i * 32 + bit)
    return active


def note_to_name(note):
    """Convert MIDI note number to note name"""
    names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    octave = note // 12 - 1
    return f"{names[note % 12]}{octave}"


# Start core 1
denkioto_rin.start()
print("Core 1 started - monitoring MIDI notes with 1-bit-per-note precision\n")

# Monitor specific notes
watch_notes = [60, 64, 67]  # C major chord (C4, E4, G4)
print(f"Watching notes: {[f'{note} ({note_to_name(note)})' for note in watch_notes]}")
print("Play notes on MIDI channel 1 to see individual note tracking\n")

channel = 0  # MIDI channel 1 (0-indexed)
prev_status = [0, 0, 0, 0]

try:
    for _ in range(200):  # Run for 20 seconds at 10Hz
        status = denkioto_rin.get_note_status(channel)

        # Check if anything changed
        if status != prev_status:
            print("Note status changed:")

            # Show status of watched notes
            for note in watch_notes:
                is_on = is_note_on(status, note)
                velocity = denkioto_rin.get_note(channel, note)
                print(
                    f"  {note_to_name(note)} (note {note}): {'ON' if is_on else 'OFF'} (vel={velocity})"
                )

            # Show all active notes
            active = get_active_notes(status)
            if active:
                note_names = [note_to_name(n) for n in active]
                print(f"  All active: {', '.join(note_names)}")
            else:
                print("  All notes off")

            # Show raw status array (for debugging)
            print(
                f"  Raw status: [{status[0]:08X}, {status[1]:08X}, {status[2]:08X}, {status[3]:08X}]"
            )
            print()

            prev_status = status

        time.sleep(0.1)

except KeyboardInterrupt:
    print("Stopping...")

denkioto_rin.stop()
print("Done!")
