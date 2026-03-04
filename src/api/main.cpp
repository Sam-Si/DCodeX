// Copyright 2024 DCodeX Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <grpcpp/grpcpp.h>
#include <string>
#include <memory>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/substitute.h"
#include "src/api/code_executor_service.h"
#include "src/api/server_instance_manager.h"
#include "src/common/execution_cache.h"

ABSL_FLAG(uint16_t, port, 50051, "Server port for the service");
ABSL_FLAG(int, max_concurrent_sandboxes, 10,
          "Maximum number of concurrent sandboxes allowed");

namespace dcodex {

absl::Status RunServer() {
  // Acquire single-instance lock before starting server.
  ServerInstanceManager& instance_manager = ServerInstanceManager::Instance();
  absl::Status lock_status = instance_manager.AcquireLock();
  if (!lock_status.ok()) {
    LOG(ERROR) << "Failed to acquire single-instance lock: " << lock_status.message();
    return lock_status;
  }

  std::string server_address = absl::Substitute("0.0.0.0:$0", absl::GetFlag(FLAGS_port));
  auto cache = std::make_shared<ExecutionCache>(absl::Hours(1), 1000);
  CodeExecutorServiceImpl service(absl::GetFlag(FLAGS_max_concurrent_sandboxes), std::move(cache));
  
  grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  if (!server) {
    instance_manager.ReleaseLock();
    return absl::InternalError("Failed to start gRPC server");
  }
  LOG(INFO) << "Server listening on " << server_address;
  server->Wait();
  
  // Release lock on shutdown (though this is typically not reached).
  instance_manager.ReleaseLock();
  return absl::OkStatus();
}

}  // namespace dcodex

int main(int argc, char** argv) {
  absl::InitializeLog();
  absl::ParseCommandLine(argc, argv);
  const absl::Status status = dcodex::RunServer();
  if (!status.ok()) {
    LOG(ERROR) << "Server failed: " << status.message();
    return 1;
  }
  return 0;
}
