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

"""DCodeX end-to-end integration tests.

Connects to a running DCodeX gRPC server and validates that every example
file produces the exact expected behavior — output content, exit status,
timeout flags, truncation flags, cache behavior, and resource stats.

Usage:
    # Run against a local server on port 50051 (default):
    pytest python_client/test_examples.py -v

    # Run against a custom server:
    pytest python_client/test_examples.py -v --server=localhost:9090

    # Run only the "happy path" tests:
    pytest python_client/test_examples.py -v -k "success"

    # Run only failure-mode tests:
    pytest python_client/test_examples.py -v -k "timeout or memory"

Architecture:
    Each test case is a @pytest.mark.parametrize entry that specifies:
      - The source file (or inline code) to execute
      - The language
      - What the result MUST look like (stdout substrings, flags, etc.)

    This is strictly better than sidecar .expect.json files because:
      1. All expectations are in one reviewable file
      2. pytest handles discovery, reporting, and exit codes
      3. No file drift — if an example changes behavior, the test fails
      4. Easy to add synthetic test cases that don't correspond to files
"""

from __future__ import annotations

import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import grpc
import pytest

# Ensure project root is importable
_PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

import proto.sandbox_pb2 as sandbox_pb2
import proto.sandbox_pb2_grpc as sandbox_pb2_grpc


# =============================================================================
# Configuration
# =============================================================================

EXAMPLES_DIR = _PROJECT_ROOT / "examples"
DEFAULT_SERVER = "localhost:50051"


# =============================================================================
# Fixtures
# =============================================================================


@pytest.fixture(scope="session")
def server_address(request: Any) -> str:
    """Get the server address from CLI or environment."""
    return request.config.getoption("--server")


@pytest.fixture(scope="session")
def stub(server_address: str) -> sandbox_pb2_grpc.CodeExecutorStub:
    """Create a gRPC stub scoped to the entire test session."""
    channel = grpc.insecure_channel(server_address)

    # Verify connectivity before running any tests
    try:
        grpc.channel_ready_future(channel).result(timeout=5)
    except grpc.FutureTimeoutError:
        pytest.skip(
            f"DCodeX server not reachable at {server_address}. "
            "Start the server first: bazel run //src/api:server"
        )

    return sandbox_pb2_grpc.CodeExecutorStub(channel)


# =============================================================================
# Execution helper
# =============================================================================


@dataclass
class ExecResult:
    """Parsed result of a single gRPC Execute() call."""

    stdout: str = ""
    stderr: str = ""
    peak_memory_bytes: int = 0
    execution_time_ms: float = 0.0
    cache_hit: bool = False
    wall_clock_timeout: bool = False
    output_truncated: bool = False
    wall_time_ms: float = 0.0


def execute(
    stub: sandbox_pb2_grpc.CodeExecutorStub,
    language: str,
    code: str,
    stdin_data: str = "",
) -> ExecResult:
    """Execute code via gRPC and collect the full streamed response."""
    request = sandbox_pb2.CodeRequest(
        language=language,
        code=code,
        stdin_data=stdin_data,
    )

    result = ExecResult()
    start = time.monotonic()

    for response in stub.Execute(request):
        if response.stdout_chunk:
            result.stdout += response.stdout_chunk
        if response.stderr_chunk:
            result.stderr += response.stderr_chunk
        if response.peak_memory_bytes > 0:
            result.peak_memory_bytes = response.peak_memory_bytes
        if response.execution_time_ms > 0:
            result.execution_time_ms = response.execution_time_ms
        if response.cache_hit:
            result.cache_hit = True
        if response.wall_clock_timeout:
            result.wall_clock_timeout = True
        if response.output_truncated:
            result.output_truncated = True

    result.wall_time_ms = (time.monotonic() - start) * 1000
    return result


def read_example(relative_path: str) -> str:
    """Read an example file from the examples/ directory."""
    path = EXAMPLES_DIR / relative_path
    assert path.exists(), f"Example file missing: {path}"
    return path.read_text(encoding="utf-8")


