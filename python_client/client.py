import grpc
import sys
import os

# Add the current directory to sys.path to find generated modules
sys.path.append(os.path.join(os.path.dirname(__file__), "."))
sys.path.append(os.path.join(os.path.dirname(__file__), ".."))

import proto.sandbox_pb2 as sandbox_pb2
import proto.sandbox_pb2_grpc as sandbox_pb2_grpc

def run_example(language="cpp"):
    # Connect to the server
    channel = grpc.insecure_channel('localhost:50051')
    stub = sandbox_pb2_grpc.CodeExecutorStub(channel)

    if language == "cpp":
        code = """
#include <iostream>
int main() {
    std::cout << "Hello from C++ in DCodeX!" << std::endl;
    return 0;
}
"""
    elif language == "python":
        code = "print('Hello from Python in DCodeX!')"
    else:
        print(f"Unsupported language: {language}")
        return

    request = sandbox_pb2.CodeRequest(language=language, code=code)

    print(f"Sending {language} request to server...")
    try:
        # Execute the code and stream logs
        responses = stub.Execute(request)
        for response in responses:
            if response.stdout_chunk:
                print(f"STDOUT: {response.stdout_chunk}", end="")
            if response.stderr_chunk:
                print(f"STDERR: {response.stderr_chunk}", end="")
    except grpc.RpcError as e:
        print(f"RPC failed: {e}")

if __name__ == '__main__':
    # Run C++ example
    run_example("cpp")
    print("-" * 30)
    # Run Python example
    run_example("python")
