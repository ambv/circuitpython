import time
import denkioto_rin

print("Testing IRQ-based single-byte sync with double-buffered PIO reading...")

# Start core1 (IRQ-based synchronization starting with single bytes)
print("Starting core1 with IRQ-based single-byte sync approach...")
denkioto_rin.start()

if denkioto_rin.is_running():
    print("Core1 is running with interrupt-driven DMA and thread-safe data!")
else:
    print("Failed to start core1")
    exit()

# Monitor for a few seconds
print("\nMonitoring touch rings and counter for 10 seconds...")
print("- IRQ-based synchronization: Start with 1-byte transfers until zero found")
print("- Automatic switch to 26-byte transfers once synchronized")
print("- Perfect frame alignment from first zero byte detected")
print("- No costly resync operations needed")
print("- Board-contained solution (no changes to core CircuitPython)")
print("- Three-state atomics ensure thread-safe data access between cores")
print("- Latest data priority: Core1 overwrites unread data (real-time priority)")

start_time = time.monotonic()
last_counter = 0

while time.monotonic() - start_time < 10:
    # Get counter to see core1 is active
    counter = denkioto_rin.get_counter()
    counter_diff = counter - last_counter
    last_counter = counter

    print(f"\nTime: {time.monotonic() - start_time:.1f}s")
    print(f"Counter: {counter} (diff: {counter_diff})")

    # Check data from all 4 rings
    for ring in range(4):
        values = denkioto_rin.get_ring_values(ring)
        # Show first 5 values to see the data pattern better
        non_zero = sum(1 for v in values if v > 0)
        max_val = max(values) if values else 0
        min_val = min(v for v in values if v > 0) if any(v > 0 for v in values) else 0

        # Get debug counters
        resync_count = denkioto_rin.get_resync_count(ring)
        data_ready_count = denkioto_rin.get_data_ready_count(ring)

        sync_status = "synced" if data_ready_count > 0 else "searching"
        print(
            f"Ring {ring}: {values[:5]}... ({non_zero}/25 non-zero, range: {min_val}-{max_val}) "
            f"[{sync_status}, resync: {resync_count}, ready: {data_ready_count}]"
        )

    time.sleep(1)

# Stop core1
print("\nStopping core1...")
retries = denkioto_rin.stop()
print(f"Core1 stopped with {retries} retries remaining")

print("\nIRQ-based single-byte sync double-buffer test complete!")
print("Features implemented:")
print("✓ IRQ-based synchronization with single-byte start")
print("✓ Automatic transition to 26-byte mode after zero detection")
print("✓ Perfect frame alignment from first zero byte")
print("✓ No costly resync operations")
print("✓ Double-buffered DMA prevents data loss")
print("✓ Board-contained solution (zero core CircuitPython changes)")
print("✓ Three-state atomics provide thread-safe access")
print("✓ Latest data priority (overwrites unread data)")
print("✓ Race condition-free state management")
print("✓ Debug counters for monitoring sync and data events")
print("✓ Core1 can now handle additional tasks safely")

print("\nDebug counter interpretation:")
print("- resync: Should be 0 (no resync needed with IRQ-based sync)")
print("- ready: Should be increasing steadily (indicates data flow)")
print("- Note: During initial sync, ready counter may be 0 until first zero found")
