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
    pytest tests/test_examples.py -v

    # Run against a custom server:
    pytest tests/test_examples.py -v --server=localhost:9090

    # Run only the "happy path" tests:
    pytest tests/test_examples.py -v -k "Success"

    # Run only failure-mode tests:
    pytest tests/test_examples.py -v -k "Timeout or Memory"

Design decisions:
    - Timeout/truncation detection uses BOTH the protobuf flag AND stderr
      content, because the server currently communicates these conditions
      via stderr error messages and may not always set the response flags.
    - Cache tests use unique inline code (with timestamps in comments)
      instead of shared example files, to avoid cross-test/cross-run
      cache contamination from a persistent server cache.
    - The "infinite loop" examples are actually bounded loops (100M iters).
      On fast machines they may complete. Tests use truly-infinite inline
      code instead.
"""

from __future__ import annotations

import sys
import time
import uuid
from dataclasses import dataclass
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

    @property
    def timed_out(self) -> bool:
        """Check if execution timed out via flag OR stderr content.

        The server may communicate timeout either through the protobuf
        wall_clock_timeout flag or through an error message in stderr.
        Both signals are valid.
        """
        return (
            self.wall_clock_timeout
            or "timeout" in self.stderr.lower()
            or "wall-clock timeout" in self.stderr.lower()
        )

    @property
    def truncated(self) -> bool:
        """Check if output was truncated via flag OR stderr content.

        The server may communicate truncation either through the protobuf
        output_truncated flag or through a truncation notice in stderr.
        Both signals are valid.
        """
        return (
            self.output_truncated
            or "truncated" in self.stderr.lower()
        )

    @property
    def was_killed(self) -> bool:
        """Check if the process was killed by a signal."""
        return "killed by signal" in self.stderr.lower()

    @property
    def failed(self) -> bool:
        """Check if execution failed by any mechanism."""
        return self.timed_out or self.truncated or self.was_killed or (
            "error" in self.stderr.lower()
        )


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
        assert not result.timed_out, f"Unexpected timeout. stderr={result.stderr!r}"
        assert not result.truncated, f"Unexpected truncation. stderr={result.stderr!r}"
        assert result.execution_time_ms > 0, "Missing execution_time_ms"
        assert result.peak_memory_bytes > 0, "Missing peak_memory_bytes"
        assert "Hello, DCodeX from C!" in result.stdout, (
            f"Expected greeting in stdout. Got: {result.stdout!r}"
        )

    def test_cpp_hello_world(self, stub: Any) -> None:
        """C++ hello world must produce structured output."""
        result = execute(stub, "cpp", read_example("cpp/01_hello_world.cpp"))
        assert not result.timed_out
        assert not result.truncated
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
        assert not result.timed_out
        assert not result.truncated
        assert result.execution_time_ms > 0
        assert "Hello, World!" in result.stdout
        assert "Program completed successfully!" in result.stdout

    def test_python_file_operations(self, stub: Any) -> None:
        """Python file operations runs — output may be truncated by server limits.

        This example produces substantial output (file listings, content dumps,
        temp file demos). The server's output limit may truncate it. We verify
        the early sections are present and that the program started all demos.
        """
        result = execute(
            stub, "python", read_example("python/03_file_operations.py")
        )
        assert not result.timed_out, f"Unexpected timeout. stderr={result.stderr!r}"
        # The program prints these headers in order. Even if truncated,
        # at least the first section must appear.
        assert "File Operations Demo" in result.stdout, (
            f"Missing File Operations section. Got: {result.stdout[:200]!r}"
        )
        # If not truncated, assert the later sections too
        if not result.truncated:
            assert "Path Operations Demo" in result.stdout
            assert "demo completed" in result.stdout

    def test_cpp_arrays_and_vectors(self, stub: Any) -> None:
        """C++ arrays/vectors example must succeed."""
        result = execute(
            stub, "cpp", read_example("cpp/06_arrays_and_vectors.cpp")
        )
        assert not result.timed_out
        assert not result.truncated
        assert result.execution_time_ms > 0

    def test_cpp_memory_allocation(self, stub: Any) -> None:
        """C++ memory allocation demo must succeed."""
        result = execute(
            stub, "cpp", read_example("cpp/08_memory_allocation.cpp")
        )
        assert not result.timed_out
        assert not result.truncated
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
        assert not result.timed_out
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
        assert not result.timed_out
        assert result.execution_time_ms > 0
        assert "Hello from stdin" in result.stdout, (
            f"stdin echo missing. Got: {result.stdout!r}"
        )


# =============================================================================
# TEST CASES: Timeout enforcement
#
# IMPORTANT: The example files named "infinite_loop" are actually BOUNDED
# loops (100M iterations) that complete in < 1 second on fast hardware.
# For reliable timeout testing, we use truly-infinite inline programs.
# =============================================================================


class TestTimeoutEnforcement:
    """Programs that loop forever must be killed by the wall-clock timeout."""

    def test_c_truly_infinite_loop(self, stub: Any) -> None:
        """C program with a genuine infinite loop must be killed."""
        code = (
            '#include <stdio.h>\n'
            'int main(void) {\n'
            '    printf("Starting infinite loop\\n");\n'
            '    fflush(stdout);\n'
            '    for(;;) {}  /* truly infinite */\n'
            '    return 0;\n'
            '}\n'
        )
        result = execute(stub, "c", code)
        assert result.timed_out or result.was_killed, (
            "Truly infinite C loop should be killed. "
            f"stdout={result.stdout[:100]!r} stderr={result.stderr[:200]!r}"
        )
        assert "Starting infinite loop" in result.stdout

    def test_cpp_truly_infinite_loop(self, stub: Any) -> None:
        """C++ program with a genuine infinite loop must be killed."""
        code = (
            '#include <iostream>\n'
            'int main() {\n'
            '    std::cout << "Starting infinite loop" << std::endl;\n'
            '    while(true) {}  // truly infinite\n'
            '}\n'
        )
        result = execute(stub, "cpp", code)
        assert result.timed_out or result.was_killed, (
            "Truly infinite C++ loop should be killed. "
            f"stderr={result.stderr[:200]!r}"
        )
        assert "Starting infinite loop" in result.stdout

    def test_python_truly_infinite_loop(self, stub: Any) -> None:
        """Python program with a genuine infinite loop must be killed."""
        code = (
            'import sys\n'
            'print("Starting infinite loop", flush=True)\n'
            'while True:\n'
            '    pass  # truly infinite\n'
        )
        result = execute(stub, "python", code)
        assert result.timed_out or result.was_killed, (
            "Truly infinite Python loop should be killed. "
            f"stderr={result.stderr[:200]!r}"
        )
        assert "Starting infinite loop" in result.stdout

    def test_bounded_loop_examples_complete_or_timeout(self, stub: Any) -> None:
        """The example 'infinite loop' files are actually bounded loops.

        On fast hardware they complete; on slow hardware they timeout.
        Either outcome is acceptable — the test verifies the server
        handles them without crashing.
        """
        for path, lang in [
            ("c/03_infinite_loop.c", "c"),
            ("cpp/11_infinite_loop.cpp", "cpp"),
            ("python/06_infinite_loop.py", "python"),
        ]:
            result = execute(stub, lang, read_example(path))
            # Either completed or was killed — both are fine
            assert result.wall_time_ms > 0, (
                f"Server should respond for {path}"
            )


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
        # Must NOT succeed: killed by RLIMIT_AS, malloc returns NULL (exit 1),
        # killed by kernel OOM, or timed out allocating.
        assert result.failed or "Allocation failed" in result.stdout or (
            "Reached iteration limit" not in result.stdout
        ), (
            "Memory exhaustion should not complete all 100M iterations. "
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
        # Must not produce successful execution metrics
        assert result.failed or result.execution_time_ms == 0, (
            "Empty program should not execute successfully"
        )

    def test_compilation_error(self, stub: Any) -> None:
        """Broken C++ must produce a compiler error, not crash."""
        result = execute(stub, "cpp", "int main() { return 0 }")
        # Compilation fails — stderr should contain the error
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
        assert result.wall_time_ms > 0, "Server should respond"
        # Either stderr contains error info or execution_time is 0
        # (pipeline returned InternalError before recording time)
        assert result.failed or result.execution_time_ms == 0, (
            f"exit(42) should produce an error indicator. "
            f"stderr={result.stderr!r}"
        )

    def test_segfault(self, stub: Any) -> None:
        """A segfaulting program must produce a signal error, not hang."""
        result = execute(
            stub,
            "cpp",
            '#include <cstddef>\nint main() { int* p = nullptr; return *p; }\n',
        )
        assert not result.timed_out, (
            "Segfault should not trigger timeout — it should die immediately"
        )
        assert result.wall_time_ms > 0
        # Should report being killed by a signal
        assert result.was_killed or result.failed, (
            f"Segfault should produce a signal error. stderr={result.stderr!r}"
        )

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
        """Printing more than the output limit must trigger truncation.

        The dcodex-setup.sh default is 64KB (configurable via
        SERVER_OUTPUT_LIMIT). We print 200KB to comfortably exceed it.
        Truncation is detected via either the protobuf flag or the
        truncation notice in stderr — the server may use either channel.
        """
        code = "print('x' * 204800)\n"
        result = execute(stub, "python", code)
        assert result.truncated, (
            "Expected truncation for 200KB output. "
            f"output_truncated={result.output_truncated} "
            f"stderr={result.stderr[:200]!r}"
        )

    def test_python_syntax_error(self, stub: Any) -> None:
        """Python syntax error must fail cleanly."""
        result = execute(stub, "python", "def broken(\n")
        assert result.wall_time_ms > 0, "Server should respond"
        # Should report an error — either in stderr or via failed status
        assert result.failed, (
            f"Syntax error should produce failure. stderr={result.stderr!r}"
        )


# =============================================================================
# TEST CASES: Cache behavior
#
# Cache tests use unique inline code with a UUID to guarantee the first
# execution is genuinely cold. Using shared example files would fail if
# the server has a persistent cache from a previous test run.
# =============================================================================


class TestCacheBehavior:
    """Second identical execution must be served from cache."""

    def test_cache_hit_python(self, stub: Any) -> None:
        """Running the same Python code twice must produce a cache hit."""
        # Unique code that has never been seen before by this server
        unique_id = uuid.uuid4().hex[:12]
        code = f'print("cache-test-py-{unique_id}")\n'

        # First run — guaranteed cold
        r1 = execute(stub, "python", code)
        assert not r1.cache_hit, (
            f"First run of unique code should not be cached. "
            f"code contained: cache-test-py-{unique_id}"
        )
        assert r1.execution_time_ms > 0, "First run should have execution time"
        assert f"cache-test-py-{unique_id}" in r1.stdout

        # Second run — must be cached
        r2 = execute(stub, "python", code)
        assert r2.cache_hit, (
            f"Second run should be cache hit. Got cache_hit={r2.cache_hit}"
        )
        assert f"cache-test-py-{unique_id}" in r2.stdout, (
            "Cache hit must replay the same output"
        )

    def test_cache_hit_cpp(self, stub: Any) -> None:
        """Running the same C++ code twice must produce a cache hit."""
        unique_id = uuid.uuid4().hex[:12]
        code = (
            '#include <iostream>\n'
            f'int main() {{ std::cout << "cache-test-cpp-{unique_id}" << std::endl; }}\n'
        )

        r1 = execute(stub, "cpp", code)
        assert not r1.cache_hit, "First C++ run of unique code should not be cached"
        assert f"cache-test-cpp-{unique_id}" in r1.stdout

        r2 = execute(stub, "cpp", code)
        assert r2.cache_hit, "Second C++ run should be cache hit"
        assert f"cache-test-cpp-{unique_id}" in r2.stdout

    def test_different_code_no_cache_hit(self, stub: Any) -> None:
        """Different code must NOT produce a cache hit."""
        id_a = uuid.uuid4().hex[:12]
        id_b = uuid.uuid4().hex[:12]

        r1 = execute(stub, "python", f'print("a-{id_a}")\n')
        r2 = execute(stub, "python", f'print("b-{id_b}")\n')

        assert f"a-{id_a}" in r1.stdout
        assert f"b-{id_b}" in r2.stdout
        assert not r2.cache_hit, (
            "Different code should not be served from cache"
        )


# =============================================================================
# TEST CASES: Resource stats
# =============================================================================


class TestResourceStats:
    """Verify that resource usage statistics are populated."""

    def test_stats_populated(self, stub: Any) -> None:
        """A successful execution must report peak memory and time."""
        unique_id = uuid.uuid4().hex[:8]
        result = execute(
            stub,
            "cpp",
            f'#include <iostream>\nint main() {{ std::cout << "stats-{unique_id}"; }}\n',
        )
        assert result.execution_time_ms > 0, (
            "execution_time_ms should be > 0"
        )
        assert result.peak_memory_bytes > 0, (
            "peak_memory_bytes should be > 0"
        )
        assert result.wall_time_ms > 0, "wall_time_ms should be > 0"
