# DCodeX

A high-performance, gRPC-powered code execution engine featuring production-grade sandboxing, real-time bidirectional streaming, and an intelligent dynamic worker coordinator.

[![C++](https://img.shields.io/badge/C++-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Bazel](https://img.shields.io/badge/Build-Bazel-green.svg)](https://bazel.build/)
[![License](https://img.shields.io/badge/License-Apache%202.0-orange.svg)](LICENSE)

## 🚀 Quick Start

```bash
# Build the production server
bazel build //src/api:server

# Run the server (default: localhost:50051)
bazel run //src/api:server

# Install Python client dependencies
pip install -r python_client/requirements.txt

# Run the client
python python_client/main.py --file examples/cpp/03_fibonacci.cpp
```

## 🚀 Performance & Platform Optimization

DCodeX is optimized for maximum build and test speed out of the box.

### Default: High-Performance Linux
By default, DCodeX assumes a high-performance Linux environment and optimizes for speed:
- **Parallelism**: Defaults to `--jobs 20` and `--linkopt=-Wl,--threads=16`.
- **Resources**: Pre-allocates 16 CPUs and 56GB RAM for the build/test execution.
- **Fast Linking**: Uses `lld` (the LLVM linker) on Linux for near-instant link times.

### macOS (M1/M2/M3)
On macOS, DCodeX automatically detects the Silicon architecture and tunes itself:
- **Optimization**: Uses `--cpu=darwin_arm64` and `--macos_cpus=arm64`.
- **Resource Guardrails**: Throttles to 8 CPUs and 16GB RAM to maintain UI responsiveness while building.

```bash
# Explicitly use the high-performance Linux config (if not auto-detected)
bazel build --config=linux //src/api:server

# Explicitly use the macOS Silicon config
bazel build --config=macos //src/api:server
```

## 🏗️ Core Architecture: Dynamic Worker Coordinator

DCodeX features a sophisticated **Dynamic Worker Coordinator** that replaces static pools with a reactive, language-aware concurrency model.

### Key Capabilities

- **Language Affinity**: Pre-boots sandboxes with specialized runtimes (C++, Python) based on request patterns, reducing cold-start latency by up to 80%.
- **Dynamic Scaling**: A background `PoolBalancer` thread monitors request latency and queue depth, automatically scaling the worker pool between `min_workers` and `max_workers`.
- **Asynchronous Recycling**: After execution, workers enter a background `RECYCLING` state where temp files are wiped and namespaces are sanitized without blocking the main execution path.
- **Fair Queueing**: Implements a language-weighted fair-queueing mechanism to prevent starvation during high-load bursts of a single language type.

### Worker State Machine

```mermaid
stateDiagram-v2
    [*] --> IDLE
    IDLE --> BUSY: LeaseWorker()
    BUSY --> RECYCLING: Task Complete
    RECYCLING --> IDLE: Sanitization Done
    IDLE --> SHUTDOWN: Scale Down / Shutdown
    BUSY --> SHUTDOWN: Shutdown
    RECYCLING --> SHUTDOWN: Shutdown
    SHUTDOWN --> [*]
```

## 🛠️ Server Configuration

| Flag | Default | Description |
|------|---------|-------------|
| `--port` | 50051 | gRPC server port |
| `--min_workers` | 2 | Minimum pre-booted workers |
| `--max_workers` | 20 | Maximum concurrent sandboxes |
| `--scale_up_latency_ms` | 100 | Latency threshold to trigger scaling |
| `--sandbox_cpu_limit` | 1s | CPU time limit per execution |
| `--sandbox_memory_limit` | 4GB | Memory limit per execution |

```bash
# Example: High-concurrency cluster configuration
bazel run //src/api:server -- --max_workers 64 --min_workers 8 --scale_up_latency_ms 50
```

## 📦 Project Structure

```text
DCodeX/
├── src/api/                  # gRPC Interface Layer
│   ├── execute_reactor.cpp   # Bidirectional stream handling
│   └── code_executor_service # Service lifecycle management
├── src/engine/               # Core Execution Engine
│   ├── dynamic_worker_coordinator # Intelligent resource orchestration
│   ├── sandbox.cpp           # SandboxedProcess implementation
│   ├── process_runner.cpp    # RAII-based process management
│   └── execution_pipeline.cpp # Command-pattern execution flow
├── src/common/               # Utilities & Caching
│   └── execution_cache.cpp   # LRU cache with TTL
├── proto/                    # Protocol Definitions
└── python_client/            # Reference Client Implementation
```

## 🧪 Testing & Reliability

DCodeX is built with a focus on concurrency safety and memory stability.

### Sanitizer Suite

DCodeX uses platform-aware sanitizers to ensure a race-free and leak-free codebase. The configuration automatically handles platform differences (e.g., disabling leak detection on macOS where it is unsupported).

```bash
# Run tests with ThreadSanitizer (TSan) - detects races and deadlocks
# Runs each test 20 times to shake out non-deterministic concurrency bugs.
bazel test --config=tsan //...

# Run tests with AddressSanitizer (ASan) - detects memory leaks and corruption
# Automatically selects 'asan:linux' or 'asan:macos' based on your host.
bazel test --config=asan //...
```

### Best Practices

- **Abseil Integration**: Uses `absl::Mutex` and `absl::CondVar` for robust synchronization.
- **RAII Everything**: Resource acquisition is initialization for processes, files, and threads.
- **Lock Ordering**: Strict lock hierarchy to prevent deadlocks in the balancer and worker loops.

## 📜 License

Distributed under the Apache License 2.0. See `LICENSE` for more information.
