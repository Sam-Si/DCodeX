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

"""Interactive mode for DCodeX Python Client."""

from pathlib import Path

from python_client.example_generator import ExampleGenerator
from python_client.execution_types import CodeExecutorStub
from python_client.executor import Executor
from python_client.file_manager import FileManager
from python_client.formatter import (
    COLOR_BOLD,
    COLOR_CYAN,
    COLOR_GREEN,
    COLOR_MAGENTA,
    COLOR_RESET,
    COLOR_YELLOW,
)
from python_client.grpc_client import GrpcClient
from python_client.result_printer import ResultPrinter


class InteractiveMode:
    """Handles interactive mode for the DCodeX client."""

    def __init__(
            self,
            grpc_client: GrpcClient,
            executor: Executor | None = None,
            example_generator: ExampleGenerator | None = None,
            file_manager: FileManager | None = None,
    ) -> None:
        """Initialize InteractiveMode.

        Args:
            grpc_client: GrpcClient instance for code execution.
            executor: Optional Executor instance.
            example_generator: Optional ExampleGenerator instance.
            file_manager: Optional FileManager instance.
        """
        self._grpc_client = grpc_client
        self._executor = executor or Executor(grpc_client)
        self._example_generator = example_generator or ExampleGenerator()
        self._file_manager = file_manager or FileManager()

    def run(self) -> None:
        """Run interactive mode."""
        print("\n" + "=" * 60)
        print(f"{COLOR_CYAN}{COLOR_BOLD}DCodeX Python Client - Interactive Mode{COLOR_RESET}")
        print("=" * 60)
        print(
            f"{COLOR_GREEN}Enter code to execute (press Ctrl+D on Unix or Ctrl+Z+Enter on Windows to submit):{COLOR_RESET}")
        print(f"{COLOR_YELLOW}Type 'exit' to quit, 'help' for commands{COLOR_RESET}\n")

        while True:
            print(f"\n{COLOR_MAGENTA}>>> {COLOR_RESET}", end="", flush=True)

            lines = []
            try:
                while True:
                    try:
                        line = input()
                        if line.strip() == "exit":
                            print(f"{COLOR_GREEN}Goodbye!{COLOR_RESET}")
                            return
                        if line.strip() == "help":
                            self._print_help()
                            break
                        lines.append(line)
                    except EOFError:
                        break
            except KeyboardInterrupt:
                print(f"\n{COLOR_YELLOW}Interrupted. Type 'exit' to quit.{COLOR_RESET}")
                continue

            if not lines:
                continue

            code = "\n".join(lines)
            language = self._detect_language_from_code(code)

            print(f"\n{COLOR_CYAN}Detected language: {language}{COLOR_RESET}")
            print("-" * 50)

            try:
                result = self._grpc_client.execute_code(code, language)
                ResultPrinter().print_results(result)
            except Exception as e:
                print(f"{COLOR_YELLOW}Error: {e}{COLOR_RESET}")

    def _print_help(self) -> None:
        """Print help message."""
        print(f"\n{COLOR_CYAN}{COLOR_BOLD}Available Commands:{COLOR_RESET}")
        print(f"  {COLOR_GREEN}exit{COLOR_RESET} - Exit interactive mode")
        print(f"  {COLOR_GREEN}help{COLOR_RESET} - Show this help message")
        print(f"\n{COLOR_CYAN}{COLOR_BOLD}How to use:{COLOR_RESET}")
        print("  1. Type your code")
        print("  2. Press Ctrl+D (Unix) or Ctrl+Z+Enter (Windows) to submit")
        print("  3. Language is auto-detected from code content")

    def _detect_language_from_code(self, code: str) -> str:
        """Detect language from code content.

        Args:
            code: Source code to analyze.

        Returns:
            Detected language ("c", "cpp", or "python").
        """
        # Simple heuristic: check for Python-specific syntax
        if any(keyword in code for keyword in ["def ", "import ", "print(", "if __name__"]):
            return "python"
        # Check for C-specific (not C++)
        if "printf(" in code and "std::" not in code:
            return "c"
        return "cpp"


class ExampleRunner:
    """Runs all default examples."""

    def __init__(
            self,
            grpc_client: GrpcClient,
            executor: Executor | None = None,
            example_generator: ExampleGenerator | None = None,
    ) -> None:
        """Initialize ExampleRunner.

        Args:
            grpc_client: GrpcClient instance for code execution.
            executor: Optional Executor instance.
            example_generator: Optional ExampleGenerator instance.
        """
        self._grpc_client = grpc_client
        self._executor = executor or Executor(grpc_client)
        self._example_generator = example_generator or ExampleGenerator()

    def run_all(self) -> None:
        """Run all default examples."""
        examples_dir = Path("examples")
        self._example_generator.create_examples_directory(examples_dir)

        print("\n" + "🚀" * 30)
        print("  DCodeX Feature Showcase")
        print("  Features: Resource Monitoring | Smart Caching | Sandboxing")
        print("🚀" * 30)

        self._executor.execute_codes_from_directory(
            examples_dir,
            repeat_for_cache_demo=True,
        )

        print("\n" + "=" * 60)
        print("✨ All examples completed!")
        print("Features demonstrated:")
        print("  📊 Peak memory tracking")
        print("  ⏱️  Execution time measurement")
        print("  💾 Result caching with absl::Hash")
        print("  ⚡ Cache hit/miss detection")
        print("  🛡️  Sandboxed execution")
        print("=" * 60)
