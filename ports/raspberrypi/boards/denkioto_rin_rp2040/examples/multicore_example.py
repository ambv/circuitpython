"""
Example script demonstrating the denkioto_rin_rp2040 multicore functionality.

This script shows how to use the second core (core1) of the RP2040 to run
background tasks while the main CircuitPython interpreter runs on core0.
"""

import time
import denkioto_rin


def main():
    print("Denkioto Rin RP2040 Multicore Example")
    print("=====================================")

    # Check if core1 is already running (it shouldn't as it's not automatically started to avoid race conditions)
    if denkioto_rin.is_running():
        print("Core1 is already running!")
    else:
        print("Starting core1...")
        denkioto_rin.start()

    print("\nCore1 is now running an infinite loop, incrementing a counter.")
    print("Press Ctrl+C to stop the demo.\n")

    try:
        # Monitor the counter for 30 seconds
        start_time = time.monotonic()
        last_counter = 0

        while True:
            current_time = time.monotonic()
            elapsed = current_time - start_time

            # Get current counter value
            counter = denkioto_rin.get_counter()

            # Calculate increments per second
            if elapsed > 0:
                rate = counter / elapsed
                increments_since_last = counter - last_counter
                print(
                    f"Time: {elapsed:6.1f}s | Counter: {counter:10d} | Rate: {rate:8.0f}/sec | Delta: {increments_since_last:6d}"
                )

            last_counter = counter
            time.sleep(1.0)

    except KeyboardInterrupt:
        print("\nDemo interrupted by user.")

    finally:
        print("\nDemo complete!")
        print("Note: Core1 will continue running in the background.")
        print("To stop core1: denkioto_rin.stop()")
        print("To restart core1: denkioto_rin.start()")
        print("To reset counter: denkioto_rin.reset_counter()")


if __name__ == "__main__":
    main()
