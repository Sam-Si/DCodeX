# 🚀 DCodeX

**DCodeX** is a high-performance, gRPC-powered code execution engine designed for secure and scalable sandboxing. Built with C++ and Bazel, it provides a robust infrastructure for executing untrusted code with fine-grained resource control, real-time streaming feedback, and detailed resource usage metrics.

---

## ✨ Key Features

- **🛡️ Secure Sandboxing:** Leverages Linux `rlimit` to enforce strict CPU and memory constraints.
- **⏰ Wall-Clock Timeout:** A dedicated watcher process terminates any sandboxed program that exceeds the configured
  real-time limit (default: 5 seconds), catching programs that sleep, block on I/O, or are otherwise hung — scenarios
  that CPU-time limits alone cannot handle.
- **✂️ Output Size Limiting:** Enforces a hard cap (default: 10 KB) on combined stdout+stderr output. When the limit is
  hit the child is killed immediately, a truncation notice is appended, and the `output_truncated` flag is set —
  preventing runaway programs from flooding the system.
- **⌨️ Stdin Support:** Pass arbitrary input data to the program's standard input via the `stdin_data` field in
  `CodeRequest`. The cache key covers both source code and stdin, so the same program with different inputs is cached
  independently.
- **⚡ Real-time Streaming:** Uses gRPC bi-directional streaming for instantaneous stdout/stderr feedback.
- **📊 Resource Monitoring:** Tracks peak memory usage and execution time with human-readable formatting.
- **💾 Smart Caching:** FNV-1a hash-based result caching eliminates redundant executions for identical code.
- **🛠️ Bazel-Powered:** Reproducible, hermetic builds ensuring consistency across environments.
- **🐍 Python Client:** Easy-to-use client with full type hints and flexible execution modes.
- **📈 Scalable:** Designed with a reactive, multi-threaded architecture to handle concurrent execution requests.
- **📁 File-based Execution:** Execute C++ code from files or directories instead of hardcoded strings.

---

## 🏗️ Architecture

DCodeX consists of two main components:

1.  **The Server (C++):** A gRPC server that manages a pool of sandboxed processes. It handles code compilation, execution, and resource monitoring using `getrusage()` for accurate metrics.
2.  **The Client (Python):** A reference implementation demonstrating how to stream code execution requests and receive live updates, including resource usage summaries.

---

## 🚦 Getting Started

### Prerequisites

