#!/usr/bin/env python3
"""Standard Python File Operations
Uses only standard library modules.
"""

import json
import os
import sys
import tempfile
from pathlib import Path
from typing import Dict


def create_sample_files(directory: Path) -> None:
    """Create sample files for demonstration."""
    directory.mkdir(parents=True, exist_ok=True)
    
    # Create a text file
    text_file = directory / "sample.txt"
    with open(text_file, 'w') as f:
        f.write("Hello, World!\n")
        f.write("This is a sample text file.\n")
        f.write("Line 3: Python file operations demo.\n")
    
    # Create a JSON file
    json_file = directory / "data.json"
    data: Dict[str, object] = {
        "name": "DCodeX",
        "version": "1.0",
        "features": ["sandboxing", "caching", "monitoring"],
        "active": True
    }
    with open(json_file, 'w') as f:
        json.dump(data, f, indent=2)
    
    print(f"Created sample files in: {directory}")


def demonstrate_file_operations(directory: Path) -> None:
    """Demonstrate various file operations."""
    print("\n" + "=" * 50)
    print("File Operations Demo")
    print("=" * 50)
    
    # List files
    print(f"\nFiles in directory:")
    for file_path in directory.iterdir():
        if file_path.is_file():
            size = file_path.stat().st_size
            print(f"  {file_path.name} ({size} bytes)")
    
    # Read text file
    text_file = directory / "sample.txt"
    print(f"\nReading {text_file.name}:")
    with open(text_file, 'r') as f:
        content = f.read()
        print(content)
    
    # Read line by line
    print(f"Reading {text_file.name} line by line:")
    with open(text_file, 'r') as f:
        for i, line in enumerate(f, 1):
            print(f"  Line {i}: {line.strip()}")
    
    # Read JSON file
    json_file = directory / "data.json"
    print(f"\nReading {json_file.name}:")
    with open(json_file, 'r') as f:
        data = json.load(f)
        print(f"  Name: {data['name']}")
        print(f"  Version: {data['version']}")
        print(f"  Features: {', '.join(data['features'])}")
        print(f"  Active: {data['active']}")


def demonstrate_path_operations() -> None:
    """Demonstrate path operations."""
    print("\n" + "=" * 50)
    print("Path Operations Demo")
    print("=" * 50)
    
    # Current working directory
    cwd = Path.cwd()
    print(f"\nCurrent working directory: {cwd}")
    
    # Home directory
    home = Path.home()
    print(f"Home directory: {home}")
    
    # Path manipulation
    sample_path = Path("/home/user/documents/file.txt")
    print(f"\nSample path: {sample_path}")
    print(f"  Name: {sample_path.name}")
    print(f"  Stem: {sample_path.stem}")
    print(f"  Suffix: {sample_path.suffix}")
    print(f"  Parent: {sample_path.parent}")
    print(f"  Absolute: {sample_path.is_absolute()}")


def demonstrate_temp_files() -> None:
    """Demonstrate temporary file operations."""
    print("\n" + "=" * 50)
    print("Temporary Files Demo")
    print("=" * 50)
    
    # Create temporary file
    with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
        f.write("This is a temporary file.\n")
        f.write(f"Process ID: {os.getpid()}\n")
        temp_path = f.name
    
    print(f"\nCreated temporary file: {temp_path}")
    
    # Read it back
    with open(temp_path, 'r') as f:
        content = f.read()
        print(f"Content:\n{content}")
    
    # Clean up
    os.unlink(temp_path)
    print(f"Deleted temporary file: {temp_path}")


def main() -> int:
    """Main function."""
    # Create temporary directory for demo
    with tempfile.TemporaryDirectory() as tmpdir:
        demo_dir = Path(tmpdir)
        
        create_sample_files(demo_dir)
        demonstrate_file_operations(demo_dir)
        demonstrate_path_operations()
        demonstrate_temp_files()
    
    print("\n" + "=" * 50)
    print("File operations demo completed!")
    print("=" * 50)
    
    return 0


if __name__ == "__main__":
    sys.exit(main())