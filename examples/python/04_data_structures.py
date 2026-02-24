#!/usr/bin/env python3
"""Standard Python Data Structures
Uses only standard library modules.
"""

import sys
from collections import deque, defaultdict, Counter
from typing import List, Dict, Set, Tuple


def demonstrate_lists() -> None:
    """Demonstrate list operations."""
    print("=" * 50)
    print("List Operations")
    print("=" * 50)
    
    # Creating lists
    numbers: List[int] = [3, 1, 4, 1, 5, 9, 2, 6]
    print(f"\nOriginal list: {numbers}")
    
    # Basic operations
    print(f"Length: {len(numbers)}")
    print(f"First element: {numbers[0]}")
    print(f"Last element: {numbers[-1]}")
    print(f"Slice [2:5]: {numbers[2:5]}")
    
    # Modifying lists
    numbers.append(7)
    print(f"After append(7): {numbers}")
    
    numbers.insert(0, 0)
    print(f"After insert(0, 0): {numbers}")
    
    numbers.remove(1)
    print(f"After remove(1): {numbers}")
    
    popped = numbers.pop()
    print(f"Popped element: {popped}")
    print(f"After pop(): {numbers}")
    
    # Sorting
    numbers.sort()
    print(f"Sorted: {numbers}")
    
    numbers.reverse()
    print(f"Reversed: {numbers}")
    
    # List comprehension
    squares = [x**2 for x in range(10)]
    print(f"\nSquares 0-9: {squares}")
    
    evens = [x for x in numbers if x % 2 == 0]
    print(f"Even numbers: {evens}")


def demonstrate_dictionaries() -> None:
    """Demonstrate dictionary operations."""
    print("\n" + "=" * 50)
    print("Dictionary Operations")
    print("=" * 50)
    
    # Creating dictionaries
    person: Dict[str, object] = {
        "name": "Alice",
        "age": 30,
        "city": "New York",
        "hobbies": ["reading", "coding", "gaming"]
    }
    
    print(f"\nPerson dictionary:")
    for key, value in person.items():
        print(f"  {key}: {value}")
    
    # Accessing values
    print(f"\nName: {person['name']}")
    print(f"Age: {person.get('age')}")
    print(f"Country: {person.get('country', 'Unknown')}")
    
    # Modifying
    person["age"] = 31
    person["country"] = "USA"
    print(f"\nAfter modifications:")
    print(f"  Age: {person['age']}")
    print(f"  Country: {person['country']}")
    
    # Dictionary methods
    print(f"\nKeys: {list(person.keys())}")
    print(f"Values: {list(person.values())}")
    print(f"Items count: {len(person)}")
    
    # Dictionary comprehension
    squares_dict = {x: x**2 for x in range(6)}
    print(f"\nSquares dict: {squares_dict}")


def demonstrate_sets() -> None:
    """Demonstrate set operations."""
    print("\n" + "=" * 50)
    print("Set Operations")
    print("=" * 50)
    
    # Creating sets
    set_a: Set[int] = {1, 2, 3, 4, 5}
    set_b: Set[int] = {4, 5, 6, 7, 8}
    
    print(f"\nSet A: {set_a}")
    print(f"Set B: {set_b}")
    
    # Set operations
    print(f"Union (A | B): {set_a | set_b}")
    print(f"Intersection (A & B): {set_a & set_b}")
    print(f"Difference (A - B): {set_a - set_b}")
    print(f"Symmetric Difference (A ^ B): {set_a ^ set_b}")
    
    # Modifying sets
    set_a.add(10)
    print(f"\nAfter add(10) to A: {set_a}")
    
    set_a.remove(2)
    print(f"After remove(2) from A: {set_a}")
    
    # Membership testing
    print(f"\n3 in A: {3 in set_a}")
    print(f"10 in B: {10 in set_b}")


def demonstrate_tuples() -> None:
    """Demonstrate tuple operations."""
    print("\n" + "=" * 50)
    print("Tuple Operations")
    print("=" * 50)
    
    # Creating tuples
    point: Tuple[int, int] = (3, 4)
    person_tuple: Tuple[str, int, str] = ("Bob", 25, "Engineer")
    
    print(f"\nPoint: {point}")
    print(f"Person tuple: {person_tuple}")
    
    # Accessing elements
    print(f"X coordinate: {point[0]}")
    print(f"Y coordinate: {point[1]}")
    print(f"Name: {person_tuple[0]}")
    
    # Tuple unpacking
    name, age, job = person_tuple
    print(f"\nUnpacked: name={name}, age={age}, job={job}")
    
    # Tuple methods
    numbers = (1, 2, 3, 2, 4, 2, 5)
    print(f"\nTuple: {numbers}")
    print(f"Count of 2: {numbers.count(2)}")
    print(f"Index of 4: {numbers.index(4)}")


def demonstrate_collections() -> None:
    """Demonstrate collections module."""
    print("\n" + "=" * 50)
    print("Collections Module")
    print("=" * 50)
    
    # Counter
    words = ["apple", "banana", "apple", "cherry", "banana", "apple"]
    word_count = Counter(words)
    print(f"\nWord frequencies: {dict(word_count)}")
    print(f"Most common: {word_count.most_common(2)}")
    
    # defaultdict
    word_dict: Dict[str, List[int]] = defaultdict(list)
    for i, word in enumerate(words):
        word_dict[word].append(i)
    print(f"\nWord positions: {dict(word_dict)}")
    
    # deque
    queue: deque[int] = deque()
    queue.append(1)
    queue.append(2)
    queue.append(3)
    print(f"\nQueue: {list(queue)}")
    print(f"Popped from left: {queue.popleft()}")
    print(f"Queue after pop: {list(queue)}")


def main() -> int:
    """Main function."""
    demonstrate_lists()
    demonstrate_dictionaries()
    demonstrate_sets()
    demonstrate_tuples()
    demonstrate_collections()
    
    print("\n" + "=" * 50)
    print("Data structures demo completed!")
    print("=" * 50)
    
    return 0


if __name__ == "__main__":
    sys.exit(main())