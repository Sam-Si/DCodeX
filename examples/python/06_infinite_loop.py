#!/usr/bin/env python3
"""
Infinite Loop Example
This script runs an infinite loop that will be terminated by the sandbox.
The sandbox has a 1-second CPU time limit.
"""


def main():
    print("Starting infinite loop...")
    print("This program will be terminated by the sandbox timeout.")
    print("Sandbox CPU limit: 1 second")
    
    counter = 0
    
    # Infinite loop - will exceed CPU time limit
    while True:
        counter += 1
        if counter % 1000000 == 0:
            print(f"Iteration: {counter}")
    
    # This line will never be reached
    print("Program completed (should not see this)")


if __name__ == "__main__":
    main()