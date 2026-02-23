import grpc
import sys
import os
import time

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


def execute_code(stub, code, description=""):
    """Execute code and return results with timing."""
    request = sandbox_pb2.CodeRequest(language="cpp", code=code)
    
    start_time = time.time()
    responses = stub.Execute(request)
    
    peak_memory = 0
    execution_time = 0
    cache_hit = False
    stdout_output = []
    stderr_output = []
    
    for response in responses:
        if response.stdout_chunk:
            stdout_output.append(response.stdout_chunk)
        if response.stderr_chunk:
            stderr_output.append(response.stderr_chunk)
        if response.peak_memory_bytes > 0:
            peak_memory = response.peak_memory_bytes
        if response.execution_time_ms > 0:
            execution_time = response.execution_time_ms
        cache_hit = response.cache_hit
    
    actual_time = (time.time() - start_time) * 1000  # Convert to ms
    
    return {
        'stdout': ''.join(stdout_output),
        'stderr': ''.join(stderr_output),
        'peak_memory': peak_memory,
        'execution_time': execution_time,
        'cache_hit': cache_hit,
        'actual_time': actual_time
    }


def print_results(results, show_output=True):
    """Print execution results in a formatted way."""
    if show_output and results['stdout']:
        print(results['stdout'], end='')
    if show_output and results['stderr']:
        print(f"STDERR: {results['stderr']}", end='')
    
    print("-" * 50)
    print("📊 Resource Usage Summary:")
    print(f"   💾 Peak Memory: {format_bytes(results['peak_memory'])}")
    print(f"   ⏱️  Execution Time: {format_duration(results['execution_time'])}")
    print(f"   🌐 Network Time: {format_duration(results['actual_time'])}")
    cache_status = "⚡ CACHE HIT" if results['cache_hit'] else "🆕 Fresh Execution"
    print(f"   {cache_status}")
    print("=" * 50)


def run_example_hello():
    """Example 1: Simple Hello World program."""
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

    print("\n📝 Example 1: Hello World")
    print("=" * 50)
    results = execute_code(stub, code)
    print_results(results)


def run_example_cache_demo():
    """Example 2: Demonstrate caching by running same code twice."""
    channel = grpc.insecure_channel('localhost:50051')
    stub = sandbox_pb2_grpc.CodeExecutorStub(channel)

    code = """
#include <iostream>
#include <cmath>

int main() {
    std::cout << "Computing fibonacci sequence..." << std::endl;
    long long a = 0, b = 1;
    std::cout << "Fibonacci: " << a;
    for (int i = 0; i < 40; i++) {
        std::cout << ", " << b;
        long long temp = a + b;
        a = b;
        b = temp;
    }
    std::cout << std::endl;
    return 0;
}
"""

    print("\n📝 Example 2: Cache Demonstration")
    print("=" * 60)
    print("This example runs the SAME code twice to demonstrate caching.")
    print("First run will be fresh execution, second should be cache hit.")
    print("=" * 60)
    
    # First execution - fresh
    print("\n🔴 FIRST RUN (Expected: Fresh Execution)")
    print("-" * 60)
    results1 = execute_code(stub, code, "First run")
    print_results(results1)
    
    # Small delay to make it clear these are separate requests
    time.sleep(0.5)
    
    # Second execution - should be cached
    print("\n🟢 SECOND RUN (Expected: Cache Hit)")
    print("-" * 60)
    results2 = execute_code(stub, code, "Second run - cached")
    print_results(results2)
    
    # Compare results
    print("\n📈 Cache Performance Comparison:")
    print("-" * 60)
    print(f"   First Run Network Time:  {format_duration(results1['actual_time'])}")
    print(f"   Second Run Network Time: {format_duration(results2['actual_time'])}")
    if results2['cache_hit']:
        speedup = results1['actual_time'] / max(results2['actual_time'], 1)
        print(f"   ⚡ Speedup: {speedup:.2f}x faster (cache hit)")
    print("=" * 60)


def run_example_multiple_cache():
    """Example 3: Multiple different programs, then repeat to show selective caching."""
    channel = grpc.insecure_channel('localhost:50051')
    stub = sandbox_pb2_grpc.CodeExecutorStub(channel)

    programs = [
        ("Program A: Squares", """
#include <iostream>
int main() {
    std::cout << "Squares of 1-10:" << std::endl;
    for (int i = 1; i <= 10; i++) {
        std::cout << i << "² = " << (i * i) << std::endl;
    }
    return 0;
}
"""),
        ("Program B: Cubes", """
#include <iostream>
int main() {
    std::cout << "Cubes of 1-10:" << std::endl;
    for (int i = 1; i <= 10; i++) {
        std::cout << i << "³ = " << (i * i * i) << std::endl;
    }
    return 0;
}
"""),
    ]

    print("\n📝 Example 3: Multiple Programs with Selective Caching")
    print("=" * 60)
    print("Running two DIFFERENT programs...")
    print("=" * 60)
    
    # First run - both should be fresh
    for name, code in programs:
        print(f"\n🔴 {name} - First Run")
        print("-" * 60)
        results = execute_code(stub, code, name)
        print_results(results)
    
    time.sleep(0.5)
    
    print("\n" + "=" * 60)
    print("Running the SAME two programs again...")
    print("Both should now be CACHE HITS")
    print("=" * 60)
    
    # Second run - both should be cached
    for name, code in programs:
        print(f"\n🟢 {name} - Second Run")
        print("-" * 60)
        results = execute_code(stub, code, name + " - cached")
        print_results(results)


