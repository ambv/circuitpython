"""Example demonstrating all UART MIDI state functions

This example shows how to use all the MIDI state tracking functions
available through the denkioto_rin module.
"""

import time
import denkioto_rin

# Start core 1
denkioto_rin.start()
print("Core 1 started - UART MIDI processing active\n")

# Example 1: Monitoring specific MIDI channel
print("Example 1: Monitoring MIDI channel 1")
print("-" * 40)

channel = 0  # MIDI channel 1 (0-indexed)

# Get individual note velocity
note = 60  # Middle C
velocity = denkioto_rin.get_note(channel, note)
print(f"Note {note} velocity: {velocity}")

# Get all note velocities at once
all_notes = denkioto_rin.get_notes(channel)
print(f"Total notes on: {sum(1 for v in all_notes if v > 0)}")

# Get control change values
modwheel = denkioto_rin.get_cc(channel, 1)
volume = denkioto_rin.get_cc(channel, 7)
print(f"Modulation wheel (CC1): {modwheel}")
print(f"Volume (CC7): {volume}")

# Get all CC values at once
all_ccs = denkioto_rin.get_cc_all(channel)
print(f"Non-zero CCs: {sum(1 for v in all_ccs if v > 0)}")

# Get pitch bend (14-bit value, center = 8192)
pitch_bend = denkioto_rin.get_pitch_bend(channel)
pitch_bend_percent = ((pitch_bend - 8192) / 8192) * 100
print(f"Pitch bend: {pitch_bend} ({pitch_bend_percent:+.1f}%)")

# Get channel pressure (aftertouch)
pressure = denkioto_rin.get_channel_pressure(channel)
print(f"Channel pressure: {pressure}")

# Get program number
program = denkioto_rin.get_program(channel)
print(f"Program: {program}")

# Get note status bitmap (efficient for checking activity)
note_status = denkioto_rin.get_note_status(channel)
print(
    f"Note status bitmap: [{note_status[0]:08X}, {note_status[1]:08X}, {note_status[2]:08X}, {note_status[3]:08X}]"
)

print("\n" + "=" * 50 + "\n")

# Example 2: Efficient polling using note_status
print("Example 2: Efficient activity detection")
print("-" * 40)
print("Monitoring all 16 channels for 10 seconds...")
print("Send MIDI data to see it tracked here\n")

start_time = time.time()
prev_status = [[0, 0, 0, 0] for _ in range(16)]
active_channels = set()

while time.time() - start_time < 10:
    for ch in range(16):
        status = denkioto_rin.get_note_status(ch)
        if status != prev_status[ch]:
            # Check if any notes are active (any non-zero value in status array)
            has_active_notes = any(s > 0 for s in status)
            if has_active_notes and ch not in active_channels:
                active_channels.add(ch)
                print(f"Channel {ch + 1} became active")
            elif not has_active_notes and ch in active_channels:
                active_channels.remove(ch)
                print(f"Channel {ch + 1} became inactive")
            prev_status[ch] = status

    time.sleep(0.05)

print("\n" + "=" * 50 + "\n")

# Example 3: Tracking chord changes
print("Example 3: Chord detection")
print("-" * 40)
print("Play chords on channel 1 to see them detected\n")


def get_note_names(notes):
    """Convert active notes to note names"""
    note_names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    active = []
    for note, vel in enumerate(notes):
        if vel > 0:
            octave = note // 12 - 1
            name = note_names[note % 12]
            active.append(f"{name}{octave}")
    return active


channel = 0
prev_chord = []
print("Monitoring for 10 seconds...")

start_time = time.time()
while time.time() - start_time < 10:
    notes = denkioto_rin.get_notes(channel)
    current_chord = get_note_names(notes)

    if current_chord != prev_chord:
        if current_chord:
            print(f"Chord: {' '.join(current_chord)}")
        else:
            print("No notes")
        prev_chord = current_chord

    time.sleep(0.1)

# Stop core 1
print("\nStopping core 1...")
denkioto_rin.stop()
print("Done!")