- **Bazel:** [Install Bazel](https://bazel.build/install) (The recommended way is via `bazelisk`).
- **Python 3.8+**: For running the client.
- **g++**: Required for the server to compile the sandboxed code.

### 1. Clone the Repository

```bash
git clone https://github.com/yourusername/DCodeX.git
cd DCodeX
```

### 2. Build the Server

```bash
bazel build //src/server:server
```

### 3. Run the Server

```bash
bazel run //src/server:server
```
The server will start listening on `0.0.0.0:50051`.

### 4. Install Python Client Dependencies

```bash
# Install dependencies
pip install -r python_client/requirements.txt

# Generate Python gRPC code
python -m grpc_tools.protoc -I. --python_out=. --grpc_python_out=. proto/sandbox.proto
```

### 5. Run the Python Client

The Python client supports multiple execution modes:

#### Default Mode - Run All Examples
```bash
python python_client/client.py
```
This executes all examples from the `examples/cpp` directory with cache demonstration.

#### Execute from a Directory
```bash
python python_client/client.py --directory /path/to/cpp/files
```
Execute all `.cpp` files from a specified directory.

```bash
python python_client/client.py --directory /path/to/python/files --language python
```
Execute all `.py` files from a specified directory.

#### Execute a Specific File
```bash
python python_client/client.py --file /path/to/program.cpp
```
Execute a single C++ file.

```bash
python python_client/client.py --file /path/to/program.py
```
Execute a single Python file (language auto-detected by extension).

#### Interactive Mode
```bash
python python_client/client.py --interactive
```
Run in interactive mode with a menu to choose execution options.

#### Cache Demonstration
```bash
python python_client/client.py --cache-demo
# or with a specific directory
python python_client/client.py --directory /path/to/cpp/files --cache-demo
```
Run each program twice to demonstrate caching (first run = fresh execution, second run = cache hit).

#### Specify Server Address
```bash
python python_client/client.py --server localhost:50051
```

---

## 🐍 Python Client Options

### Command-Line Arguments

| Option | Short | Description | Default |
|--------|-------|-------------|---------|
| `--server` | | Server address | `localhost:50051` |
| `--interactive` | `-i` | Run in interactive mode | `False` |
| `--directory` | `-d` | Directory containing code files (use `--language` to pick `.cpp` or `.py`) | `examples/cpp` |
| `--file` | `-f` | Specific code file to execute | None |
| `--language` | `-l` | Override language detection for `--file`/`--directory` (`cpp` or `python`) | Auto-detect by extension for `--file`, required for `--directory` with Python |
| `--cache-demo` | `-c` | Run files twice to show caching | `False` |

### Examples

```bash
# Run all default examples with cache demo
python python_client/client.py --cache-demo

# Execute all files from a directory
python python_client/client.py --directory ./my_cpp_programs

# Execute all Python files from a directory
python python_client/client.py --directory ./my_python_programs --language python

# Execute a single file
python python_client/client.py --file ./test.cpp

# Execute a single Python file
python python_client/client.py --file ./test.py

# Interactive mode
python python_client/client.py --interactive

# Connect to remote server
python python_client/client.py --server 192.168.1.100:50051 --directory ./programs
```

### Creating Custom Examples

Create a directory with `.cpp` files and run:

```bash
mkdir my_programs
cat > my_programs/hello.cpp << 'EOF'
#include <iostream>
int main() {
    std::cout << "Hello from custom program!" << std::endl;
    return 0;
}
EOF

python python_client/client.py --directory my_programs
```

---

## 📁 Examples

The `examples/` directory contains sample programs organized by language:

### C++ Examples

| File                        | Description                   | Key Concepts                        |
|-----------------------------|-------------------------------|-------------------------------------|
| `01_hello_world.cpp`        | Basic Hello World program     | I/O, loops                          |
| `02_basic_math.cpp`         | Math operations demonstration | Arithmetic, cmath functions         |
| `03_fibonacci.cpp`          | Fibonacci sequence generator  | Loops, algorithms                   |
| `04_prime_numbers.cpp`      | Prime number finder           | Algorithms, math                    |
| `05_factorial.cpp`          | Factorial calculator          | Functions, recursion alternative    |
| `06_arrays_and_vectors.cpp` | Array and vector operations   | STL containers, algorithms          |
| `07_strings.cpp`            | String manipulation           | std::string, algorithms             |
| `08_memory_allocation.cpp`  | Memory management demo        | new/delete, smart pointers, vectors |
| `09_cpu_intensive.cpp`      | CPU-intensive computation     | Heavy calculations, chrono          |
| `10_sandbox_safe.cpp`       | Sandbox-safe program          | Resource-conscious coding           |
| `11_infinite_loop.cpp`      | Infinite loop example         | Timeout demonstration               |
| `12_memory_exhaustion.cpp`  | Memory exhaustion test        | Resource limit demo                 |
| `13_stdin_input.cpp`        | Stdin input demo              | std::cin, std::getline, statistics  |
| `14_output_flood.cpp`       | Output size limit demo        | while(true), output truncation      |

### Python Examples

| File                         | Description            | Key Concepts                              |
|------------------------------|------------------------|-------------------------------------------|
| `01_hello_world.py`          | Basic Hello World      | Print, system info, datetime              |
| `02_basic_math.py`           | Math operations        | math, random, statistics                  |
| `03_file_operations.py`      | File I/O demo          | pathlib, json, tempfile                   |
| `04_data_structures.py`      | Data structures        | lists, dicts, sets, collections           |
| `05_iterators_generators.py` | Iterators & generators | yield, itertools                          |
| `06_infinite_loop.py`        | Infinite loop example  | Timeout demonstration                     |
| `07_memory_exhaustion.py`    | Memory exhaustion test | Resource limit demo                       |
| `08_slow_computation.py`     | Slow computation       | CPU time limit demo                       |
| `09_output_flood.py`         | Output size limit demo | while True, flush=True, output truncation |

### Running Examples

```bash
# Run all C++ examples
python python_client/client.py --directory examples/cpp

# Run with cache demonstration
python python_client/client.py --directory examples/cpp --cache-demo

# Run a specific example
python python_client/client.py --file examples/cpp/01_hello_world.cpp

# Run a Python example via the server
python python_client/client.py --file examples/python/01_hello_world.py

# Run all Python examples via the server
python python_client/client.py --directory examples/python --language python

# Demonstrate the 10 KB output size limit (C++)
# Expected: ~20 lines of output then truncation notice + ✂️ OUTPUT TRUNCATED flag
python python_client/client.py --file examples/cpp/14_output_flood.cpp

# Demonstrate the 10 KB output size limit (Python)
# Expected: same behaviour — sandbox kills the process and flags output_truncated
python python_client/client.py --file examples/python/09_output_flood.py
```

---

## 📊 Resource Monitoring

DCodeX provides detailed resource usage metrics for every code execution:

### Metrics Tracked

- **Peak Memory Usage:** Maximum resident set size (RSS) consumed by the process during execution
- **Execution Time:** Total elapsed time from process start to completion
- **User CPU Time:** Time spent in user mode
- **System CPU Time:** Time spent in kernel mode

### Human-Readable Format

The Python client automatically formats resource metrics for easy reading:

- **Memory:** Bytes → KB → MB → GB (e.g., "2.45 MB")
- **Time:** Milliseconds → Seconds → Minutes (e.g., "1.23 s" or "2m 15.50s")

### Example Output

```
📝 Example: CPU-intensive Computation
==================================================
Starting computation...
Computation complete! Result: 6.66667e+08
--------------------------------------------------
📊 Resource Usage Summary:
   💾 Peak Memory: 2.15 MB
   ⏱️  Execution Time: 234.56 ms
   🌐 Network Time: 245.32 ms
   ⚡ CACHE HIT
==================================================
```

---

## ⏰ Wall-Clock Timeout

DCodeX enforces a **wall-clock (real-time) timeout** on every sandboxed execution in addition to the CPU-time limit
provided by `RLIMIT_CPU`.

### Why Wall-Clock Timeout?

CPU-time limits (`RLIMIT_CPU`) only count time the process spends on the CPU. A program that calls `sleep()`, blocks on
I/O, or is otherwise hung consumes very little CPU time and will **never** be killed by `RLIMIT_CPU` alone. Wall-clock
timeout measures actual elapsed real time, so it catches:

- Programs with `sleep()` / `usleep()` / `std::this_thread::sleep_for()` calls
- Programs blocked waiting for I/O that never arrives
- Programs that are genuinely hung (e.g. deadlocked)
- Any other scenario where the process is not making progress

### How It Works

1. After forking the sandboxed child process, the parent forks a second **watcher process**.
2. The watcher sleeps for `kWallClockTimeoutSeconds` (default: **5 seconds**) using `sleep()`.
3. If the child is still running after the timeout, the watcher sends `SIGKILL` to the child and exits.
4. The parent's output-draining loop uses a short `select()` timeout (100 ms) so it unblocks promptly once the child's
   pipes are closed (which happens as soon as the child is killed).
5. The parent reaps both processes with `wait4()` to collect accurate resource usage.
6. A `wall_clock_timeout = true` flag is set in the result and propagated to the gRPC client.

### Configuration

The timeout is controlled by the constant `kWallClockTimeoutSeconds` in `src/server/sandbox.cpp`:

```cpp
// Wall-clock timeout in seconds for sandboxed execution.
constexpr int kWallClockTimeoutSeconds = 5;
```

### Client Output

When a wall-clock timeout occurs the Python client shows:

```
STDERR: Wall-clock timeout exceeded (5 seconds)
--------------------------------------------------
📊 Resource Usage Summary:
   💾 Peak Memory: 1.23 MB
   ⏱️  Execution Time: 5001 ms
   🌐 Network Time: 5023 ms
   🆕 Fresh Execution
   ⏰ WALL-CLOCK TIMEOUT (process killed by sandbox)
==================================================
```

---

## ✂️ Output Size Limiting

DCodeX enforces a **hard cap on the total output** (stdout + stderr combined) produced by every sandboxed execution.

### Why Output Limiting?

A program with no output limit can flood the server and the client with arbitrarily large amounts of data. Classic
examples:

- `while (true) { std::cout << <long essay pasted from the internet>; }` — fills the pipe buffer in milliseconds and
  keeps going until the wall-clock timeout fires, potentially buffering gigabytes in memory.
- A program that opens `/dev/urandom` and copies it to stdout.
- A bug that accidentally prints a multi-million-element container.

Without a limit, the server must buffer all output before streaming it, the client must receive all of it, and the
execution cache would store it permanently. The output cap stops this as soon as the threshold is crossed, regardless of
whether the wall-clock timeout has fired yet.

### How It Works

1. `ReadProcessOutput` in `sandbox.cpp` maintains a running counter of bytes received across **both** stdout and stderr.
2. After every `read()` call the counter is checked against `kMaxOutputBytes`.
3. When the limit is exceeded the server:
    - Sends `SIGKILL` to the child process immediately.
    - Appends a human-readable notice to the stderr stream:
      ```
      [Output truncated: combined stdout+stderr exceeded 10 KB limit]
      ```
    - Breaks out of the output-draining loop.
4. The child is reaped normally by `WaitWithTimeout` / `wait4`.
5. `result.output_truncated = true` is set and propagated through the gRPC `ExecutionLog` as `output_truncated = 7`.

### Configuration

The limit is controlled by the constant `kMaxOutputBytes` in `src/server/sandbox.h`:

```cpp
// Maximum combined stdout+stderr output in bytes before the process is
// killed and the output is truncated.  10 KB is small enough for demos
// while still protecting against runaway printers.
static constexpr size_t kMaxOutputBytes = 10 * 1024;  // 10 KB
```

### Client Output

When output is truncated the Python client shows the truncation notice inline (streamed as a stderr chunk) and a summary
flag:

```
...first 10 KB of output...
STDERR: 
[Output truncated: combined stdout+stderr exceeded 10 KB limit]
--------------------------------------------------
📊 Resource Usage Summary:
   💾 Peak Memory: 4.00 MB
   ⏱️  Execution Time: 312.00 ms
   🌐 Network Time: 318.45 ms
   🆕 Fresh Execution
   ✂️  OUTPUT TRUNCATED (exceeded 10 KB combined output limit)
==================================================
```

### Proto Field

```protobuf
message ExecutionLog {
  // ...
  bool output_truncated = 7;  // true when output exceeded kMaxOutputBytes
}
```

---

## ⌨️ Stdin Support

DCodeX allows you to supply arbitrary data to a program's **standard input** (stdin) as part of every execution request.

### How It Works

The `stdin_data` field in `CodeRequest` (proto field 3) is piped directly into the sandboxed process's `STDIN_FILENO`
before execution begins. After all bytes are written the write-end of the pipe is closed, so the program receives a
clean EOF when it has consumed all input — exactly as if a user had typed the data and pressed Ctrl-D.

The cache key is derived from **both the source code and the stdin data** (concatenated with a separator before
hashing), so the same program submitted with different stdin values is stored and retrieved independently.

### Python Client Usage

Pass stdin inline using `--stdin` (use `\n` for newlines):

```bash
# Run the stdin example with inline data
python python_client/client.py \
    --file examples/cpp/13_stdin_input.cpp \
    --stdin 'DCodeX\n5\n10\n20\n30\n40\n50\n'
```

Pass stdin from a file using `--stdin-file` (takes precedence over `--stdin`):

```bash
# Create an input file
cat > /tmp/my_input.txt << 'EOF'
DCodeX
5
10
20
30
40
50
EOF

python python_client/client.py \
    --file examples/cpp/13_stdin_input.cpp \
    --stdin-file /tmp/my_input.txt
```

### Expected Output for `13_stdin_input.cpp`

```
Hello, DCodeX!
=========================
You provided 5 number(s):
  [1] 10
  [2] 20
  [3] 30
  [4] 40
  [5] 50

Statistics:
  Sum     : 150
  Minimum : 10
  Maximum : 50
  Average : 30
```

### Programmatic Usage (Python)

```python
result = execute_code(stub, code, stdin_data="Alice\n3\n7\n14\n21\n")
```

### Proto Field

```protobuf
message CodeRequest {
  string language   = 1;
  string code       = 2;
  string stdin_data = 3;  // optional; empty = EOF immediately
}
```

---

## 💾 Smart Caching

DCodeX implements intelligent result caching to avoid redundant code executions:

### How It Works

1. **FNV-1a Hashing:** Each code submission is hashed using FNV-1a algorithm
2. **Cache Lookup:** If the hash exists in cache, the stored result is returned immediately
3. **Cache Miss:** New code is executed, and successful results are stored for future use

### Cache Features

- **Thread-Safe:** Uses `shared_mutex` for concurrent read access
- **LRU Eviction:** Least Recently Used entries are evicted when max capacity is reached
- **TTL Support:** Entries expire after a configurable time (default: 1 hour)
- **Memory Efficient:** Stores stdout, stderr, and resource metrics only

### Cache Configuration

Default settings in `execution_cache.h`:
- **Max Entries:** 1000
- **TTL:** 1 hour
- **Hash Algorithm:** FNV-1a (64-bit)

### Cache Indicators

The Python client shows cache status in the output:

```
📊 Resource Usage Summary:
   💾 Peak Memory: 2.15 MB
   ⏱️  Execution Time: 234.56 ms
   ⚡ CACHE HIT
```

Or for fresh executions:

```
📊 Resource Usage Summary:
   💾 Peak Memory: 2.15 MB
   ⏱️  Execution Time: 234.56 ms
   🆕 Fresh Execution
```

### Programmatic Access

```cpp
// Clear the cache
SandboxedProcess::ClearCache();

// Access cache statistics
size_t cache_size = SandboxedProcess::GetCache().Size();
```

---

## 🛠️ Development & Testing

DCodeX uses Bazel for its entire lifecycle.

### C++ Code Quality Tools

Configuration files are provided for code quality tools:

```bash
# Apply clang-format to all source files
clang-format -i src/server/*.cpp src/server/*.h

# Run clang-tidy for static analysis
clang-tidy src/server/main.cpp src/server/sandbox.cpp src/server/execution_cache.cpp
```

### Python Type Checking

```bash
# Run pyright for type checking
pyright python_client/client.py

# Or use mypy
mypy python_client/client.py
```

### Build Commands

- **Build Server:** `bazel build //src/server:server`
- **Run Tests:** `bazel test //...`
- **Clean Build:** `bazel clean --expunge`

---

## 🏗️ Project Structure

```
DCodeX/
├── src/server/           # C++ server source code
│   ├── main.cpp         # gRPC server implementation
│   ├── sandbox.h/.cpp   # Sandboxed process execution
│   └── execution_cache.h/.cpp  # Result caching
├── proto/               # Protocol Buffers definitions
│   └── sandbox.proto
├── python_client/       # Python client implementation
│   ├── client.py
│   └── requirements.txt
├── examples/            # Example programs
│   ├── cpp/            # C++ examples
│   └── python/         # Python examples
├── MODULE.bazel        # Bazel module configuration
├── .clang-format       # clang-format configuration
├── .clang-tidy         # clang-tidy configuration
└── README.md           # This file
```

---

## 🤝 Contributing

We welcome contributions! Whether it's adding support for more languages, improving the sandboxing mechanism, or enhancing the documentation, your help is appreciated.

1. Fork the project.
2. Create your feature branch (`git checkout -b feature/AmazingFeature`).
3. Commit your changes (`git commit -m 'feat: add some AmazingFeature'`).
4. Push to the branch (`git push origin feature/AmazingFeature`).
5. Open a Pull Request.

---

## 📜 License

Distributed under the Apache License 2.0. See `LICENSE` for more information.

---

<p align="center">
  Built with ❤️ by the DCodeX Team
</p>
