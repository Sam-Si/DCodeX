#!/usr/bin/env python3
"""DCodeX Python Client - Execute C++ code with resource monitoring and caching."""

# Copyright 2024 DCodeX Team
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from __future__ import annotations

import os
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING, Any, Dict, Iterator, List

import grpc

# ANSI Color Codes
COLOR_RESET = "\033[0m"
COLOR_BOLD = "\033[1m"
COLOR_RED = "\033[91m"
COLOR_GREEN = "\033[92m"
COLOR_YELLOW = "\033[93m"
COLOR_BLUE = "\033[94m"
COLOR_MAGENTA = "\033[95m"
COLOR_CYAN = "\033[96m"
COLOR_WHITE = "\033[97m"

# Add the current directory to sys.path to find generated modules
sys.path.append(os.path.join(os.path.dirname(__file__), "."))
sys.path.append(os.path.join(os.path.dirname(__file__), ".."))

if TYPE_CHECKING:
    pass

import proto.sandbox_pb2 as sandbox_pb2
import proto.sandbox_pb2_grpc as sandbox_pb2_grpc


@dataclass
class ExecutionResult:
    """Result of code execution."""
    stdout: str
    stderr: str
    peak_memory: int
    execution_time: float
    cache_hit: bool
    actual_time: float
    cache_speedup: float | None = None
    wall_clock_timeout: bool = False
    output_truncated: bool = False


def format_bytes(bytes_val: int) -> str:
    """Convert bytes to human-readable format.
    
    Args:
        bytes_val: Number of bytes.
        
    Returns:
        Human-readable string representation (B, KB, MB, or GB).
    """
    if bytes_val < 1024:
        return f"{bytes_val} B"
    elif bytes_val < 1024 * 1024:
        return f"{bytes_val / 1024:.2f} KB"
    elif bytes_val < 1024 * 1024 * 1024:
        return f"{bytes_val / (1024 * 1024):.2f} MB"
    else:
        return f"{bytes_val / (1024 * 1024 * 1024):.2f} GB"


def format_duration(ms: float) -> str:
    """Convert milliseconds to human-readable format.
    
    Args:
        ms: Duration in milliseconds.
        
    Returns:
        Human-readable string representation (ms, s, or m:s).
    """
    if ms < 1000:
        return f"{ms:.2f} ms"
    elif ms < 60000:
        return f"{ms / 1000:.2f} s"
    else:
        minutes = int(ms / 60000)
        seconds = (ms % 60000) / 1000
        return f"{minutes}m {seconds:.2f}s"


def execute_code(
    stub: Any,
    code: str,
        description: str = "",
        stdin_data: str = "",
        language: str = "cpp",
) -> ExecutionResult:
    """Execute code and return results with timing.
    
    Args:
        stub: gRPC stub for the CodeExecutor service.
        code: Code to execute.
        description: Optional description of the code.
        stdin_data: Data to feed to the program's standard input.
                    Empty string means the program receives EOF immediately.
        language: Execution language ("cpp" or "python").
        
    Returns:
        ExecutionResult containing stdout, stderr, timing, and cache status.
    """
    request: Any = sandbox_pb2.CodeRequest(
        language=language, code=code, stdin_data=stdin_data
    )
    
    start_time = time.time()
    responses: Iterator[Any] = stub.Execute(request)
    
    peak_memory = 0
    execution_time = 0.0
    cache_hit = False
    wall_clock_timeout = False
    output_truncated = False
    cached_execution_time = 0.0
    stdout_output: List[str] = []
    stderr_output: List[str] = []
    
    for response in responses:
        if response.stdout_chunk:
            stdout_output.append(response.stdout_chunk)
            print(response.stdout_chunk, end='', flush=True)
        if response.stderr_chunk:
            stderr_output.append(response.stderr_chunk)
            print(f"{COLOR_RED}STDERR: {response.stderr_chunk}{COLOR_RESET}", end='', flush=True)
        if response.peak_memory_bytes > 0:
            peak_memory = response.peak_memory_bytes
        if response.execution_time_ms > 0:
            execution_time = response.execution_time_ms
        cache_hit = response.cache_hit
        if response.cache_hit and response.execution_time_ms > 0:
            cached_execution_time = response.execution_time_ms
        if response.wall_clock_timeout:
            wall_clock_timeout = True
        if response.output_truncated:
            output_truncated = True
    
    actual_time = (time.time() - start_time) * 1000  # Convert to ms
    cache_speedup = None
    if cache_hit and cached_execution_time > 0:
        cache_speedup = max(actual_time, 1) / max(cached_execution_time, 1)
    
    return ExecutionResult(
        stdout=''.join(stdout_output),
        stderr=''.join(stderr_output),
        peak_memory=peak_memory,
        execution_time=execution_time,
        cache_hit=cache_hit,
        actual_time=actual_time,
        cache_speedup=cache_speedup,
        wall_clock_timeout=wall_clock_timeout,
        output_truncated=output_truncated,
    )


