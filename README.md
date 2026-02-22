# 🚀 DCodeX

**DCodeX** is a high-performance, gRPC-powered code execution engine designed for secure and scalable sandboxing. Built with C++ and Bazel, it provides a robust infrastructure for executing untrusted code with fine-grained resource control and real-time streaming feedback.

---

## ✨ Key Features

- **🛡️ Secure Sandboxing:** Leverages Linux `rlimit` to enforce strict CPU and memory constraints.
- **⚡ Real-time Streaming:** Uses gRPC bi-directional streaming for instantaneous stdout/stderr feedback.
- **🛠️ Bazel-Powered:** Reproducible, hermetic builds ensuring consistency across environments.
- **🐍 Python Client:** Easy-to-use client for interacting with the execution engine.
- **📈 Scalable:** Designed with a reactive, multi-threaded architecture to handle concurrent execution requests.

---

## 🏗️ Architecture

DCodeX consists of two main components:

1.  **The Server (C++):** A gRPC server that manages a pool of sandboxed processes. It handles code compilation, execution, and resource monitoring.
2.  **The Client (Python):** A reference implementation demonstrating how to stream code execution requests and receive live updates.

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