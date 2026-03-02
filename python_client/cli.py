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

"""CLI implementation for DCodeX Python Client."""

import argparse
import sys
from pathlib import Path

from python_client.executor import Executor
from python_client.file_manager import FileManager
from python_client.grpc_client import GrpcClient
from python_client.interactive_mode import InteractiveMode
from python_client.result_printer import ResultPrinter


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(description="DCodeX gRPC Client")
    parser.add_argument(
        "--server",
        default="localhost:50051",
        help="Server address (default: localhost:50051)"
    )
    parser.add_argument(
        "--directory", "-d",
        help="Directory containing code files to execute"
    )
    parser.add_argument(
        "--file", "-f",
        help="Single file to execute"
    )
    parser.add_argument(
        "--interactive", "-i",
        action="store_true",
        help="Run in interactive mode"
    )
    parser.add_argument(
        "--stdin",
        default="",
        help="Stdin data for execution (use \\n for newlines)"
    )
    parser.add_argument(
        "--cache-demo", "-c",
        action="store_true",
        help="Run twice to demonstrate caching"
    )
    parser.add_argument(
        "--policy", "-p",
        choices=["default", "strict", "lax"],
        default="default",
        help="Execution resource policy (default: from server flags)"
    )
    parser.add_argument(
        "--language", "-l",
        help="Explicitly specify language (overrides auto-detection)"
    )
    return parser.parse_args()


def main() -> None:
    """Main CLI entry point."""
    args = parse_args()

    # Create the gRPC client
    grpc_client = GrpcClient(args.server)
    file_manager = FileManager()
    result_printer = ResultPrinter()
    executor = Executor(grpc_client, file_manager, result_printer)

    try:
        if args.interactive:
            # Start interactive mode
            interactive_mode = InteractiveMode(grpc_client, executor, file_manager=file_manager)
            interactive_mode.run()
            return

        stdin_data = args.stdin.replace("\\n", "\n")

        if args.file:
            file_path = Path(args.file)
            if not file_path.exists():
                print(f"Error: File '{file_path}' not found.")
                sys.exit(1)
            
            # Execute single file
            executor.execute_file(
                file_path, 
                stdin=stdin_data, 
                policy=args.policy,
                language=args.language
            )
            
            if args.cache_demo:
                print("\n" + "="*80)
                print("CACHE DEMO: Running again (should be instant and mark 'Cache Hit')")
                print("="*80)
                executor.execute_file(
                    file_path, 
                    stdin=stdin_data, 
                    policy=args.policy,
                    language=args.language
                )
            return

        if args.directory:
            dir_path = Path(args.directory)
            if not dir_path.exists() or not dir_path.is_dir():
                print(f"Error: Directory '{dir_path}' not found or is not a directory.")
                sys.exit(1)
            
            # Execute all files in directory
            files = list(dir_path.glob("*.[c|cpp|py]*"))
            if not files:
                print(f"No C, C++, or Python files found in '{dir_path}'.")
                return

            executor.execute_codes_from_directory(
                dir_path, 
                repeat_for_cache_demo=args.cache_demo,
                policy=args.policy,
                language=args.language
            )
            return

        # If no flags provided, show help
        print("DCodeX Client: No action specified.")
        print("Use -h or --help for usage instructions.")
    finally:
        grpc_client.close()


if __name__ == "__main__":
    main()
