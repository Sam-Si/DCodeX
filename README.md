# 🚀 DCodeX

**DCodeX** is a high-performance, gRPC-powered code execution engine designed for secure and scalable sandboxing. Built with C++ and Bazel, it provides a robust infrastructure for executing untrusted code with fine-grained resource control, real-time streaming feedback, and detailed resource usage metrics.

---

## ✨ Key Features

- **🛡️ Secure Sandboxing:** Leverages Linux `rlimit` to enforce strict CPU and memory constraints.
- **⚡ Real-time Streaming:** Uses gRPC bi-directional streaming for instantaneous stdout/stderr feedback.
- **📊 Resource Monitoring:** Tracks peak memory usage and execution time with human-readable formatting.
- **💾 Smart Caching:** SHA256-based result caching eliminates redundant executions for identical code.
- **🛠️ Bazel-Powered:** Reproducible, hermetic builds ensuring consistency across environments.
- **🐍 Python Client:** Easy-to-use client for interacting with the execution engine.
- **📈 Scalable:** Designed with a reactive, multi-threaded architecture to handle concurrent execution requests.

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

### 4. Run the Client

First, install the dependencies and generate the gRPC code:

```bash
# Install dependencies
pip install -r python_client/requirements.txt

# Generate Python gRPC code
python -m grpc_tools.protoc -I. --python_out=. --grpc_python_out=. proto/sandbox.proto
```

Then, execute the sample client:

```bash
python python_client/client.py
```

---

## 📊 Resource Monitoring

DCodeX now provides detailed resource usage metrics for every code execution:

### Metrics Tracked

- **Peak Memory Usage:** Maximum resident set size (RSS) consumed by the process during execution
- **Execution Time:** Total elapsed time from process start to completion

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
==================================================
```

---

## 💾 Smart Caching

DCodeX implements intelligent result caching to avoid redundant code executions:

### How It Works

1. **SHA256 Hashing:** Each code submission is hashed using SHA256
2. **Cache Lookup:** If the hash exists in cache, the stored result is returned immediately
3. **Cache Miss:** New code is executed, and successful results are stored for future use

### Cache Features

- **Thread-Safe:** Uses shared_mutex for concurrent read access
- **LRU Eviction:** Least Recently Used entries are evicted when max capacity is reached
- **TTL Support:** Entries expire after a configurable time (default: 1 hour)
- **Memory Efficient:** Stores stdout, stderr, and resource metrics only

### Cache Configuration

Default settings in `execution_cache.h`:
- **Max Entries:** 1000
- **TTL:** 1 hour
- **Hash Algorithm:** SHA256 (via OpenSSL)

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

- **Run Tests:** `bazel test //...`
- **Clean Build:** `bazel clean --expunge`

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

Distributed under the MIT License. See `LICENSE` for more information.

---

<p align="center">
  Built with ❤️ by the DCodeX Team
</p>