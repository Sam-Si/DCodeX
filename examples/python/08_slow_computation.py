#!/usr/bin/env python3
"""
Slow Computation Example
This script performs CPU-intensive work that may exceed time limits.
The sandbox has a 2-second CPU time limit.
"""

import math
import time


def main():
    print("Slow Computation Test")
    print("=" * 21)
    print("Sandbox CPU limit: 2 seconds")
    print("This computation may exceed the time limit...")
    
    start_time = time.time()
    
    # CPU-intensive computation: nested loops with heavy math
    result = 0.0
    iterations = 0
    
    try:
        for i in range(1, 10000000):
            for j in range(1, 100):
                result += math.sqrt(i) * math.sin(j) * math.cos(j)
                iterations += 1
                
            if i % 100000 == 0:
                elapsed = time.time() - start_time
                print(f"Iteration {i}: elapsed time = {elapsed:.2f}s")
                
    except Exception as e:
        print(f"Computation interrupted: {e}")
    
    end_time = time.time()
    total_time = end_time - start_time
    
    print(f"\nComputation statistics:")
    print(f"  Total iterations: {iterations}")
    print(f"  Result: {result}")
    print(f"  Total time: {total_time:.2f} seconds")
    
    if total_time > 2.0:
        print("  WARNING: Exceeded sandbox CPU limit!")
    
    print("\nProgram completed (if you see this, it didn't time out)")


if __name__ == "__main__":
    main()