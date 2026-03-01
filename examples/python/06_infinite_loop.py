#!/usr/bin/env python3
"""
Bounded Loop Example
This script runs a bounded CPU-intensive loop.
The loop is capped at 100000000 iterations for safety.
"""


def main() -> int:
    """Main function with bounded CPU-intensive loop."""
    MAX_ITERATIONS: int = 100_000_000

    print("Starting bounded CPU-intensive loop...")
    print(f"Max iterations: {MAX_ITERATIONS:,}")

    counter: int = 0

    # Bounded loop - will complete within iteration limit
    while counter < MAX_ITERATIONS:
        counter += 1
        if counter % 1_000_000 == 0:
            print(f"Iteration: {counter:,}")

    print(f"Program completed. Total iterations: {counter:,}")
    return 0


if __name__ == "__main__":
    import sys

    sys.exit(main())
