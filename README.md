# DCodeX

A gRPC-powered code execution engine with secure sandboxing, real-time streaming, and smart caching.

## Quick Start

```bash
# Build the server
bazel build //src/api:server

# Run the server (listens on localhost:50051)
bazel run //src/api:server

# Install Python client dependencies
pip install -r python_client/requirements.txt

# Generate Python gRPC bindings
python -m grpc_tools.protoc -I. --python_out=. --grpc_python_out=. proto/sandbox.proto

# Run the client with individual files
python python_client/main.py --file examples/c/01_hello_world.c
python python_client/main.py --file examples/cpp/01_hello_world.cpp
python python_client/main.py --file examples/python/01_hello_world.py
```

## Server Options

| Flag | Default | Description |
|------|---------|-------------|
| `--port` | 50051 | Server port |
| `--max_concurrent_sandboxes` | 10 | Max concurrent executions |
| `--sandbox_cpu_time_limit_seconds` | 1 | CPU time limit (seconds) |
| `--sandbox_wall_clock_timeout_seconds` | 2 | Wall-clock timeout (seconds) |
| `--sandbox_memory_limit_bytes` | 4294967296 | Memory limit (bytes, default 4GB) |
| `--sandbox_max_output_bytes` | 10240 | Max stdout+stderr (bytes, default 10KB) |

```bash
# Example: Custom port with 8GB memory limit
bazel run //src/api:server -- --port 9090 --sandbox_memory_limit_bytes 8589934592

# Example: Strict limits for testing untrusted code
bazel run //src/api:server -- --sandbox_cpu_time_limit_seconds 3 --sandbox_memory_limit_bytes 52428800
```

## Python Client

```bash
# Execute all C examples
python python_client/main.py --directory examples/c

# Execute all C++ examples
python python_client/main.py --directory examples/cpp

# Execute all Python examples  
python python_client/main.py --directory examples/python

# Execute a single file (language auto-detected from extension)
python python_client/main.py --file examples/c/01_hello_world.c
python python_client/main.py --file examples/cpp/01_hello_world.cpp
python python_client/main.py --file examples/python/01_hello_world.py

# Run each file twice to demonstrate caching
python python_client/main.py --directory examples/c --cache-demo

# Interactive mode
python python_client/main.py --interactive

# Connect to remote server
python python_client/main.py --server 192.168.1.100:50051 --directory examples/cpp

# Pass stdin data to a program
python python_client/main.py --file examples/cpp/13_stdin_input.cpp --stdin 'DCodeX\n5\n10\n20\n30\n40\n50\n'

# Pass stdin from a file
python python_client/main.py --file examples/cpp/13_stdin_input.cpp --stdin-file /tmp/input.txt
```

### Client Options

| Flag | Short | Default | Description |
|------|-------|---------|-------------|
| `--server` | | localhost:50051 | Server address |
| `--directory` | `-d` | examples/cpp | Directory of code files |
| `--file` | `-f` | None | Single file to execute |
| `--cache-demo` | `-c` | False | Run twice to show caching |
| `--interactive` | `-i` | False | Interactive menu mode |
| `--stdin` | | "" | Stdin data (use `\n` for newlines) |
| `--stdin-file` | | None | File to read stdin from |

## Examples

### C (`examples/c/`)

| File                  | Description                  |
|-----------------------|------------------------------|
| `01_hello_world.c`    | Basic I/O                    |
| `02_basic_math.c`     | Math operations              |
| `03_pointers.c`       | Pointer arithmetic and swap  |
| `04_structs.c`        | Struct definitions and usage |
| `05_file_io.c`        | File read/write operations   |
| `06_dynamic_memory.c` | malloc/calloc/realloc/free   |

### C++ (`examples/cpp/`)

