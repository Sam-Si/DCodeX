# 🚀 DCodeX

**DCodeX** is a high-performance, gRPC-powered code execution engine designed for secure and scalable sandboxing. Built with C++ and Bazel, it provides a robust infrastructure for executing untrusted code with fine-grained resource control, real-time streaming feedback, and detailed resource usage metrics.

---

## ✨ Key Features

- **🛡️ Secure Sandboxing:** Leverages Linux `rlimit` to enforce strict CPU and memory constraints.
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

#### Execute a Specific File
```bash
python python_client/client.py --file /path/to/program.cpp
```
Execute a single C++ file.

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
| `--directory` | `-d` | Directory containing `.cpp` files | `examples/cpp` |
| `--file` | `-f` | Specific `.cpp` file to execute | None |
| `--cache-demo` | `-c` | Run files twice to show caching | `False` |

### Examples

```bash
# Run all default examples with cache demo
python python_client/client.py --cache-demo

# Execute all files from a directory
python python_client/client.py --directory ./my_cpp_programs

# Execute a single file
python python_client/client.py --file ./test.cpp

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

| File | Description | Key Concepts |
|------|-------------|--------------|
| `01_hello_world.cpp` | Basic Hello World program | I/O, loops |
| `02_basic_math.cpp` | Math operations demonstration | Arithmetic, cmath functions |
| `03_fibonacci.cpp` | Fibonacci sequence generator | Loops, algorithms |
| `04_prime_numbers.cpp` | Prime number finder | Algorithms, math |
| `05_factorial.cpp` | Factorial calculator | Functions, recursion alternative |
| `06_arrays_and_vectors.cpp` | Array and vector operations | STL containers, algorithms |
| `07_strings.cpp` | String manipulation | std::string, algorithms |
| `08_memory_allocation.cpp` | Memory management demo | new/delete, smart pointers, vectors |
| `09_cpu_intensive.cpp` | CPU-intensive computation | Heavy calculations, chrono |
| `10_sandbox_safe.cpp` | Sandbox-safe program | Resource-conscious coding |
| `11_infinite_loop.cpp` | Infinite loop example | Timeout demonstration |
| `12_memory_exhaustion.cpp` | Memory exhaustion test | Resource limit demo |

### Python Examples

| File | Description | Key Concepts |
|------|-------------|--------------|
| `01_hello_world.py` | Basic Hello World | Print, system info, datetime |
| `02_basic_math.py` | Math operations | math, random, statistics |
| `03_file_operations.py` | File I/O demo | pathlib, json, tempfile |
| `04_data_structures.py` | Data structures | lists, dicts, sets, collections |
| `05_iterators_generators.py` | Iterators & generators | yield, itertools |
| `06_infinite_loop.py` | Infinite loop example | Timeout demonstration |
| `07_memory_exhaustion.py` | Memory exhaustion test | Resource limit demo |
| `08_slow_computation.py` | Slow computation | CPU time limit demo |

### Running Examples

```bash
# Run all C++ examples
python python_client/client.py --directory examples/cpp

# Run with cache demonstration
python python_client/client.py --directory examples/cpp --cache-demo

# Run a specific example
python python_client/client.py --file examples/cpp/01_hello_world.cpp

# Run Python examples directly
cd examples/python
python 01_hello_world.py
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
