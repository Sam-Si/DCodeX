# DCodeX

A gRPC-powered code execution engine with secure sandboxing, real-time streaming, and smart caching.

## Quick Start

```bash
# Build the server
bazel build //src/server:server

# Run the server (listens on localhost:50051)
bazel run //src/server:server

# Install Python client dependencies
pip install -r python_client/requirements.txt

# Generate Python gRPC bindings
python -m grpc_tools.protoc -I. --python_out=. --grpc_python_out=. proto/sandbox.proto

# Run the client (executes all examples with cache demo)
python python_client/client.py
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
bazel run //src/server:server -- --port 9090 --sandbox_memory_limit_bytes 8589934592

# Example: Strict limits for testing untrusted code
bazel run //src/server:server -- --sandbox_cpu_time_limit_seconds 3 --sandbox_memory_limit_bytes 52428800
```

## Python Client

```bash
# Execute all C++ examples
python python_client/client.py --directory examples/cpp

# Execute all Python examples  
python python_client/client.py --directory examples/python

# Execute a single file (language auto-detected from extension)
python python_client/client.py --file examples/cpp/01_hello_world.cpp
python python_client/client.py --file examples/python/01_hello_world.py

# Run each file twice to demonstrate caching
python python_client/client.py --directory examples/cpp --cache-demo

# Interactive mode
python python_client/client.py --interactive

# Connect to remote server
python python_client/client.py --server 192.168.1.100:50051 --directory examples/cpp

# Pass stdin data to a program
python python_client/client.py --file examples/cpp/13_stdin_input.cpp --stdin 'DCodeX\n5\n10\n20\n30\n40\n50\n'

# Pass stdin from a file
python python_client/client.py --file examples/cpp/13_stdin_input.cpp --stdin-file /tmp/input.txt
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
- **Output Limiting**: Hard cap on stdout+stderr (default 10KB) prevents flooding
- **Stdin Support**: Pass input data via `--stdin` or `--stdin-file`
- **Real-time Streaming**: gRPC bidirectional streaming for live output
- **Smart Caching**: `absl::Hash`-based LRU cache with 1-hour TTL
- **Language Auto-Detection**: Detected from file extension (`.cpp`, `.py`) or directory name

## Project Structure

```
DCodeX/
├── src/server/           # C++ gRPC server
│   ├── main.cpp          # Server entry point
│   ├── sandbox.cpp/h     # Sandbox execution
│   └── execution_cache.cpp/h  # LRU cache
├── proto/sandbox.proto   # gRPC protocol
├── python_client/client.py    # Python client
├── examples/cpp/         # C++ examples
└── examples/python/      # Python examples
```

## License

Apache License 2.0