| File | Description |
|------|-------------|
| `01_hello_world.cpp` | Basic I/O |
| `02_basic_math.cpp` | Math operations |
| `03_fibonacci.cpp` | Fibonacci sequence |
| `04_prime_numbers.cpp` | Prime finder |
| `05_factorial.cpp` | Factorial calculator |
| `06_arrays_and_vectors.cpp` | STL containers |
| `07_strings.cpp` | String manipulation |
| `08_memory_allocation.cpp` | Memory management |
| `09_cpu_intensive.cpp` | Heavy computation |
| `10_sandbox_safe.cpp` | Resource-conscious code |
| `11_infinite_loop.cpp` | Timeout demo |
| `12_memory_exhaustion.cpp` | Memory limit demo |
| `13_stdin_input.cpp` | Stdin handling |
| `14_output_flood.cpp` | Output truncation demo |

### Python (`examples/python/`)

| File | Description |
|------|-------------|
| `01_hello_world.py` | Basic I/O |
| `02_basic_math.py` | Math operations |
| `03_file_operations.py` | File I/O |
| `04_data_structures.py` | Data structures |
| `05_iterators_generators.py` | Iterators/generators |
| `06_infinite_loop.py` | Timeout demo |
| `07_memory_exhaustion.py` | Memory limit demo |
| `08_slow_computation.py` | CPU limit demo |
| `09_output_flood.py` | Output truncation demo |

## Features

- **Secure Sandboxing**: Linux `rlimit` enforces CPU/memory constraints
- **Wall-Clock Timeout**: Catches sleeping/blocked processes that CPU limits miss
- **gRPC Alarm-Based Timeout**: Efficient timeout handling without fork overhead
- **Output Limiting**: Hard cap on stdout+stderr (default 10KB) prevents flooding
- **Stdin Support**: Pass input data via `--stdin` or `--stdin-file`
- **Real-time Streaming**: gRPC bidirectional streaming for live output
- **Smart Caching**: `absl::Hash`-based LRU cache with 1-hour TTL
- **Multi-Language Support**: C, C++, and Python with auto-detection from file extension

## Architecture: gRPC Alarm for Process Timeout

The sandbox uses **gRPC Alarm** for efficient process timeout handling instead of the traditional fork-based watcher
process approach.

### Before: Fork-Based Watcher (DEPRECATED)

```cpp
// Old approach - creates an extra process for each timeout
pid_t watcher_pid = fork();
if (watcher_pid == 0) {
    for (int fd = 0; fd < 1024; ++fd) close(fd);
    sleep(timeout_seconds);
    if (kill(pid, 0) == 0) kill(pid, SIGKILL);
    _exit(0);
}
```

**Problems with fork-based approach:**

- Creates an extra process for each timeout (process table pollution)
- Duplicates parent's address space (memory overhead)
- Requires complex cleanup (zombie process handling)
- Not integrated with gRPC server lifecycle
- Race conditions between watcher and main process

### After: gRPC Alarm-Based Timeout

```cpp
// New approach - uses gRPC Alarm for efficient timeout
auto timeout_manager = std::make_unique<ProcessTimeoutManager>(
    pid, timeout, [&timed_out_flag, pid]() {
      timed_out_flag.store(true);
      if (kill(pid, 0) == 0) kill(pid, SIGKILL);
    });
timeout_manager->Start();
```

**Benefits of gRPC Alarm approach:**

| Aspect           | Fork-Based              | gRPC Alarm        |
|------------------|-------------------------|-------------------|
| Extra processes  | 1 per timeout           | 0                 |
| Memory overhead  | Full address space copy | Minimal           |
| Zombie handling  | Required                | Not needed        |
| gRPC integration | None                    | Native            |
| Cancellation     | Complex                 | Simple `Cancel()` |
| Thread safety    | Manual                  | `absl::Mutex`     |

### Demo: Comparing Approaches

To demonstrate the benefits:

```bash
# Terminal 1: Monitor process count
watch -n 0.5 'ps -e | wc -l'

# Terminal 2: Start server
bazel run //src/api:server

# Terminal 3: Send concurrent requests with timeouts
for i in {1..50}; do
  python python_client/main.py --file examples/python/06_infinite_loop.py &
done
```

**Expected Results:**

- **Old approach**: Process count increases by 2x (main + watcher for each request)
- **New approach**: Process count increases by 1x (only the sandboxed process)

### Execution Pipeline Architecture

