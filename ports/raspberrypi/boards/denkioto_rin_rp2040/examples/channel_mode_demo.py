"""Simple demo showing the differences between channel mode messages

This script shows what each channel mode message does to the MIDI state.
Send the messages from your MIDI controller to see the effects.
"""

import time
import denkioto_rin


def show_key_state(channel):
    """Show the most important MIDI state for a channel"""
    notes = denkioto_rin.get_notes(channel)
    active_notes = sum(1 for vel in notes if vel > 0)

    modulation = denkioto_rin.get_cc(channel, 1)
    volume = denkioto_rin.get_cc(channel, 7)
    expression = denkioto_rin.get_cc(channel, 11)
    sustain = denkioto_rin.get_cc(channel, 64)

    pitch_bend = denkioto_rin.get_pitch_bend(channel)
    is_centered = pitch_bend == 8192

    poly_pressure = denkioto_rin.get_poly_pressure_all(channel)
    active_pressure = sum(1 for p in poly_pressure if p > 0)

    print(
        f"  Notes: {active_notes:2d} | Mod: {modulation:3d} | Vol: {volume:3d} | Exp: {expression:3d} | Sus: {sustain:3d} | PB: {'CTR' if is_centered else 'OFF'} | Pressure: {active_pressure:2d}"
    )


# Start MIDI processing
denkioto_rin.start()
print("MIDI Channel Mode Message Demo")
print("=" * 60)
print("Send these control changes to see their effects:")
print("  CC 120 = All Sound Off (clears notes only)")
print("  CC 121 = Reset All Controllers (resets controllers + clears pressure)")
print("  CC 123 = All Notes Off (clears notes only)")
print()
print("Play notes and adjust controllers, then send channel mode messages")
print("Note: Volume (CC7) is preserved during CC 121 (Reset All Controllers)")
print()
print("Legend: Notes=active count, Mod=modulation, Vol=volume, Exp=expression,")
print("        Sus=sustain, PB=pitch bend center, Pressure=poly pressure count")
print("-" * 60)

channel = 0
prev_update = 0

try:
    while True:
        last_update = denkioto_rin.get_last_update(channel)

        if last_update != prev_update:
            # Something changed, show current state
            timestamp = time.monotonic()
            print(f"{timestamp:6.1f}s", end="")
            show_key_state(channel)
            prev_update = last_update

        time.sleep(0.05)  # Fast refresh for responsive display

except KeyboardInterrupt:
    print("\n\nDemo completed!")
    print("\nSummary of channel mode messages:")
    print("CC 120 (All Sound Off):")
    print("  ✓ Clears all notes")
    print("  • Preserves all controllers")
    print("  • Preserves polyphonic pressure")
    print()
    print("CC 121 (Reset All Controllers):")
    print("  ✓ Resets most controllers per MIDI RP-015")
    print("  ✓ Centers pitch bend")
    print("  ✓ Clears all pressure (channel + polyphonic)")
    print("  • Preserves Volume, Pan, Program, Bank Select")
    print("  • Does NOT clear notes")
    print()
    print("CC 123 (All Notes Off):")
    print("  ✓ Clears all notes")
    print("  • Preserves all controllers")
    print("  • Preserves polyphonic pressure")

denkioto_rin.stop()
print("Done!")