def print_results(results: ExecutionResult, show_output: bool = False) -> None:
    """Print execution results in a formatted way.
    
    Args:
        results: ExecutionResult to print.
        show_output: Whether to show stdout/stderr output (defaults to False as output is now streamed).
    """
    if show_output:
        if results.stdout:
            print(results.stdout, end='')
        if results.stderr:
            # If stdout existed and didn't end with a newline, add one before STDERR
            if results.stdout and not results.stdout.endswith('\n'):
                print()
            print(f"{COLOR_RED}STDERR: {results.stderr}{COLOR_RESET}", end='')

        if not results.stdout and not results.stderr and results.execution_time > 0:
            # Execution finished but produced no output - might be a silent crash or infrastructure error
            pass
    
    print("-" * 50)
    print(f"{COLOR_CYAN}{COLOR_BOLD}📊 Resource Usage Summary:{COLOR_RESET}")
    print(f"   💾 {COLOR_BOLD}Peak Memory:{COLOR_RESET} {COLOR_MAGENTA}{format_bytes(results.peak_memory)}{COLOR_RESET}")
    print(f"   ⏱️  {COLOR_BOLD}Execution Time:{COLOR_RESET} {COLOR_GREEN}{format_duration(results.execution_time)}{COLOR_RESET}")
    print(f"   🌐 {COLOR_BOLD}Network Time:{COLOR_RESET} {COLOR_BLUE}{format_duration(results.actual_time)}{COLOR_BLUE}{COLOR_RESET}")
    cache_status = f"{COLOR_YELLOW}⚡ CACHE HIT{COLOR_RESET}" if results.cache_hit else f"{COLOR_WHITE}🆕 Fresh Execution{COLOR_RESET}"
    print(f"   {cache_status}")
    if results.cache_speedup is not None:
        print(
            f"   {COLOR_CYAN}⚡ Cache Speedup: {COLOR_BOLD}{results.cache_speedup:.2f}x faster{COLOR_RESET}"
        )
    if results.wall_clock_timeout:
        print(f"   {COLOR_RED}{COLOR_BOLD}⏰ WALL-CLOCK TIMEOUT (process killed by sandbox){COLOR_RESET}")
    if results.output_truncated:
        print(f"   {COLOR_RED}✂️  OUTPUT TRUNCATED (exceeded 10 KB combined output limit){COLOR_RESET}")
    print("=" * 50)


def read_code_from_file(file_path: Path) -> str:
    """Read source code from a file.
    
    Args:
        file_path: Path to the source file.
        
    Returns:
        Contents of the file as a string.
        
    Raises:
        FileNotFoundError: If the file does not exist.
        IOError: If the file cannot be read.
    """
    with open(file_path, 'r', encoding='utf-8') as f:
        return f.read()


def detect_language(file_path: Path) -> str:
    """Detect execution language from file extension.
    
    Args:
        file_path: Path to the source file.
        
    Returns:
        Language string for the server ("cpp" or "python").
    """
    suffix = file_path.suffix.lower()
    if suffix == ".py":
        return "python"
    return "cpp"


def read_codes_from_directory(directory: Path, language: str) -> Dict[str, str]:
    """Read all code files from a directory.
    
    Args:
        directory: Path to the directory containing code files.
        language: Execution language ("cpp" or "python").
        
    Returns:
        Dictionary mapping file names to their contents.
        
    Raises:
        FileNotFoundError: If the directory does not exist.
    """
    codes: Dict[str, str] = {}
    if not directory.exists():
        raise FileNotFoundError(f"Directory not found: {directory}")

    extension = ".py" if language == "python" else ".cpp"
    for file_path in directory.glob(f"*{extension}"):
        try:
            codes[file_path.stem] = read_code_from_file(file_path)
        except IOError as e:
            print(f"Warning: Could not read {file_path}: {e}")

    return codes


def execute_single_code(
    stub: Any,
    name: str,
        code: str,
        stdin_data: str = "",
        language: str = "cpp",
) -> None:
    """Execute a single code example and print results.
    
    Args:
        stub: gRPC stub for the CodeExecutor service.
        name: Name/description of the code example.
        code: Code to execute.
        stdin_data: Data to feed to the program's standard input.
        language: Execution language ("cpp" or "python").
    """
    print(f"\n📝 {name}")
    print("=" * 50)
    results = execute_code(
        stub, code, name, stdin_data=stdin_data, language=language
    )
    print_results(results)