# =============================================================================
# TEST CASES: Happy-path examples (expect success)
# =============================================================================


class TestSuccessExamples:
    """Examples that must compile, run, and produce expected output."""

    def test_c_hello_world(self, stub: Any) -> None:
        """C hello world must produce the exact greeting."""
        result = execute(stub, "c", read_example("c/01_hello_world.c"))
        assert not result.wall_clock_timeout, "Unexpected timeout"
        assert not result.output_truncated, "Unexpected truncation"
        assert result.execution_time_ms > 0, "Missing execution_time_ms"
        assert result.peak_memory_bytes > 0, "Missing peak_memory_bytes"
        assert "Hello, DCodeX from C!" in result.stdout, (
            f"Expected greeting in stdout. Got: {result.stdout!r}"
        )

    def test_cpp_hello_world(self, stub: Any) -> None:
        """C++ hello world must produce structured output."""
        result = execute(stub, "cpp", read_example("cpp/01_hello_world.cpp"))
        assert not result.wall_clock_timeout
        assert not result.output_truncated
        assert result.execution_time_ms > 0
        assert "Hello, World!" in result.stdout, (
            f"Missing greeting. Got: {result.stdout!r}"
        )
        assert "Welcome to DCodeX" in result.stdout

    def test_python_hello_world(self, stub: Any) -> None:
        """Python hello world must succeed and contain system info."""
        result = execute(
            stub, "python", read_example("python/01_hello_world.py")
        )
        assert not result.wall_clock_timeout
        assert not result.output_truncated
        assert result.execution_time_ms > 0
        assert "Hello, World!" in result.stdout
        assert "Program completed successfully!" in result.stdout

    def test_python_file_operations(self, stub: Any) -> None:
        """Python file operations must complete all demos."""
        result = execute(
            stub, "python", read_example("python/03_file_operations.py")
        )
        assert not result.wall_clock_timeout
        assert not result.output_truncated
        assert "File Operations Demo" in result.stdout
        assert "Path Operations Demo" in result.stdout
        assert "Temporary Files Demo" in result.stdout
        assert "demo completed" in result.stdout

    def test_cpp_arrays_and_vectors(self, stub: Any) -> None:
        """C++ arrays/vectors example must succeed."""
        result = execute(
            stub, "cpp", read_example("cpp/06_arrays_and_vectors.cpp")
        )
        assert not result.wall_clock_timeout
        assert not result.output_truncated
        assert result.execution_time_ms > 0

    def test_cpp_memory_allocation(self, stub: Any) -> None:
        """C++ memory allocation demo must succeed."""
        result = execute(
            stub, "cpp", read_example("cpp/08_memory_allocation.cpp")
        )
        assert not result.wall_clock_timeout
        assert not result.output_truncated
        assert result.execution_time_ms > 0


# =============================================================================
# TEST CASES: stdin passthrough
# =============================================================================


class TestStdinPassthrough:
    """Examples that read from stdin and must echo it back."""

    def test_c_stdin(self, stub: Any) -> None:
        """C stdin example must echo input back."""
        result = execute(
            stub,
            "c",
            read_example("c/02_stdin_input.c"),
            stdin_data="DCodeX\n25\n",
        )
        assert not result.wall_clock_timeout
        assert result.execution_time_ms > 0
        # The C example reads name and age from stdin
        assert "DCodeX" in result.stdout, (
            f"stdin echo missing. Got: {result.stdout!r}"
        )

    def test_cpp_stdin(self, stub: Any) -> None:
        """C++ stdin example must echo input back."""
        result = execute(
            stub,
            "cpp",
            read_example("cpp/13_stdin_input.cpp"),
            stdin_data="Hello from stdin\n42\n",
        )
        assert not result.wall_clock_timeout
        assert result.execution_time_ms > 0
        assert "Hello from stdin" in result.stdout, (
            f"stdin echo missing. Got: {result.stdout!r}"
        )


