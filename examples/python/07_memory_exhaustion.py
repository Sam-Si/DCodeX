#!/usr/bin/env python3
"""
Bounded Memory Allocation Example
This script allocates memory in a bounded loop.
The loop is capped at 100000000 iterations for safety.
"""


def main() -> int:
    """Main function with bounded memory allocation loop."""
    MAX_ITERATIONS: int = 100_000_000
    CHUNK_SIZE: int = 50 * 1024 * 1024  # 50 MB per chunk

    print("Bounded Memory Allocation Test")
    print("=" * 30)
    print(f"Max iterations: {MAX_ITERATIONS:,}")
    print(f"Chunk size: {CHUNK_SIZE // (1024 * 1024)} MB")
    print("Attempting to allocate memory...")

    allocations: list[str] = []
    iteration: int = 0
    
    try:
        while iteration < MAX_ITERATIONS:
            chunk_num = iteration + 1
            print(f"Allocating chunk {chunk_num} ({CHUNK_SIZE // (1024 * 1024)} MB)...")
            # Allocate a large string of 'X' characters
            allocations.append("X" * CHUNK_SIZE)
            total_mb = chunk_num * CHUNK_SIZE // (1024 * 1024)
            print(f"Total allocated: {total_mb} MB")
            iteration += 1
        print(f"Reached iteration limit of {MAX_ITERATIONS:,}")
    except MemoryError as e:
        print(f"Memory allocation failed after {iteration} iterations: {e}")

    print(f"Program completed. Total iterations: {iteration}")
    return 0


if __name__ == "__main__":
    import sys

    sys.exit(main())