def execute_codes_from_directory(
    stub: Any,
    directory: Path,
    repeat_for_cache_demo: bool = False,
    language: str = "cpp",
) -> None:
    """Execute all code files from a directory.
    
    Args:
        stub: gRPC stub for the CodeExecutor service.
        directory: Path to the directory containing code files.
        repeat_for_cache_demo: Whether to run each file twice to demonstrate caching.
        language: Execution language ("cpp" or "python").
    """
    codes = read_codes_from_directory(directory, language)

    extension = ".py" if language == "python" else ".cpp"
    if not codes:
        print(f"{COLOR_YELLOW}No {extension} files found in {directory}{COLOR_RESET}")
        return

    print(f"\n{COLOR_CYAN}📁 Found {len(codes)} code file(s) in {directory}{COLOR_RESET}")
    print("=" * 60)

    for name, code in codes.items():
        print(f"\n{COLOR_WHITE}{COLOR_BOLD}🔴 {name} - First Run{COLOR_RESET}")
        print("-" * 60)
        results1 = execute_code(stub, code, name, language=language)
        print_results(results1)

        if repeat_for_cache_demo:
            time.sleep(0.5)
            print(f"\n{COLOR_GREEN}{COLOR_BOLD}🟢 {name} - Second Run (Cache Demo){COLOR_RESET}")
            print("-" * 60)
            results2 = execute_code(
                stub, code, name + " - cached", language=language
            )
            print_results(results2)

            if results2.cache_speedup is not None:
                print(
                    f"\n{COLOR_CYAN}⚡ Cache Speedup: {COLOR_BOLD}{results2.cache_speedup:.2f}x faster{COLOR_RESET}"
                )


def create_default_examples_directory(directory: Path) -> None:
    """Create default example code files in the specified directory.
    
    Args:
        directory: Path to create the examples directory.
    """
    directory.mkdir(parents=True, exist_ok=True)
    
    examples: Dict[str, str] = {
        "hello_world": '''#include <iostream>

int main() {
    std::cout << "Hello, World!" << std::endl;
    for (int i = 0; i < 5; i++) {
        std::cout << "Count: " << i << std::endl;
    }
    return 0;
}
''',
        "fibonacci": '''#include <iostream>
#include <cmath>

int main() {
    std::cout << "Computing fibonacci sequence..." << std::endl;
    long long a = 0, b = 1;
    std::cout << "Fibonacci: " << a;
    for (int i = 0; i < 40; i++) {
        std::cout << ", " << b;
        long long temp = a + b;
        a = b;
        b = temp;
    }
    std::cout << std::endl;
    return 0;
}
''',
        "squares": '''#include <iostream>
int main() {
    std::cout << "Squares of 1-10:" << std::endl;
    for (int i = 1; i <= 10; i++) {
        std::cout << i << "² = " << (i * i) << std::endl;
    }
    return 0;
}
''',
        "cubes": '''#include <iostream>
int main() {
    std::cout << "Cubes of 1-10:" << std::endl;
    for (int i = 1; i <= 10; i++) {
        std::cout << i << "³ = " << (i * i * i) << std::endl;
    }
    return 0;
}
''',
        "computation": '''#include <iostream>
#include <cmath>

int main() {
    std::cout << "Starting heavy computation..." << std::endl;
    double result = 0.0;
    for (int i = 1; i <= 5000000; i++) {
        result += std::sqrt(static_cast<double>(i));
    }
    std::cout << "Sum of square roots: " << result << std::endl;
    return 0;
}
''',
        "memory_test": '''#include <iostream>
#include <vector>

int main() {
    std::cout << "Testing memory allocation..." << std::endl;
    std::vector<int> data;
    const int count = 2000000;
    for (int i = 0; i < count; i++) {
        data.push_back(i);
    }
    std::cout << "Allocated " << (data.size() * sizeof(int) / (1024*1024)) << " MB" << std::endl;
    std::cout << "Memory test complete!" << std::endl;
    return 0;
}
''',
        "sandbox_safe": '''#include <iostream>
#include <vector>

int main() {
    std::cout << "Running within sandbox limits..." << std::endl;
    std::vector<int> data(10000);
    for (size_t i = 0; i < data.size(); i++) {
        data[i] = i * i;
    }
    std::cout << "Processed " << data.size() << " elements safely." << std::endl;
    return 0;
}
'''
    }
    
    for name, code in examples.items():
        file_path = directory / f"{name}.cpp"
        if not file_path.exists():
            with open(file_path, 'w', encoding='utf-8') as f:
                f.write(code)
            print(f"Created: {file_path}")


