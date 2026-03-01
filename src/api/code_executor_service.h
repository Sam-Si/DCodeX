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

#ifndef SRC_API_CODE_EXECUTOR_SERVICE_H_
#define SRC_API_CODE_EXECUTOR_SERVICE_H_

#include <grpcpp/grpcpp.h>
#include <atomic>
#include <memory>

#include "proto/sandbox.grpc.pb.h"
#include "src/engine/warm_worker_pool.h"

namespace dcodex {

class CodeExecutorServiceImpl final : public CodeExecutor::CallbackService {
 public:
  explicit CodeExecutorServiceImpl(int max_sandboxes);
  ~CodeExecutorServiceImpl() override;

  grpc::ServerWriteReactor<ExecutionLog>* Execute(
      grpc::CallbackServerContext* context, const CodeRequest* request) override;

 private:
  std::atomic<int> active_sandboxes_;
  WarmWorkerPool worker_pool_;
};

void EnsureSingleInstance();

}  // namespace dcodex

#endif  // SRC_API_CODE_EXECUTOR_SERVICE_H_
