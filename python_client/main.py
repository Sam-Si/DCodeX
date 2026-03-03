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

"""DCodeX Python Client entry point."""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

import grpc

# Ensure the project root is in the path for proto imports
_PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

import proto.sandbox_pb2 as sandbox_pb2
import proto.sandbox_pb2_grpc as sandbox_pb2_grpc

from python_client.execution_types import CodeExecutorStub
from python_client.executor import Executor
from python_client.file_manager import FileManager
from python_client.formatter import COLOR_CYAN, COLOR_GREEN, COLOR_RESET, COLOR_YELLOW
from python_client.grpc_client import GrpcClient
from python_client.interactive_mode import InteractiveMode, ExampleRunner
from python_client.language_detector import LanguageDetector


def create_stub(server_address: str) -> CodeExecutorStub:
    """Create a gRPC stub for the CodeExecutor service.

    Args:
        server_address: Server address in host:port format.

    Returns:
        CodeExecutorStub for making gRPC calls.
    """
    channel = grpc.insecure_channel(server_address)
    return sandbox_pb2_grpc.CodeExecutorStub(channel)


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments.

    Returns:
        Parsed arguments namespace.
    """
    parser = argparse.ArgumentParser(
        description="DCodeX Python Client - Execute code in a sandboxed environment"
    )
    parser.add_argument(
        "--server",
        default="localhost:50051",
        help="Server address (default: localhost:50051)",
    )
    parser.add_argument(
        "-d",
        "--directory",
        type=Path,
        default=None,
        help="Directory containing code files to execute",
    )
    parser.add_argument(
        "-f",
        "--file",
        type=Path,
        default=None,
        help="Single file to execute",
    )
    parser.add_argument(
        "-c",
        "--cache-demo",
        action="store_true",
        help="Run each file twice to demonstrate caching",
    )
    parser.add_argument(
        "-i",
        "--interactive",
        action="store_true",
        help="Run in interactive mode",
    )
    parser.add_argument(
        "--stdin",
        default="",
        help="Stdin data to pass to the program (use \\n for newlines)",
    )
    parser.add_argument(
        "--stdin-file",
        type=Path,
        default=None,
        help="File to read stdin data from",
    )
    return parser.parse_args()


def main() -> None:
    """Main entry point for the DCodeX Python Client."""
    args = parse_args()

    # Create gRPC client
    grpc_client = GrpcClient()

    # Handle stdin input
    stdin_data = args.stdin
    if args.stdin_file:
        stdin_data = args.stdin_file.read_text(encoding="utf-8")

    # Create executor
    executor = Executor(grpc_client)

    # Create stub for connection
    stub = create_stub(args.server)

    if args.interactive:
        interactive = InteractiveMode(grpc_client)
        interactive.run(stub)
        return

    if args.file:
        if not args.file.exists():
            print(f"{COLOR_YELLOW}File not found: {args.file}{COLOR_RESET}")
            sys.exit(1)
        executor.execute_single_code(stub, args.file, stdin_data=stdin_data)
        return

    if args.directory:
        if not args.directory.exists():
            print(
                f"{COLOR_YELLOW}Directory not found: {args.directory}{COLOR_RESET}"
            )
            sys.exit(1)
        executor.execute_codes_from_directory(
            stub, args.directory, repeat_for_cache_demo=args.cache_demo
        )
        return

    # Default: run all examples
    print(
        f"\n{COLOR_CYAN}No directory or file specified. Running all examples...{COLOR_RESET}"
    )
    example_runner = ExampleRunner(grpc_client)
    example_runner.run_all(stub)


if __name__ == "__main__":
    main()
