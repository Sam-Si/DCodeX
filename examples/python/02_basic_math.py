#!/usr/bin/env python3
"""Standard Python Basic Math Operations
Uses only standard library modules.
"""

import math
import random
import statistics
from typing import List


def calculate_factorial(n: int) -> int:
    """Calculate factorial of n."""
    if n <= 1:
        return 1
    result = 1
    for i in range(2, n + 1):
        result *= i
    return result


def is_prime(n: int) -> bool:
    """Check if a number is prime."""
    if n <= 1:
        return False
    if n <= 3:
        return True
    if n % 2 == 0 or n % 3 == 0:
        return False
    i = 5
    while i * i <= n:
        if n % i == 0 or n % (i + 2) == 0:
            return False
        i += 6
    return True


def main() -> None:
    """Main function demonstrating math operations."""
    print("=" * 50)
    print("Basic Math Operations")
    print("=" * 50)
    
    # Basic arithmetic
    a, b = 15, 4
    print(f"\nBasic Arithmetic (a={a}, b={b}):")
    print(f"  Addition: {a + b}")
    print(f"  Subtraction: {a - b}")
    print(f"  Multiplication: {a * b}")
    print(f"  Division: {a / b:.2f}")
    print(f"  Integer Division: {a // b}")
    print(f"  Modulo: {a % b}")
    print(f"  Power: {a ** b}")
    
    # Math module functions
    print(f"\nMath Module Functions:")
    print(f"  Square root of 16: {math.sqrt(16)}")
    print(f"  Power (2^10): {math.pow(2, 10)}")
    print(f"  Factorial of 5: {math.factorial(5)}")
    print(f"  Pi: {math.pi:.6f}")
    print(f"  Euler's number: {math.e:.6f}")
    
    # Factorial calculation
    print(f"\nFactorials (0-10):")
    for i in range(11):
        print(f"  {i}! = {calculate_factorial(i)}")
    
    # Prime numbers
    print(f"\nPrime numbers up to 50:")
    primes = [n for n in range(2, 51) if is_prime(n)]
    print(f"  {primes}")
    print(f"  Total: {len(primes)} primes")
    
    # Random numbers
    print(f"\nRandom Numbers:")
    random.seed(42)  # For reproducible results
    print(f"  Random integer (1-100): {random.randint(1, 100)}")
    print(f"  Random float: {random.random():.4f}")
    print(f"  Random choice from list: {random.choice(['apple', 'banana', 'cherry'])}")
    
    # Statistics
    data: List[float] = [random.gauss(100, 15) for _ in range(100)]
    print(f"\nStatistics (100 random values):")
    print(f"  Mean: {statistics.mean(data):.2f}")
    print(f"  Median: {statistics.median(data):.2f}")
    print(f"  Std Dev: {statistics.stdev(data):.2f}")
    print(f"  Min: {min(data):.2f}")
    print(f"  Max: {max(data):.2f}")
    
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())