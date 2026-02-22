import grpc
import sys
import os

# Add the current directory to sys.path to find generated modules
sys.path.append(os.path.join(os.path.dirname(__file__), "."))
sys.path.append(os.path.join(os.path.dirname(__file__), ".."))

import proto.sandbox_pb2 as sandbox_pb2
import proto.sandbox_pb2_grpc as sandbox_pb2_grpc

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
    run()
