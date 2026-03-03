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

"""Execution orchestration for DCodeX Python Client."""

import time
from pathlib import Path

from python_client.execution_types import CodeExecutorStub
from python_client.file_manager import FileManager
from python_client.formatter import (
    COLOR_BOLD,
    COLOR_CYAN,
    COLOR_GREEN,
    COLOR_RESET,
    COLOR_WHITE,
    COLOR_YELLOW,
)
from python_client.grpc_client import GrpcClient
from python_client.language_detector import LanguageDetector
from python_client.result_printer import ResultPrinter


class Executor:
    """Orchestrates code execution operations."""

    def __init__(
            self,
            grpc_client: GrpcClient,
            file_manager: FileManager | None = None,
            result_printer: ResultPrinter | None = None,
            language_detector: LanguageDetector | None = None,
    ) -> None:
        """Initialize Executor.

        Args:
            grpc_client: GrpcClient instance for code execution.
            file_manager: Optional FileManager instance.
            result_printer: Optional ResultPrinter instance.
            language_detector: Optional LanguageDetector instance.
        """
        self._grpc_client = grpc_client
        self._file_manager = file_manager or FileManager()
        self._result_printer = result_printer or ResultPrinter()
        self._language_detector = language_detector or LanguageDetector()

    def execute_single_code(
            self,
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
        code = self._file_manager.read_code_from_file(file_path)
        language = self._language_detector.detect_from_file(file_path)
        name = file_path.stem

        print(f"\n📝 {name}")
        print("=" * 50)

        self._grpc_client.set_stub(stub)
        results = self._grpc_client.execute_code(
            code,
            language,
            name,
            stdin_data=stdin_data,
        )
        self._result_printer.print_results(results)

    def execute_codes_from_directory(
            self,
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
        codes = self._file_manager.read_codes_from_directory(directory)
        language = self._language_detector.detect_from_directory(directory)

        # Determine extension for error message
        extensions = self._language_detector.get_extensions(language)
        extension = extensions[0] if extensions else ".cpp"

        if not codes:
            print(f"{COLOR_YELLOW}No {extension} files found in {directory}{COLOR_RESET}")
            return

        print(
            f"\n{COLOR_CYAN}📁 Found {len(codes)} code file(s) in {directory}{COLOR_RESET}"
        )
        print("=" * 60)

        self._grpc_client.set_stub(stub)

        for name, (code, file_language) in codes.items():
            print(f"\n{COLOR_WHITE}{COLOR_BOLD}🔴 {name} - First Run{COLOR_RESET}")
            print("-" * 60)
            results1 = self._grpc_client.execute_code(
                code, file_language, name
            )
            self._result_printer.print_results(results1)

            if repeat_for_cache_demo:
                time.sleep(0.5)
                print(
                    f"\n{COLOR_GREEN}{COLOR_BOLD}🟢 {name} - "
                    f"Second Run (Cache Demo){COLOR_RESET}"
                )
                print("-" * 60)
                results2 = self._grpc_client.execute_code(
                    code,
                    file_language,
                    f"{name} - cached",
                )
                self._result_printer.print_results(results2)

                if results2.cache_speedup is not None:
                    print(
                        f"\n{COLOR_CYAN}⚡ Cache Speedup: "
                        f"{COLOR_BOLD}{results2.cache_speedup:.2f}x faster{COLOR_RESET}"
                    )
