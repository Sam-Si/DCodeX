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

CodeExecutorServiceImpl::CodeExecutorServiceImpl(int max_sandboxes,
                                                 std::shared_ptr<CacheInterface> cache)
    : active_sandboxes_(0),
      worker_pool_([max_sandboxes]() {
        DynamicWorkerCoordinator::Options opts;
        opts.max_workers = max_sandboxes;
        return opts;
      }()),
      executor_(std::make_shared<SandboxedProcess>(std::move(cache))) {
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
                                                   &worker_pool_, executor_);
  
  LanguageId lang = ParseLanguageId(request->language());
  absl::StatusOr<WorkerTask*> assignment = worker_pool_.LeaseWorker(lang, reactor);
  
  if (!assignment.ok()) {
    LOG(WARNING) << "Worker pool rejected request: " << assignment.status();
    auto reject_reactor = std::make_shared<RejectReactor>(
        "Worker pool rejected request", this);
    TrackRejectReactor(reject_reactor);
    return reject_reactor.get();
  }
  return reactor.get();
}

}  // namespace dcodex
