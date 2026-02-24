#!/usr/bin/env python3
"""
Memory Exhaustion Example
This script tries to allocate more memory than the sandbox allows.
The sandbox has a 50 MB memory limit.
"""


def main():
    print("Memory Exhaustion Test")
    print("=" * 22)
    print("Sandbox memory limit: 50 MB")
    print("Attempting to allocate large amounts of memory...")
    
    allocations = []
    chunk_size = 10 * 1024 * 1024  # 10 MB per chunk
    
    try:
        i = 1
        while True:
            print(f"Allocating chunk {i} ({chunk_size // (1024 * 1024)} MB)...")
            # Allocate a large string of 'X' characters
            allocations.append("X" * chunk_size)
            total_mb = i * chunk_size // (1024 * 1024)
            print(f"Total allocated: {total_mb} MB")
            i += 1
    except MemoryError as e:
        print(f"Memory allocation failed: {e}")
    
    # This line may or may not be reached
    print("Program reached end (may have been terminated by sandbox)")


if __name__ == "__main__":
    main()