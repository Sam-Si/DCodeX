#!/usr/bin/env python3
"""DCodeX Python Client - Execute C++ code with resource monitoring and caching.

This module provides a gRPC client for executing C++ and Python code on a
DCodeX server with features like resource monitoring, smart caching, and
sandboxed execution.
"""

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
from typing import TYPE_CHECKING, Final

import grpc

# Add the current directory to sys.path to find generated modules
sys.path.append(os.path.join(os.path.dirname(__file__), "."))
sys.path.append(os.path.join(os.path.dirname(__file__), ".."))

import proto.sandbox_pb2 as sandbox_pb2
import proto.sandbox_pb2_grpc as sandbox_pb2_grpc

if TYPE_CHECKING:
    from collections.abc import Iterator
    from typing import Any


# ==============================================================================
# Type Aliases
# ==============================================================================

CodeExecutorStub = sandbox_pb2_grpc.CodeExecutorStub
"""Type alias for the gRPC CodeExecutor stub."""


# ==============================================================================
# ANSI Color Codes
# ==============================================================================

COLOR_RESET: Final[str] = "\033[0m"
COLOR_BOLD: Final[str] = "\033[1m"
COLOR_RED: Final[str] = "\033[91m"
COLOR_GREEN: Final[str] = "\033[92m"
COLOR_YELLOW: Final[str] = "\033[93m"
COLOR_BLUE: Final[str] = "\033[94m"
COLOR_MAGENTA: Final[str] = "\033[95m"
COLOR_CYAN: Final[str] = "\033[96m"
COLOR_WHITE: Final[str] = "\033[97m"


# ==============================================================================
# Constants
# ==============================================================================

_KILOBYTE: Final[int] = 1024
_MEGABYTE: Final[int] = 1024 * 1024
_GIGABYTE: Final[int] = 1024 * 1024 * 1024
_MILLISECOND: Final[int] = 1000
_MINUTE_MS: Final[int] = 60000


# ==============================================================================
# Data Classes
# ==============================================================================


@dataclass
class ExecutionResult:
    """Result of code execution.

    Attributes:
        stdout: Standard output from the executed code.
        stderr: Standard error from the executed code.
        peak_memory: Peak memory usage in bytes.
        execution_time: Execution time in milliseconds.
        cache_hit: Whether the result was served from cache.
        actual_time: Actual wall-clock time in milliseconds.
        cache_speedup: Speedup factor if cache hit, None otherwise.
        wall_clock_timeout: Whether execution timed out.
        output_truncated: Whether output was truncated.
    """

    stdout: str
    stderr: str
    peak_memory: int
    execution_time: float
    cache_hit: bool
    actual_time: float
    cache_speedup: float | None = None
    wall_clock_timeout: bool = False
    output_truncated: bool = False


# ==============================================================================
# Utility Functions
# ==============================================================================


def format_bytes(bytes_val: int) -> str:
    """Convert bytes to human-readable format.

    Args:
        bytes_val: Number of bytes.

    Returns:
        Human-readable string representation (B, KB, MB, or GB).
    """
    if bytes_val < _KILOBYTE:
        return f"{bytes_val} B"
    if bytes_val < _MEGABYTE:
        return f"{bytes_val / _KILOBYTE:.2f} KB"
    if bytes_val < _GIGABYTE:
        return f"{bytes_val / _MEGABYTE:.2f} MB"
    return f"{bytes_val / _GIGABYTE:.2f} GB"


def format_duration(ms: float) -> str:
    """Convert milliseconds to human-readable format.

    Args:
        ms: Duration in milliseconds.

    Returns:
        Human-readable string representation (ms, s, or m:s).
    """
    if ms < _MILLISECOND:
        return f"{ms:.2f} ms"
    if ms < _MINUTE_MS:
        return f"{ms / _MILLISECOND:.2f} s"
    minutes = int(ms / _MINUTE_MS)
    seconds = (ms % _MINUTE_MS) / _MILLISECOND
    return f"{minutes}m {seconds:.2f}s"


# ==============================================================================
# Core Execution Functions
# ==============================================================================


