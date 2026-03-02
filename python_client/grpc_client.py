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

"""gRPC client for DCodeX Python Client."""

from __future__ import annotations

import os
import sys
import time
from typing import TYPE_CHECKING

import grpc

from python_client.proto import sandbox_pb2
from python_client.proto import sandbox_pb2_grpc

from python_client.formatter import (
    COLOR_RED,
    COLOR_RESET,
)
from python_client.execution_types import ExecutionResult

if TYPE_CHECKING:
    from collections.abc import Iterator
    from typing import Any


class GrpcClient:
    """gRPC client for communicating with the DCodeX server."""

    def __init__(self, server_address: str = "localhost:50051") -> None:
        """Initialize GrpcClient.

        Args:
            server_address: Address of the DCodeX server.
        """
        self._server_address = server_address
        self._channel = grpc.insecure_channel(server_address)
        self._stub = sandbox_pb2_grpc.CodeExecutorStub(self._channel)

    def get_stub(self):
        """Returns the gRPC stub."""
        return self._stub

    def close(self) -> None:
        """Close the gRPC channel."""
        self._channel.close()

    def execute_code(
            self,
            code: str,
            language: str,
            description: str = "",
            stdin_data: str = "",
            policy: str = "default",
    ) -> ExecutionResult:
        """Execute code and return results with timing.

        Args:
            code: Code to execute.
            language: Execution language ("c", "cpp", or "python").
            description: Optional description of the code.
            stdin_data: Data to feed to the program's standard input.
                Empty string means the program receives EOF immediately.
            policy: Execution policy ("default", "strict", "lax").

        Returns:
            ExecutionResult containing stdout, stderr, timing, and cache status.
        """
        request: Any = sandbox_pb2.CodeRequest(
            language=language,
            code=code,
            stdin_data=stdin_data,
            policy=policy,
        )

        start_time = time.time()
        responses: Iterator[Any] = self._stub.Execute(request)

        peak_memory = 0
        execution_time = 0.0
        cache_hit = False
        wall_clock_timeout = False
        output_truncated = False
        cached_execution_time = 0.0
        stdout_output: list[str] = []
        stderr_output: list[str] = []

        try:
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
        except grpc.RpcError as e:
            print(f"{COLOR_RED}gRPC Error: {e.details()}{COLOR_RESET}")
            return ExecutionResult(
                stdout="".join(stdout_output),
                stderr=f"gRPC Error: {e.details()}",
                peak_memory=0,
                execution_time=0.0,
                cache_hit=False,
                actual_time=0.0,
            )

        actual_time = (time.time() - start_time) * 1000
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
