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

#include "src/api/code_executor_service.h"
#include <signal.h>
#include <unistd.h>
#include <fstream>
#include <thread>

#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "src/api/execute_reactor.h"

ABSL_DECLARE_FLAG(int, max_concurrent_sandboxes);

namespace dcodex {

class RejectReactor final : public grpc::ServerWriteReactor<ExecutionLog> {
 public:
  RejectReactor(absl::string_view reason, CodeExecutorServiceImpl* owner)
      : owner_(owner) {
    Finish(grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                        std::string(reason)));
  }

  void OnDone() override;

 private:
  CodeExecutorServiceImpl* owner_;
};

void RejectReactor::OnDone() {
  if (owner_ != nullptr) {
    owner_->ReleaseRejectReactor(this);
  }
}

CodeExecutorServiceImpl::CodeExecutorServiceImpl(int max_sandboxes)
    : active_sandboxes_(0),
      worker_pool_(max_sandboxes) {
  worker_pool_.Start();
}

CodeExecutorServiceImpl::~CodeExecutorServiceImpl() {
  worker_pool_.Shutdown();
  absl::MutexLock lock(&reject_mutex_);
  reject_reactors_.clear();
}

void CodeExecutorServiceImpl::TrackRejectReactor(
    const std::shared_ptr<RejectReactor>& reactor) {
  absl::MutexLock lock(&reject_mutex_);
  reject_reactors_.emplace(reactor.get(), reactor);
}

void CodeExecutorServiceImpl::ReleaseRejectReactor(const RejectReactor* reactor) {
  absl::MutexLock lock(&reject_mutex_);
  reject_reactors_.erase(reactor);
}

grpc::ServerWriteReactor<ExecutionLog>* CodeExecutorServiceImpl::Execute(
    grpc::CallbackServerContext* context, const CodeRequest* request) {
  (void)context;
  if (active_sandboxes_.load() >= absl::GetFlag(FLAGS_max_concurrent_sandboxes)) {
    auto reactor = std::make_shared<RejectReactor>(
        "Too many active sandboxes", this);
    TrackRejectReactor(reactor);
    return reactor.get();
  }
  auto reactor = std::make_shared<ExecuteReactor>(request, active_sandboxes_,
                                                   &worker_pool_);
  absl::Status assignment = worker_pool_.AcquireWorker(reactor);
  if (!assignment.ok()) {
    LOG(WARNING) << "Worker pool rejected request: " << assignment;
    auto reject_reactor = std::make_shared<RejectReactor>(
        "Worker pool rejected request", this);
    TrackRejectReactor(reject_reactor);
    return reject_reactor.get();
  }
  return reactor.get();
}

void EnsureSingleInstance() {
  const std::string pid_file = "/tmp/dcodex_server.pid";
  std::ifstream existing_pid_file(pid_file);
  if (existing_pid_file.is_open()) {
    pid_t old_pid;
    if (existing_pid_file >> old_pid) {
      if (kill(old_pid, 0) == 0) {
        LOG(INFO) << "Existing server found with PID " << old_pid << ". Killing it...";
        kill(old_pid, SIGTERM);
        // Wait a bit for it to terminate
        for (int i = 0; i < 10; ++i) {
          if (kill(old_pid, 0) != 0) break;
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (kill(old_pid, 0) == 0) {
          LOG(WARNING) << "Server did not terminate with SIGTERM, using SIGKILL...";
          kill(old_pid, SIGKILL);
        }
      }
    }
    existing_pid_file.close();
  }

  std::ofstream new_pid_file(pid_file, std::ios::trunc);
  if (new_pid_file.is_open()) {
    new_pid_file << getpid();
    new_pid_file.close();
  } else {
    LOG(ERROR) << "Failed to create PID file: " << pid_file;
  }
}

}  // namespace dcodex