def execute_code(
    stub: CodeExecutorStub,
    code: str,
    language: str,
    description: str = "",
    stdin_data: str = "",
) -> ExecutionResult:
    """Execute code and return results with timing.

    Args:
        stub: gRPC stub for the CodeExecutor service.
        code: Code to execute.
        language: Execution language ("cpp" or "python").
        description: Optional description of the code.
        stdin_data: Data to feed to the program's standard input.
            Empty string means the program receives EOF immediately.

    Returns:
        ExecutionResult containing stdout, stderr, timing, and cache status.
    """
    request: Any = sandbox_pb2.CodeRequest(
        language=language,
        code=code,
        stdin_data=stdin_data,
    )

    start_time = time.time()
    responses: Iterator[Any] = stub.Execute(request)

    peak_memory = 0
    execution_time = 0.0
    cache_hit = False
    wall_clock_timeout = False
    output_truncated = False
    cached_execution_time = 0.0
    stdout_output: list[str] = []
    stderr_output: list[str] = []

    for response in responses:
        if response.stdout_chunk:
            stdout_output.append(response.stdout_chunk)
            print(response.stdout_chunk, end="", flush=True)
        if response.stderr_chunk:
            stderr_output.append(response.stderr_chunk)
            print(
                f"{COLOR_RED}STDERR: {response.stderr_chunk}{COLOR_RESET}",
                end="",
                flush=True,
            )
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

    actual_time = (time.time() - start_time) * _MILLISECOND
    cache_speedup: float | None = None
    if cache_hit and cached_execution_time > 0:
        cache_speedup = max(actual_time, 1) / max(cached_execution_time, 1)

    return ExecutionResult(
        stdout="".join(stdout_output),
        stderr="".join(stderr_output),
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
        show_output: Whether to show stdout/stderr output. Defaults to False
            as output is now streamed.
    """
    if show_output:
        if results.stdout:
            print(results.stdout, end="")
        if results.stderr:
            # Add newline before STDERR if stdout didn't end with one
            if results.stdout and not results.stdout.endswith("\n"):
                print()
            print(f"{COLOR_RED}STDERR: {results.stderr}{COLOR_RESET}", end="")

        if (
            not results.stdout
            and not results.stderr
            and results.execution_time > 0
        ):
            # Execution finished but produced no output
            pass

    print("-" * 50)
    print(f"{COLOR_CYAN}{COLOR_BOLD}📊 Resource Usage Summary:{COLOR_RESET}")
    print(
        f"   💾 {COLOR_BOLD}Peak Memory:{COLOR_RESET} "
        f"{COLOR_MAGENTA}{format_bytes(results.peak_memory)}{COLOR_RESET}"
    )
    print(
        f"   ⏱️  {COLOR_BOLD}Execution Time:{COLOR_RESET} "
        f"{COLOR_GREEN}{format_duration(results.execution_time)}{COLOR_RESET}"
    )
    print(
        f"   🌐 {COLOR_BOLD}Network Time:{COLOR_RESET} "
        f"{COLOR_BLUE}{format_duration(results.actual_time)}{COLOR_RESET}"
    )
    cache_status = (
        f"{COLOR_YELLOW}⚡ CACHE HIT{COLOR_RESET}"
        if results.cache_hit
        else f"{COLOR_WHITE}🆕 Fresh Execution{COLOR_RESET}"
    )
    print(f"   {cache_status}")
    if results.cache_speedup is not None:
        print(
            f"   {COLOR_CYAN}⚡ Cache Speedup: "
            f"{COLOR_BOLD}{results.cache_speedup:.2f}x faster{COLOR_RESET}"
        )
    if results.wall_clock_timeout:
        print(
            f"   {COLOR_RED}{COLOR_BOLD}⏰ WALL-CLOCK TIMEOUT "
            "(process killed by sandbox){COLOR_RESET}"
        )
    if results.output_truncated:
        print(
            f"   {COLOR_RED}✂️  OUTPUT TRUNCATED "
            "(exceeded 10 KB combined output limit){COLOR_RESET}"
        )
    print("=" * 50)


# ==============================================================================
# File Operations
# ==============================================================================


def read_code_from_file(file_path: Path) -> str:
    """Read source code from a file.

    Args:
        file_path: Path to the source file.

    Returns:
        Contents of the file as a string.

    Raises:
        FileNotFoundError: If the file does not exist.
        OSError: If the file cannot be read.
    """
    with open(file_path, "r", encoding="utf-8") as f:
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


def detect_language_from_directory(directory: Path) -> str:
    """Detect execution language from directory name.

    The function checks if the directory name contains 'python' to determine
    the language. Otherwise, it defaults to 'cpp'.

    Args:
        directory: Path to the directory containing code files.

    Returns:
        Language string for the server ("cpp" or "python").
    """
    dir_name = directory.name.lower()
    if "python" in dir_name:
        return "python"
    return "cpp"


def read_codes_from_directory(directory: Path) -> dict[str, tuple[str, str]]:
    """Read all code files from a directory.

    Language is automatically detected from the directory name.
    Files are returned with their detected language.

    Args:
        directory: Path to the directory containing code files.

    Returns:
        Dictionary mapping file names to tuples of (code, language).

    Raises:
        FileNotFoundError: If the directory does not exist.
    """
    codes: dict[str, tuple[str, str]] = {}
    if not directory.exists():
        raise FileNotFoundError(f"Directory not found: {directory}")

    language = detect_language_from_directory(directory)
    extension = ".py" if language == "python" else ".cpp"

    for file_path in directory.glob(f"*{extension}"):
        try:
            code = read_code_from_file(file_path)
            codes[file_path.stem] = (code, language)
        except OSError as e:
            print(f"Warning: Could not read {file_path}: {e}")

    return codes


# ==============================================================================
# Execution Helpers
# ==============================================================================


def execute_single_code(
    stub: CodeExecutorStub,
    file_path: Path,
    stdin_data: str = "",
) -> None:
    """Execute a single code example and print results.

    Language is automatically detected from the file extension.

    Args:
        stub: gRPC stub for the CodeExecutor service.
        file_path: Path to the source file to execute.
        stdin_data: Data to feed to the program's standard input.
    """
    code = read_code_from_file(file_path)
    language = detect_language(file_path)
    name = file_path.stem

    print(f"\n📝 {name}")
    print("=" * 50)
    results = execute_code(
        stub,
        code,
        language,
        name,
        stdin_data=stdin_data,
    )
    print_results(results)


def execute_codes_from_directory(
    stub: CodeExecutorStub,
    directory: Path,
    repeat_for_cache_demo: bool = False,
) -> None:
    """Execute all code files from a directory.

    Language is automatically detected from the directory name.

    Args:
        stub: gRPC stub for the CodeExecutor service.
        directory: Path to the directory containing code files.
        repeat_for_cache_demo: Whether to run each file twice to
            demonstrate caching.
    """
    codes = read_codes_from_directory(directory)
    language = detect_language_from_directory(directory)

    extension = ".py" if language == "python" else ".cpp"
    if not codes:
        print(f"{COLOR_YELLOW}No {extension} files found in {directory}{COLOR_RESET}")
        return

    print(
        f"\n{COLOR_CYAN}📁 Found {len(codes)} code file(s) in {directory}{COLOR_RESET}"
    )
    print("=" * 60)

    for name, (code, file_language) in codes.items():
        print(f"\n{COLOR_WHITE}{COLOR_BOLD}🔴 {name} - First Run{COLOR_RESET}")
        print("-" * 60)
        results1 = execute_code(stub, code, file_language, name)
        print_results(results1)

        if repeat_for_cache_demo:
            time.sleep(0.5)
            print(
                f"\n{COLOR_GREEN}{COLOR_BOLD}🟢 {name} - "
                f"Second Run (Cache Demo){COLOR_RESET}"
            )
            print("-" * 60)
            results2 = execute_code(
                stub,
                code,
                file_language,
                f"{name} - cached",
            )
            print_results(results2)

            if results2.cache_speedup is not None:
                print(
                    f"\n{COLOR_CYAN}⚡ Cache Speedup: "
                    f"{COLOR_BOLD}{results2.cache_speedup:.2f}x faster{COLOR_RESET}"
                )


# ==============================================================================
# Example Management
# ==============================================================================


def create_default_examples_directory(directory: Path) -> None:
    """Create default example code files in the specified directory.

    Args:
        directory: Path to create the examples directory.
    """
    directory.mkdir(parents=True, exist_ok=True)

    examples: dict[str, str] = {
        "hello_world": """#include <iostream>

int main() {
    std::cout << "Hello, World!" << std::endl;
    for (int i = 0; i < 5; i++) {
        std::cout << "Count: " << i << std::endl;
    }
    return 0;
}
""",
        "fibonacci": """#include <iostream>
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
""",
        "squares": """#include <iostream>
int main() {
    std::cout << "Squares of 1-10:" << std::endl;
    for (int i = 1; i <= 10; i++) {
        std::cout << i << "² = " << (i * i) << std::endl;
    }
    return 0;
}
""",
        "cubes": """#include <iostream>
int main() {
    std::cout << "Cubes of 1-10:" << std::endl;
    for (int i = 1; i <= 10; i++) {
        std::cout << i << "³ = " << (i * i * i) << std::endl;
    }
    return 0;
}
""",
        "computation": """#include <iostream>
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
""",
        "memory_test": """#include <iostream>
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
""",
        "sandbox_safe": """#include <iostream>
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
""",
    }

    for name, code in examples.items():
        file_path = directory / f"{name}.cpp"
        if not file_path.exists():
            with open(file_path, "w", encoding="utf-8") as f:
                f.write(code)
            print(f"Created: {file_path}")


# ==============================================================================
# Interactive Mode
# ==============================================================================


def run_interactive_mode(stub: CodeExecutorStub) -> None:
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
            execute_codes_from_directory(
                stub,
                examples_dir,
                repeat_for_cache_demo=True,
            )
        elif choice == "3":
            file_path_str = input("Enter file path: ").strip()
            try:
                file_path = Path(file_path_str)
                execute_single_code(stub, file_path)
            except (FileNotFoundError, OSError) as e:
                print(f"Error: {e}")
        elif choice == "4":
            print("Exiting...")
            break
        else:
            print("Invalid choice. Please try again.")


def run_all_examples(stub: CodeExecutorStub) -> None:
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

    execute_codes_from_directory(
        stub,
        examples_dir,
        repeat_for_cache_demo=True,
    )

    print("\n" + "=" * 60)
    print("✨ All examples completed!")
    print("Features demonstrated:")
    print("  📊 Peak memory tracking")
    print("  ⏱️  Execution time measurement")
    print("  💾 Result caching with FNV-1a hashing")
    print("  ⚡ Cache hit/miss detection")
    print("  🛡️  Sandboxed execution")
    print("=" * 60)


# ==============================================================================
# Main Entry Point
# ==============================================================================


def main() -> None:
    """Main entry point for the client."""
    import argparse

    parser = argparse.ArgumentParser(
        description="DCodeX Python Client - Execute code with resource monitoring",
    )
    parser.add_argument(
        "--server",
        default="localhost:50051",
        help="Server address (default: localhost:50051)",
    )
    parser.add_argument(
        "--interactive",
        "-i",
        action="store_true",
        help="Run in interactive mode",
    )
    parser.add_argument(
        "--directory",
        "-d",
        type=Path,
        help="Directory containing code files to execute",
    )
    parser.add_argument(
        "--file",
        "-f",
        type=Path,
        help="Specific code file to execute",
    )
    parser.add_argument(
        "--cache-demo",
        "-c",
        action="store_true",
        help="Run each file twice to demonstrate caching",
    )
    parser.add_argument(
        "--stdin",
        default="",
        help=(
            "Data to pass to the program's standard input. "
            "Use \\n for newlines, e.g. --stdin 'Alice\\n3\\n10\\n20\\n30\\n'. "
            "Only used with --file."
        ),
    )
    parser.add_argument(
        "--stdin-file",
        type=Path,
        default=None,
        help=(
            "Path to a file whose contents are passed to the program's stdin. "
            "Only used with --file. Takes precedence over --stdin."
        ),
    )

    args = parser.parse_args()

    # Resolve stdin_data: --stdin-file takes precedence over --stdin.
    stdin_data: str = ""
    if args.stdin_file:
        try:
            stdin_data = args.stdin_file.read_text(encoding="utf-8")
        except (FileNotFoundError, OSError) as e:
            print(f"Error reading stdin file: {e}")
            sys.exit(1)
    elif args.stdin:
        # Allow the user to write \n literally on the command line and have it
        # interpreted as a real newline character.
        stdin_data = args.stdin.replace("\\n", "\n")

    # Connect to server
    channel = grpc.insecure_channel(args.server)
    stub: CodeExecutorStub = sandbox_pb2_grpc.CodeExecutorStub(channel)

    if args.file:
        # Execute specific file - language auto-detected from file extension
        try:
            execute_single_code(
                stub,
                args.file,
                stdin_data=stdin_data,
            )
        except (FileNotFoundError, OSError) as e:
            print(f"Error: {e}")
            sys.exit(1)
    elif args.directory:
        # Execute all files from directory - language auto-detected from dir name
        execute_codes_from_directory(
            stub,
            args.directory,
            args.cache_demo,
        )
    elif args.interactive:
        # Run interactive mode
        run_interactive_mode(stub)
    else:
        # Run all default examples
        run_all_examples(stub)


if __name__ == "__main__":
    main()
