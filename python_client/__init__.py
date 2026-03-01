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

"""DCodeX Python Client Package.

A modular gRPC client for executing C++ and Python code with resource monitoring,
smart caching, and sandboxed execution.
"""

from python_client.example_generator import ExampleGenerator
from python_client.execution_types import ExecutionResult
from python_client.executor import Executor
from python_client.file_manager import FileManager
from python_client.formatter import format_bytes, format_duration
from python_client.grpc_client import GrpcClient
from python_client.interactive_mode import ExampleRunner, InteractiveMode
from python_client.language_detector import LanguageDetector
from python_client.result_printer import ResultPrinter

__all__ = [
    "ExampleGenerator",
    "ExampleRunner",
    "Executor",
    "ExecutionResult",
    "FileManager",
    "GrpcClient",
    "InteractiveMode",
    "LanguageDetector",
    "ResultPrinter",
    "format_bytes",
    "format_duration",
]

__version__ = "1.0.0"