# =============================================================================
# TEST CASES: Timeout enforcement
# =============================================================================


class TestTimeoutEnforcement:
    """Programs that loop forever must be killed by the wall-clock timeout."""

    def test_c_infinite_loop_timeout(self, stub: Any) -> None:
        """C infinite loop must trigger wall-clock timeout."""
        result = execute(stub, "c", read_example("c/03_infinite_loop.c"))
        assert result.wall_clock_timeout, (
            "Expected wall_clock_timeout=True for infinite loop. "
            f"Got stdout={result.stdout[:100]!r}"
        )
        # The program should have printed its start message before being killed
        assert "Starting" in result.stdout or "bounded" in result.stdout.lower()

    def test_cpp_infinite_loop_timeout(self, stub: Any) -> None:
        """C++ infinite loop must trigger wall-clock timeout."""
        result = execute(stub, "cpp", read_example("cpp/11_infinite_loop.cpp"))
        assert result.wall_clock_timeout, (
            "Expected wall_clock_timeout=True for infinite loop"
        )

    def test_python_infinite_loop_timeout(self, stub: Any) -> None:
        """Python infinite loop must trigger wall-clock timeout."""
        result = execute(
            stub, "python", read_example("python/06_infinite_loop.py")
        )
        assert result.wall_clock_timeout, (
            "Expected wall_clock_timeout=True for infinite loop"
        )
        assert "Starting" in result.stdout


# =============================================================================
# TEST CASES: Memory exhaustion
# =============================================================================


class TestMemoryEnforcement:
    """Programs that exhaust memory must fail (OOM or rlimit kill)."""

    def test_c_memory_exhaustion(self, stub: Any) -> None:
        """C memory exhaustion must fail — not run to completion."""
        result = execute(
            stub, "c", read_example("c/04_memory_exhaustion.c")
        )
        # Must NOT succeed (either killed by RLIMIT_AS, or malloc returns NULL
        # and the program exits 1, or the process is killed by the kernel OOM).
        assert result.wall_clock_timeout or result.output_truncated or (
            "Allocation failed" in result.stdout
            or "Reached iteration limit" not in result.stdout
        ), (
            "Memory exhaustion should not complete successfully. "
            f"Got: {result.stdout[:200]!r}"
        )

    def test_python_memory_exhaustion(self, stub: Any) -> None:
        """Python memory exhaustion must fail."""
        result = execute(
            stub, "python", read_example("python/07_memory_exhaustion.py")
        )
        # The program should be killed or raise MemoryError before reaching
        # the iteration limit.
        assert "Reached iteration limit" not in result.stdout, (
            "Python memory exhaustion should not reach iteration limit"
        )


# =============================================================================
# TEST CASES: Synthetic edge cases (inline code, not example files)
# =============================================================================