def run_example_computation():
    """Example 4: CPU-intensive computation."""
    channel = grpc.insecure_channel('localhost:50051')
    stub = sandbox_pb2_grpc.CodeExecutorStub(channel)

    code = """
#include <iostream>
#include <cmath>

int main() {
    std::cout << "Starting heavy computation..." << std::endl;
    double result = 0.0;
    for (int i = 1; i <= 5000000; i++) {
        result += std::sqrt(static_cast<double>(i));
    }
    std::cout << "Sum of square roots: " << result << std::endl;
    return 0;
}
"""

    print("\n📝 Example 4: CPU-intensive Computation")
    print("=" * 60)
    print("This heavy computation will be cached for instant replay!")
    print("=" * 60)
    
    # First run
    print("\n🔴 First Run (Heavy computation - takes time)")
    print("-" * 60)
    results1 = execute_code(stub, code)
    print_results(results1)
    
    time.sleep(0.5)
    
    # Second run - should be instant
    print("\n🟢 Second Run (Should be instant from cache)")
    print("-" * 60)
    results2 = execute_code(stub, code)
    print_results(results2)


def run_example_memory():
    """Example 5: Memory allocation test."""
    channel = grpc.insecure_channel('localhost:50051')
    stub = sandbox_pb2_grpc.CodeExecutorStub(channel)

    code = """
#include <iostream>
#include <vector>

int main() {
    std::cout << "Testing memory allocation..." << std::endl;
    std::vector<int> data;
    const int count = 2000000;
    for (int i = 0; i < count; i++) {
        data.push_back(i);
    }
    std::cout << "Allocated " << (data.size() * sizeof(int) / (1024*1024)) << " MB" << std::endl;
    std::cout << "Memory test complete!" << std::endl;
    return 0;
}
"""

    print("\n📝 Example 5: Memory Allocation Test with Caching")
    print("=" * 60)
    
    # First run
    print("\n🔴 First Run")
    print("-" * 60)
    results1 = execute_code(stub, code)
    print_results(results1)
    
    time.sleep(0.5)
    
    # Second run - cached
    print("\n🟢 Second Run (From Cache)")
    print("-" * 60)
    results2 = execute_code(stub, code)
    print_results(results2)


def run_example_sandbox_limits():
    """Example 6: Demonstrate sandbox resource limits."""
    channel = grpc.insecure_channel('localhost:50051')
    stub = sandbox_pb2_grpc.CodeExecutorStub(channel)

    # This should work fine within limits
    code_safe = """
#include <iostream>
#include <vector>

int main() {
    std::cout << "Running within sandbox limits..." << std::endl;
    std::vector<int> data(10000);
    for (size_t i = 0; i < data.size(); i++) {
        data[i] = i * i;
    }
    std::cout << "Processed " << data.size() << " elements safely." << std::endl;
    return 0;
}
"""

    print("\n📝 Example 6: Sandbox Resource Limits")
    print("=" * 60)
    print("Demonstrating code running within sandbox constraints...")
    print("=" * 60)
    
    print("\n✅ Safe Code Execution")
    print("-" * 60)
    results = execute_code(stub, code_safe)
    print_results(results)


def run_all_examples():
    """Run all examples in sequence."""
    print("\n" + "🚀" * 30)
    print("  DCodeX Feature Showcase")
    print("  Features: Resource Monitoring | Smart Caching | Sandboxing")
    print("🚀" * 30)
    
    run_example_hello()
    run_example_cache_demo()
    run_example_multiple_cache()
    run_example_computation()
    run_example_memory()
    run_example_sandbox_limits()
    
    print("\n" + "=" * 60)
    print("✨ All examples completed!")
    print("Features demonstrated:")
    print("  📊 Peak memory tracking")
    print("  ⏱️  Execution time measurement")
    print("  💾 Result caching with FNV-1a hashing")
    print("  ⚡ Cache hit/miss detection")
    print("  🛡️  Sandboxed execution")
    print("=" * 60)


if __name__ == '__main__':
    run_all_examples()
