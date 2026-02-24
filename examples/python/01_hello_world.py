#!/usr/bin/env python3
"""Standard Python Hello World Example
Uses only standard library modules.
"""

import datetime
import platform
import sys


def main() -> None:
    """Main function demonstrating basic Python features."""
    print("=" * 50)
    print("Hello, World!")
    print("Welcome to DCodeX Python Examples!")
    print("=" * 50)
    
    # System information
    print(f"\nSystem Information:")
    print(f"  Python Version: {platform.python_version()}")
    print(f"  Platform: {platform.platform()}")
    print(f"  Current Time: {datetime.datetime.now()}")
    
    # Basic loop
    print("\nCounting to 5:")
    for i in range(1, 6):
        print(f"  Count: {i}")
    
    # Command line arguments
    print(f"\nCommand line arguments: {sys.argv}")
    print(f"Number of arguments: {len(sys.argv)}")
    
    print("\nProgram completed successfully!")
    
    return 0


if __name__ == "__main__":
    sys.exit(main())