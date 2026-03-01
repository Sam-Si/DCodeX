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

"""Type definitions for DCodeX Python Client."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

# Type alias for gRPC CodeExecutor stub
CodeExecutorStub = Any
"""Type alias for the gRPC CodeExecutor stub."""


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