def run_interactive_mode(stub: Any) -> None:
    """Run interactive mode for executing code from files.
    
    Args:
        stub: gRPC stub for the CodeExecutor service.
    """
    examples_dir = Path("examples")
    create_default_examples_directory(examples_dir)
    
    while True:
        print("\n" + "=" * 60)
        print("DCodeX Python Client - Interactive Mode")
        print("=" * 60)
        print("1. Execute all examples from 'examples/' directory")
        print("2. Execute examples with cache demonstration")
        print("3. Execute a specific file")
        print("4. Exit")
        print("-" * 60)
        
        choice = input("Enter choice (1-4): ").strip()
        
        if choice == "1":
            execute_codes_from_directory(stub, examples_dir)
        elif choice == "2":
            execute_codes_from_directory(stub, examples_dir, repeat_for_cache_demo=True)
        elif choice == "3":
            file_path = input("Enter file path: ").strip()
            try:
                code = read_code_from_file(Path(file_path))
                language = detect_language(Path(file_path))
                execute_single_code(
                    stub, Path(file_path).stem, code, language=language
                )
            except (FileNotFoundError, IOError) as e:
                print(f"Error: {e}")
        elif choice == "4":
            print("Exiting...")
            break
        else:
            print("Invalid choice. Please try again.")


def run_all_examples(stub: Any) -> None:
    """Run all default examples.
    
    Args:
        stub: gRPC stub for the CodeExecutor service.
    """
    examples_dir = Path("examples")
    create_default_examples_directory(examples_dir)
    
    print("\n" + "🚀" * 30)
    print("  DCodeX Feature Showcase")
    print("  Features: Resource Monitoring | Smart Caching | Sandboxing")
    print("🚀" * 30)
    
    execute_codes_from_directory(stub, examples_dir, repeat_for_cache_demo=True)
    
    print("\n" + "=" * 60)
    print("✨ All examples completed!")
    print("Features demonstrated:")
    print("  📊 Peak memory tracking")
    print("  ⏱️  Execution time measurement")
    print("  💾 Result caching with FNV-1a hashing")
    print("  ⚡ Cache hit/miss detection")
    print("  🛡️  Sandboxed execution")
    print("=" * 60)


def main() -> None:
    """Main entry point for the client."""
    import argparse
    
    parser = argparse.ArgumentParser(
        description="DCodeX Python Client - Execute code with resource monitoring"
    )
    parser.add_argument(
        "--server",
        default="localhost:50051",
        help="Server address (default: localhost:50051)"
    )
    parser.add_argument(
        "--interactive", "-i",
        action="store_true",
        help="Run in interactive mode"
    )
    parser.add_argument(
        "--directory", "-d",
        type=Path,
        help="Directory containing code files to execute"
    )
    parser.add_argument(
        "--file", "-f",
        type=Path,
        help="Specific code file to execute"
    )
    parser.add_argument(
        "--language", "-l",
        choices=("cpp", "python"),
        default=None,
        help="Override language detection for --file/--directory"
    )
    parser.add_argument(
        "--cache-demo", "-c",
        action="store_true",
        help="Run each file twice to demonstrate caching"
    )
    parser.add_argument(
        "--stdin",
        default="",
        help=(
            "Data to pass to the program's standard input. "
            "Use \\n for newlines, e.g. --stdin 'Alice\\n3\\n10\\n20\\n30\\n'. "
            "Only used with --file."
        )
    )
    parser.add_argument(
        "--stdin-file",
        type=Path,
        default=None,
        help=(
            "Path to a file whose contents are passed to the program's stdin. "
            "Only used with --file. Takes precedence over --stdin."
        )
    )
    
    args = parser.parse_args()

    # Resolve stdin_data: --stdin-file takes precedence over --stdin.
    stdin_data: str = ""
    if args.stdin_file:
        try:
            stdin_data = args.stdin_file.read_text(encoding="utf-8")
        except (FileNotFoundError, IOError) as e:
            print(f"Error reading stdin file: {e}")
            sys.exit(1)
    elif args.stdin:
        # Allow the user to write \n literally on the command line and have it
        # interpreted as a real newline character.
        stdin_data = args.stdin.replace("\\n", "\n")
    
    # Connect to server
    channel = grpc.insecure_channel(args.server)
    stub: Any = sandbox_pb2_grpc.CodeExecutorStub(channel)
    
    if args.file:
        # Execute specific file
        try:
            code = read_code_from_file(args.file)
            language = args.language or detect_language(args.file)
            execute_single_code(
                stub,
                args.file.stem,
                code,
                stdin_data=stdin_data,
                language=language,
            )
        except (FileNotFoundError, IOError) as e:
            print(f"Error: {e}")
            sys.exit(1)
    elif args.directory:
        # Execute all files from directory
        language = args.language or "cpp"
        execute_codes_from_directory(
            stub, args.directory, args.cache_demo, language=language
        )
    elif args.interactive:
        # Run interactive mode
        run_interactive_mode(stub)
    else:
        # Run all default examples
        run_all_examples(stub)


if __name__ == '__main__':
    main()
