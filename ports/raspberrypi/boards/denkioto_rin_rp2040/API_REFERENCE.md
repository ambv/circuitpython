# UART MIDI State API Reference

## Note Functions

### `get_note(source, channel, note) -> int`
Get velocity of a specific note on a MIDI channel.
- **source**: MIDI source (MIDI_UART or MIDI_USB)
- **channel**: MIDI channel (0-15)
- **note**: Note number (0-127)
- **Returns**: Velocity (0=off, 1-127=on with velocity)

### `get_notes(source, channel) -> list[int]`
Get all note velocities for a MIDI channel.
- **source**: MIDI source (MIDI_UART or MIDI_USB)
- **channel**: MIDI channel (0-15)
- **Returns**: List of 128 velocities (0=off, 1-127=on)

## Control Change Functions

### `get_cc(source, channel, cc) -> int`
Get a control change value for a MIDI channel.
- **source**: MIDI source (MIDI_UART or MIDI_USB)
- **channel**: MIDI channel (0-15)
- **cc**: Control change number (0-127)
- **Returns**: CC value (0-127)

### `get_cc_all(source, channel) -> list[int]`
Get all control change values for a MIDI channel.
- **source**: MIDI source (MIDI_UART or MIDI_USB)
- **channel**: MIDI channel (0-15)
- **Returns**: List of 128 CC values (0-127)

## Polyphonic Aftertouch Functions

### `get_poly_pressure(source, channel, note) -> int`
Get the polyphonic aftertouch pressure for a specific note.
- **source**: MIDI source (MIDI_UART or MIDI_USB)
- **channel**: MIDI channel (0-15)
- **note**: Note number (0-127)
- **Returns**: Pressure value (0-127)

### `get_poly_pressure_all(source, channel) -> list[int]`
Get all polyphonic aftertouch pressure values for a MIDI channel.
- **source**: MIDI source (MIDI_UART or MIDI_USB)
- **channel**: MIDI channel (0-15)
- **Returns**: List of 128 pressure values (0-127)

## Other MIDI State Functions

### `get_pitch_bend(source, channel) -> int`
Get the pitch bend value for a MIDI channel.
- **source**: MIDI source (MIDI_UART or MIDI_USB)
- **channel**: MIDI channel (0-15)
- **Returns**: Pitch bend value (0-16383, center=8192)

### `get_channel_pressure(source, channel) -> int`
Get the channel pressure (aftertouch) for a MIDI channel.
- **source**: MIDI source (MIDI_UART or MIDI_USB)
- **channel**: MIDI channel (0-15)
- **Returns**: Channel pressure value (0-127)

### `get_program(source, channel) -> int`
Get the program (patch) number for a MIDI channel.
- **source**: MIDI source (MIDI_UART or MIDI_USB)
- **channel**: MIDI channel (0-15)
- **Returns**: Program number (0-127)

### `get_note_status(source, channel) -> list[int]`
Get the note status bitmap for a MIDI channel.
- **source**: MIDI source (MIDI_UART or MIDI_USB)
- **channel**: MIDI channel (0-15)
- **Returns**: List of 4 uint32 values representing 128 note bits

Each bit represents one note: `status[0]` bits 0-31 = notes 0-31, etc.
Use `(status[note//32] & (1 << (note%32)))` to check if note is on.

## Automatic Channel Mode Message Handling

The UART MIDI implementation automatically handles these channel mode messages internally on Core 1:

### CC 120 (All Sound Off)
- Clears all note velocities for the channel
- Sets all notes to velocity 0

### CC 121 (Reset All Controllers)
Resets controllers according to MIDI Recommended Practice RP-015:
- **Expression (CC11)**: Set to 127
- **Modulation (CC1)**: Set to 0
- **Pedals (CC64-67)**: Set to 0 (Sustain, Portamento, Sostenuto, Soft Pedal)
- **Parameter Numbers (CC98-101)**: Set to 127 (NRPN/RPN LSB/MSB)
- **Pitch Bend**: Centered to 8192
- **Channel Pressure**: Reset to 0
- **Polyphonic Pressure**: Reset to 0 for all notes

**Preserved (NOT reset)**:
- Bank Select (CC0/32)
- Volume (CC7)
- Pan (CC10)
- Program Change
- Effect Controllers (CC91-95)
- Sound Controllers (CC70-79)

### CC 123 (All Notes Off)
- Clears all note velocities for the channel
- Sets all notes to velocity 0

These messages are processed automatically and do not appear in the CC array as regular control changes.

## Example Usage

```python
import denkioto_rin

# Start MIDI processing
denkioto_rin.start()

# Check if middle C is playing on UART MIDI channel 1
velocity = denkioto_rin.get_note(denkioto_rin.MIDI_UART, 0, 60)  # Channel 0 = MIDI channel 1
if velocity > 0:
    print(f"Middle C is on with velocity {velocity}")

# Get all active notes on UART MIDI channel 1
notes = denkioto_rin.get_notes(denkioto_rin.MIDI_UART, 0)
active_notes = [i for i, vel in enumerate(notes) if vel > 0]
print(f"Active notes: {active_notes}")

# Check volume control
volume = denkioto_rin.get_cc(denkioto_rin.MIDI_UART, 0, 7)  # CC7 = volume
print(f"Volume: {volume}")

# Check polyphonic aftertouch for middle C
poly_pressure = denkioto_rin.get_poly_pressure(denkioto_rin.MIDI_UART, 0, 60)
print(f"Middle C poly pressure: {poly_pressure}")

# Efficient note activity detection using bitmap
status = denkioto_rin.get_note_status(denkioto_rin.MIDI_UART, 0)
has_notes = any(s > 0 for s in status)
print(f"Channel has active notes: {has_notes}")

denkioto_rin.stop()
```
