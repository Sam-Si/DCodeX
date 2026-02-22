import grpc
import sys
import os

# Add the current directory to sys.path to find generated modules
sys.path.append(os.path.join(os.path.dirname(__file__), "."))
sys.path.append(os.path.join(os.path.dirname(__file__), ".."))

import proto.sandbox_pb2 as sandbox_pb2
import proto.sandbox_pb2_grpc as sandbox_pb2_grpc


def format_bytes(bytes_val):
    """Convert bytes to human-readable format."""
    if bytes_val < 1024:
        return f"{bytes_val} B"
    elif bytes_val < 1024 * 1024:
        return f"{bytes_val / 1024:.2f} KB"
    elif bytes_val < 1024 * 1024 * 1024:
        return f"{bytes_val / (1024 * 1024):.2f} MB"
    else:
        return f"{bytes_val / (1024 * 1024 * 1024):.2f} GB"


def format_duration(ms):
    """Convert milliseconds to human-readable format."""
    if ms < 1000:
        return f"{ms:.2f} ms"
    elif ms < 60000:
        return f"{ms / 1000:.2f} s"
    else:
        minutes = int(ms / 60000)
        seconds = (ms % 60000) / 1000
        return f"{minutes}m {seconds:.2f}s"


def run():
    # Connect to the server
    channel = grpc.insecure_channel('localhost:50051')
    stub = sandbox_pb2_grpc.CodeExecutorStub(channel)

    # C++ snippet with an infinite loop
    code = """
#include <iostream>
#include <unistd.h>

int main() {
    std::cout << "Starting infinite loop..." << std::endl;
    while(true) {
        // Just loop
    }
    return 0;
}
"""

    request = sandbox_pb2.CodeRequest(language="cpp", code=code)

    print("Sending request to server...")
    print("-" * 50)
    try:
        # Execute the code and stream logs
        responses = stub.Execute(request)
        peak_memory = 0
        execution_time = 0
        cache_hit = False
        
        for response in responses:
            if response.stdout_chunk:
                print(f"STDOUT: {response.stdout_chunk}", end="")
            if response.stderr_chunk:
                print(f"STDERR: {response.stderr_chunk}", end="")
            # Capture resource stats from the final log
            if response.peak_memory_bytes > 0:
                peak_memory = response.peak_memory_bytes
            if response.execution_time_ms > 0:
                execution_time = response.execution_time_ms
            cache_hit = response.cache_hit
        
        print("-" * 50)
        print("📊 Resource Usage Summary:")
        print(f"   💾 Peak Memory: {format_bytes(peak_memory)}")
        print(f"   ⏱️  Execution Time: {format_duration(execution_time)}")
        cache_status = "⚡ CACHE HIT" if cache_hit else "🆕 Fresh Execution"
        print(f"   {cache_status}")
        print("-" * 50)
    except grpc.RpcError as e:
        print(f"RPC failed: {e}")


def run_example_hello():
    """Example: Simple Hello World program."""
    channel = grpc.insecure_channel('localhost:50051')
    stub = sandbox_pb2_grpc.CodeExecutorStub(channel)

    code = """
#include <iostream>

int main() {
    std::cout << "Hello, World!" << std::endl;
    for (int i = 0; i < 5; i++) {
        std::cout << "Count: " << i << std::endl;
    }
    return 0;
}
"""

    request = sandbox_pb2.CodeRequest(language="cpp", code=code)

    print("\n📝 Example: Hello World")
    print("=" * 50)
    try:
        responses = stub.Execute(request)
        peak_memory = 0
        execution_time = 0
        cache_hit = False
        
        for response in responses:
            if response.stdout_chunk:
                print(response.stdout_chunk, end="")
            if response.stderr_chunk:
                print(f"STDERR: {response.stderr_chunk}", end="")
            if response.peak_memory_bytes > 0:
                peak_memory = response.peak_memory_bytes
            if response.execution_time_ms > 0:
                execution_time = response.execution_time_ms
            cache_hit = response.cache_hit
        
        print("-" * 50)
        print("📊 Resource Usage Summary:")
        print(f"   💾 Peak Memory: {format_bytes(peak_memory)}")
        print(f"   ⏱️  Execution Time: {format_duration(execution_time)}")
        cache_status = "⚡ CACHE HIT" if cache_hit else "🆕 Fresh Execution"
        print(f"   {cache_status}")
        print("=" * 50)
    except grpc.RpcError as e:
        print(f"RPC failed: {e}")


