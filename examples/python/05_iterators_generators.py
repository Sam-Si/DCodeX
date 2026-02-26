#!/usr/bin/env python3
"""Standard Python Iterators and Generators
Uses only standard library modules.
"""

import itertools
import sys
from typing import Iterator


def count_to_n(n: int) -> Iterator[int]:
    """Generator that counts from 1 to n."""
    for i in range(1, n + 1):
        yield i


def fibonacci(n: int) -> Iterator[int]:
    """Generator that yields first n Fibonacci numbers."""
    a, b = 0, 1
    for _ in range(n):
        yield a
        a, b = b, a + b


def infinite_counter(start: int = 0) -> Iterator[int]:
    """Infinite counter generator."""
    while True:
        yield start
        start += 1


def demonstrate_generators() -> None:
    """Demonstrate generator functions."""
    print("=" * 50)
    print("Generator Functions")
    print("=" * 50)
    
    # Simple generator
    print("\nCounting to 5:")
    for num in count_to_n(5):
        print(f"  {num}")
    
    # Fibonacci generator
    print("\nFirst 15 Fibonacci numbers:")
    fibs = list(fibonacci(15))
    print(f"  {fibs}")
    
    # Generator with limited infinite sequence
    print("\nFirst 10 values from infinite counter (starting at 100):")
    counter = infinite_counter(100)
    for _ in range(10):
        print(f"  {next(counter)}", end=" ")
    print()


def demonstrate_iterators() -> None:
    """Demonstrate iterator operations."""
    print("\n" + "=" * 50)
    print("Iterator Operations")
    print("=" * 50)
    
    # Using iter() and next()
    numbers = [1, 2, 3, 4, 5]
    iterator = iter(numbers)
    
    print("\nManual iteration:")
    try:
        while True:
            print(f"  {next(iterator)}")
    except StopIteration:
        print("  (Iterator exhausted)")
    
    # enumerate
    fruits = ["apple", "banana", "cherry"]
    print("\nUsing enumerate:")
    for index, fruit in enumerate(fruits, start=1):
        print(f"  {index}. {fruit}")
    
    # zip
    names = ["Alice", "Bob", "Charlie"]
    ages = [25, 30, 35]
    print("\nUsing zip:")
    for name, age in zip(names, ages):
        print(f"  {name} is {age} years old")
    
    # map and filter
    numbers = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
    squared = list(map(lambda x: x**2, numbers))
    evens = list(filter(lambda x: x % 2 == 0, numbers))
    
    print(f"\nOriginal: {numbers}")
    print(f"Squared: {squared}")
    print(f"Evens: {evens}")


def demonstrate_itertools() -> None:
    """Demonstrate itertools module."""
    print("\n" + "=" * 50)
    print("Itertools Module")
    print("=" * 50)
    
    # count
    print("\nitertools.count (first 5):")
    for i in itertools.count(10, 2):
        if i > 18:
            break
        print(f"  {i}")
    
    # cycle
    print("\nitertools.cycle (first 6):")
    colors = ["red", "green", "blue"]
    cycle_count = 0
    for color in itertools.cycle(colors):
        print(f"  {color}")
        cycle_count += 1
        if cycle_count >= 6:
            break
    
    # repeat
    print("\nitertools.repeat:")
    repeated = list(itertools.repeat("hello", 3))
    print(f"  {repeated}")
    
    # chain
    print("\nitertools.chain:")
    chained = list(itertools.chain([1, 2, 3], [4, 5, 6], [7, 8, 9]))
    print(f"  {chained}")
    
    # combinations
    print("\nitertools.combinations:")
    items = ["A", "B", "C"]
    combos = list(itertools.combinations(items, 2))
    print(f"  Combinations of {items} taken 2 at a time:")
    for combo in combos:
        print(f"    {combo}")
    
    # permutations
    print("\nitertools.permutations:")
    perms = list(itertools.permutations(items, 2))
    print(f"  Permutations of {items} taken 2 at a time:")
    for perm in perms:
        print(f"    {perm}")
    
    # groupby
    print("\nitertools.groupby:")
    data = [("fruit", "apple"), ("fruit", "banana"), 
            ("vegetable", "carrot"), ("vegetable", "spinach")]
    data.sort(key=lambda x: x[0])
    for key, group in itertools.groupby(data, lambda x: x[0]):
        items = list(group)
        print(f"  {key}: {[item[1] for item in items]}")


def generator_expression_demo() -> None:
    """Demonstrate generator expressions."""
    print("\n" + "=" * 50)
    print("Generator Expressions")
    print("=" * 50)
    
    # Generator expression vs list comprehension
    numbers = range(1000000)
    
    # List comprehension (creates entire list in memory)
    list_comp = [x**2 for x in numbers if x % 2 == 0]
    print(f"\nList comprehension size: {len(list_comp)} items")
    print(f"Memory used: ~{len(list_comp) * 8 / 1024 / 1024:.2f} MB")
    
    # Generator expression (lazy evaluation)
    gen_expr = (x**2 for x in numbers if x % 2 == 0)
    print(f"\nGenerator expression created")
    print(f"First 10 values: {list(itertools.islice(gen_expr, 10))}")
    
    # Practical example: processing large files
    print("\nPractical example - sum of squares:")
    total = sum(x**2 for x in range(1000) if x % 2 == 0)
    print(f"  Sum of even squares (0-999): {total}")


def main() -> int:
    """Main function."""
    demonstrate_generators()
    demonstrate_iterators()
    demonstrate_itertools()
    generator_expression_demo()
    
    print("\n" + "=" * 50)
    print("Iterators and generators demo completed!")
    print("=" * 50)
    
    return 0


if __name__ == "__main__":
    sys.exit(main())