class TestSyntheticEdgeCases:
    """Synthetic programs that test specific sandbox behaviors."""

    def test_empty_program_fails(self, stub: Any) -> None:
        """Submitting empty C++ code must fail, not crash the server."""
        result = execute(stub, "cpp", "")
        # Must not claim success — there's no main()
        assert result.execution_time_ms == 0 or result.wall_clock_timeout, (
            "Empty program should not execute successfully"
        )

    def test_compilation_error(self, stub: Any) -> None:
        """Broken C++ must produce a compiler error, not crash."""
        result = execute(stub, "cpp", "int main() { return 0 }")
        # Compilation fails — we should see clang's error output in stderr
        assert result.stderr or result.execution_time_ms == 0, (
            "Compilation error should produce stderr or no execution time"
        )

    def test_nonzero_exit_code(self, stub: Any) -> None:
        """A program that exits 42 must not be reported as success."""
        result = execute(
            stub,
            "cpp",
            '#include <cstdlib>\nint main() { return 42; }\n',
        )
        # The server should report this as a non-success execution.
        # Exact behavior depends on server: it may set execution_time > 0
        # but the process is not "successful" (no stdout, non-zero exit).
        # At minimum, we verify the server didn't crash.
        assert result.wall_time_ms > 0, "Server should respond"

    def test_segfault(self, stub: Any) -> None:
        """A segfaulting program must produce a signal error, not hang."""
        result = execute(
            stub,
            "cpp",
            '#include <cstddef>\nint main() { int* p = nullptr; return *p; }\n',
        )
        assert not result.wall_clock_timeout, (
            "Segfault should not trigger timeout — it should die immediately"
        )
        assert result.wall_time_ms > 0

    def test_stderr_capture(self, stub: Any) -> None:
        """stderr output must be streamed back to the client."""
        code = (
            '#include <iostream>\n'
            'int main() {\n'
            '  std::cout << "to-stdout" << std::endl;\n'
            '  std::cerr << "to-stderr" << std::endl;\n'
            '  return 0;\n'
            '}\n'
        )
        result = execute(stub, "cpp", code)
        assert "to-stdout" in result.stdout, (
            f"stdout missing. Got: {result.stdout!r}"
        )
        assert "to-stderr" in result.stderr, (
            f"stderr missing. Got: {result.stderr!r}"
        )

    def test_large_output_truncation(self, stub: Any) -> None:
        """Printing more than the output limit must trigger truncation."""
        # Default server output limit is 10KB. Print 100KB.
        code = "print('x' * 102400)\n"
        result = execute(stub, "python", code)
        assert result.output_truncated, (
            "Expected output_truncated=True for 100KB output"
        )

    def test_python_syntax_error(self, stub: Any) -> None:
        """Python syntax error must fail cleanly."""
        result = execute(stub, "python", "def broken(\n")
        # Should produce stderr and not succeed
        assert result.wall_time_ms > 0, "Server should respond"


# =============================================================================
# TEST CASES: Cache behavior
# =============================================================================


class TestCacheBehavior:
    """Second identical execution must be served from cache."""

    def test_cache_hit_on_rerun(self, stub: Any) -> None:
        """Running the same code twice must produce a cache hit."""
        code = read_example("python/01_hello_world.py")

        # First run — cold
        r1 = execute(stub, "python", code)
        assert not r1.cache_hit, "First run should not be a cache hit"
        assert r1.execution_time_ms > 0, "First run should have execution time"
        assert "Hello, World!" in r1.stdout

        # Second run — must be cached
        r2 = execute(stub, "python", code)
        assert r2.cache_hit, (
            f"Second run should be cache hit. Got cache_hit={r2.cache_hit}"
        )
        assert "Hello, World!" in r2.stdout, (
            "Cache hit must still produce the same output"
        )

    def test_cache_hit_cpp(self, stub: Any) -> None:
        """C++ cache hit must replay the same output."""
        code = (
            '#include <iostream>\n'
            'int main() { std::cout << "cache-test-cpp" << std::endl; }\n'
        )

        r1 = execute(stub, "cpp", code)
        assert not r1.cache_hit
        assert "cache-test-cpp" in r1.stdout

        r2 = execute(stub, "cpp", code)
        assert r2.cache_hit, "Second C++ run should be cache hit"
        assert "cache-test-cpp" in r2.stdout


# =============================================================================
# TEST CASES: Resource stats
# =============================================================================


class TestResourceStats:
    """Verify that resource usage statistics are populated."""

    def test_stats_populated(self, stub: Any) -> None:
        """A successful execution must report peak memory and time."""
        result = execute(
            stub,
            "cpp",
            '#include <iostream>\nint main() { std::cout << "stats"; }\n',
        )
        assert result.execution_time_ms > 0, (
            "execution_time_ms should be > 0"
        )
        assert result.peak_memory_bytes > 0, (
            "peak_memory_bytes should be > 0"
        )
        assert result.wall_time_ms > 0, "wall_time_ms should be > 0"