def run_example_computation():
    """Example: CPU-intensive computation."""
    channel = grpc.insecure_channel('localhost:50051')
    stub = sandbox_pb2_grpc.CodeExecutorStub(channel)

    code = """
#include <iostream>
#include <cmath>

int main() {
    std::cout << "Starting computation..." << std::endl;
    double result = 0.0;
    for (int i = 1; i <= 1000000; i++) {
        result += std::sqrt(i);
    }
    std::cout << "Computation complete! Result: " << result << std::endl;
    return 0;
}
"""

    request = sandbox_pb2.CodeRequest(language="cpp", code=code)

    print("\n📝 Example: CPU-intensive Computation")
    print("=" * 50)
    try:
        responses = stub.Execute(request)
        peak_memory = 0
        execution_time = 0
        cache_hit = False
        
        for response in responses:
            if response.stdout_chunk:
                print(response.stdout_chunk, end="")
            if response.stderr_chunk:
                print(f"STDERR: {response.stderr_chunk}", end="")
            if response.peak_memory_bytes > 0:
                peak_memory = response.peak_memory_bytes
            if response.execution_time_ms > 0:
                execution_time = response.execution_time_ms
            cache_hit = response.cache_hit
        
        print("-" * 50)
        print("📊 Resource Usage Summary:")
        print(f"   💾 Peak Memory: {format_bytes(peak_memory)}")
        print(f"   ⏱️  Execution Time: {format_duration(execution_time)}")
        cache_status = "⚡ CACHE HIT" if cache_hit else "🆕 Fresh Execution"
        print(f"   {cache_status}")
        print("=" * 50)
    except grpc.RpcError as e:
        print(f"RPC failed: {e}")


def run_example_memory():
    """Example: Memory allocation test."""
    channel = grpc.insecure_channel('localhost:50051')
    stub = sandbox_pb2_grpc.CodeExecutorStub(channel)

    code = """
#include <iostream>
#include <vector>

int main() {
    std::cout << "Allocating memory..." << std::endl;
    std::vector<int> data;
    for (int i = 0; i < 1000000; i++) {
        data.push_back(i);
    }
    std::cout << "Allocated " << data.size() * sizeof(int) << " bytes" << std::endl;
    std::cout << "Memory allocation complete!" << std::endl;
    return 0;
}
"""

    request = sandbox_pb2.CodeRequest(language="cpp", code=code)

    print("\n📝 Example: Memory Allocation Test")
    print("=" * 50)
    try:
        responses = stub.Execute(request)
        peak_memory = 0
        execution_time = 0
        cache_hit = False
        
        for response in responses:
            if response.stdout_chunk:
                print(response.stdout_chunk, end="")
            if response.stderr_chunk:
                print(f"STDERR: {response.stderr_chunk}", end="")
            if response.peak_memory_bytes > 0:
                peak_memory = response.peak_memory_bytes
            if response.execution_time_ms > 0:
                execution_time = response.execution_time_ms
            cache_hit = response.cache_hit
        
        print("-" * 50)
        print("📊 Resource Usage Summary:")
        print(f"   💾 Peak Memory: {format_bytes(peak_memory)}")
        print(f"   ⏱️  Execution Time: {format_duration(execution_time)}")
        cache_status = "⚡ CACHE HIT" if cache_hit else "🆕 Fresh Execution"
        print(f"   {cache_status}")
        print("=" * 50)
    except grpc.RpcError as e:
        print(f"RPC failed: {e}")


if __name__ == '__main__':
    # Run all examples
    run_example_hello()
    run_example_computation()
    run_example_memory()