The code execution follows a **Command Pattern** (GoF) with discrete execution steps:

```
ExecutionContext
       │
       ▼
┌─────────────────────┐
│ CreateSourceFileStep│  Creates temp file with code
└─────────────────────┘
       │
       ▼
┌─────────────────────┐
│    CompileStep      │  Compiles source (C++ only)
└─────────────────────┘
       │
       ▼
┌─────────────────────┐
│   RunProcessStep    │  Executes with sandboxing
│   + gRPC Alarm      │  Timeout management
└─────────────────────┘
       │
       ▼
┌─────────────────────┐
│ FinalizeResultStep  │  Formats result and trace
└─────────────────────┘
       │
       ▼
  ExecutionResult
```

## Project Structure

```
DCodeX/
├── src/api/                      # C++ gRPC server
│   ├── main.cpp                  # Server entry point
│   ├── code_executor_service.h/cpp   # gRPC service implementation
│   └── execute_reactor.h/cpp     # gRPC stream reactor
├── src/engine/                   # Execution engine
│   ├── sandbox.h/cpp             # SandboxedProcess orchestrator
│   ├── execution_types.h         # ExecutionResult, ResourceStats
│   ├── execution_step.h          # Command pattern steps
│   ├── execution_pipeline.h      # Template method pipeline
│   ├── execution_strategy.h      # Strategy pattern (C/C++/Python)
│   ├── process_timeout_manager.h # gRPC Alarm-based timeout
│   ├── warm_worker_pool.h/cpp    # Worker pool for concurrency
│   ├── process_runner.h          # RAII process management
│   ├── output_filter.h           # Output truncation
│   └── temp_file_manager.h       # Temp file utilities
├── src/common/                   # Common utilities
│   ├── execution_cache.h/cpp     # LRU cache with TTL
│   └── status_macros.h           # Local ABSL_RETURN_IF_ERROR macros
├── proto/
│   ├── sandbox.proto             # gRPC protocol definition
│   └── BUILD                     # Proto/CC gRPC library
├── python_client/                # Python client
│   ├── main.py                   # Entry point
│   ├── grpc_client.py            # gRPC client wrapper
│   ├── executor.py               # Execution orchestration
│   ├── formatter.py              # Output formatting
│   └── ...
├── examples/                     # Example code files
│   ├── c/                        # C examples
│   ├── cpp/                      # C++ examples
│   └── python/                   # Python examples
├── pyproject.toml                # Python project config
└── MODULE.bazel                  # Bazel workspace
```

## Architecture Overview

### Design Patterns

The codebase follows several GoF design patterns for clean separation of concerns:

| Pattern | Component | Purpose |
|---------|-----------|---------|
| **Command** | `ExecutionStep` | Encapsulates each pipeline step as an object |
| **Template Method** | `ExecutionPipeline` | Defines skeleton algorithm, steps fill in details |
| **Strategy** | `ExecutionStrategy` | Interchangeable execution algorithms (C/C++/Python) |
| **Dependency Injection** | `CacheInterface` | Decouples cache implementation from consumers |

### Dependency Injection

The codebase uses constructor-based dependency injection for testability and flexibility:

```cpp
// CacheInterface is injected, not statically accessed
class SandboxedProcess {
 public:
  explicit SandboxedProcess(std::shared_ptr<CacheInterface> cache);
 private:
  std::shared_ptr<CacheInterface> cache_;
};

// Server creates and injects the cache
auto cache = std::make_shared<ExecutionCache>(absl::Hours(1), 1000);
CodeExecutorServiceImpl service(max_sandboxes, std::move(cache));
```

### Error Handling

Local macros provide clean error handling without external dependencies:

```cpp
// src/common/status_macros.h
#define ABSL_RETURN_IF_ERROR(expr) \
  if (const absl::Status _status = (expr); !_status.ok()) return _status

#define ABSL_ASSIGN_OR_RETURN(lhs, rexpr) \
  auto _result = (rexpr); \
  if (!_result.ok()) return _result.status(); \
  lhs = std::move(_result).value()
```

## License

Apache License 2.0
