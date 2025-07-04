"""
Test script for non-blocking NeoPixel dual output on Denkioto Rin board.

This demonstrates:
1. Simultaneous updates to two LED strips
2. Non-blocking behavior (updates are dropped if busy)
3. Different patterns on each strip
"""

import board
import digitalio
import denkioto_rin
import time
import gc

# Configuration
NUM_PIXELS = 64  # Each ring has 64 LEDs
BRIGHTNESS = 0.1  # Keep it dim for testing

# Initialize the LED pins as digital outputs
led_pins = [board.LEDPIN0, board.LEDPIN1, board.LEDPIN2, board.LEDPIN3]
leds = []
for led_pin in led_pins:
    led = digitalio.DigitalInOut(led_pin)
    led.direction = digitalio.Direction.OUTPUT
    led.value = False
    leds.append(led)

# Start the multicore system (required for non-blocking NeoPixel)
denkioto_rin.start()


# Color wheel helper
def wheel(pos):
    """Generate rainbow colors across 0-255 positions."""
    if pos < 85:
        return (int(pos * 3 * BRIGHTNESS), int((255 - pos * 3) * BRIGHTNESS), 0)
    elif pos < 170:
        pos -= 85
        return (int((255 - pos * 3) * BRIGHTNESS), 0, int(pos * 3 * BRIGHTNESS))
    else:
        pos -= 170
        return (0, int(pos * 3 * BRIGHTNESS), int((255 - pos * 3) * BRIGHTNESS))


# Create pixel buffers
pixels = [bytearray(NUM_PIXELS * 3) for _ in range(4)]  # 3 bytes per pixel (RGB)

# Main animation loop
print("Starting non-blocking NeoPixel demo...")
print("LED1 will show a rainbow chase")
print("LED2 will show a complementary pattern")
print()

frame_count = 0
dropped_frames = 0
last_update = time.monotonic()


def update_pixels(odd, offset):
    # Pattern 1: Rainbow chase
    for i in range(NUM_PIXELS):
        color_pos = (i * 256 // NUM_PIXELS + offset) & 255
        r, g, b = wheel(color_pos)
        pixels[0 + odd][i * 3] = g  # NeoPixels are GRB
        pixels[0 + odd][i * 3 + 1] = r
        pixels[0 + odd][i * 3 + 2] = b

    # Pattern 2: Inverse rainbow with sparkles
    for i in range(NUM_PIXELS):
        color_pos = (255 - (i * 256 // NUM_PIXELS)) & 255
        r, g, b = wheel(color_pos)
        # Add random sparkles
        if i == (offset // 4) % NUM_PIXELS:
            r, g, b = int(255), int(255), int(255)
        pixels[1 + odd][i * 3] = g
        pixels[1 + odd][i * 3 + 1] = r
        pixels[1 + odd][i * 3 + 2] = b


try:
    odd = 0
    offset = 0
    update_pixels(odd, offset)

    while True:
        # Only try to update if we're not busy
        if not denkioto_rin.neopixel_nb_is_busy():
            # Try to update both strips simultaneously
            success = denkioto_rin.neopixel_nb_write_dual(
                leds[0 + odd],
                pixels[0 + odd],
                leds[1 + odd],
                pixels[1 + odd],
            )

            frame_count += 1
            odd = 2 - odd
            # Only advance the animation when we successfully send a frame
            offset = (offset + 1) % 256
            update_pixels(odd, offset)

        # Print stats every second
        now = time.monotonic()
        if now - last_update >= 1.0:
            fps = frame_count / (now - last_update)
            drop_rate = (
                dropped_frames / (frame_count + dropped_frames) * 100
                if frame_count + dropped_frames > 0
                else 0
            )
            print(
                f"FPS: {fps:.1f}, Frames: {frame_count}, Dropped: {dropped_frames} ({drop_rate:.1f}%)"
            )

            # Check memory
            gc.collect()
            print(f"Free memory: {gc.mem_free()} bytes")
            print()

            last_update = now
            frame_count = 0
            dropped_frames = 0
        else:
            dropped_frames += 1


except KeyboardInterrupt:
    print("\nStopping demo...")

    # Wait for any in-progress updates to complete
    while denkioto_rin.neopixel_nb_is_busy():
        pass

    # Turn off all LEDs
    for pix in range(4):
        for i in range(NUM_PIXELS * 3):
            pixels[pix][i] = 0

    # Final update to turn off
    denkioto_rin.neopixel_nb_write_dual(leds[0], pixels[0], leds[1], pixels[1])
    while denkioto_rin.neopixel_nb_is_busy():
        pass

    denkioto_rin.neopixel_nb_write_dual(leds[2], pixels[2], leds[3], pixels[3])
    while denkioto_rin.neopixel_nb_is_busy():
        pass

    # Stop multicore
    denkioto_rin.stop()

    print("Demo stopped")
