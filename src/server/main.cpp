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

#include <atomic>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>

#include "proto/sandbox.grpc.pb.h"
#include "src/server/sandbox.h"

using grpc::CallbackServerContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerWriteReactor;
using grpc::Status;
using grpc::StatusCode;
using dcodex::CodeExecutor;
using dcodex::CodeRequest;
using dcodex::ExecutionLog;
using dcodex::SandboxedProcess;

namespace dcodex {

// Helper class to manage execution reactor state and output buffering.
class ExecutionReactorState {
 public:
  ExecutionReactorState()
      : finished_(false),
        writing_(false),
        stats_sent_(false),
        cache_hit_(false) {}

  // Queues an execution log entry.
  void QueueLog(const ExecutionLog& log) { queue_.push(log); }

  // Checks if there are pending logs to write.
  bool HasPendingLogs() const { return !queue_.empty(); }

  // Gets the next log from the queue.
  ExecutionLog GetNextLog() {
    ExecutionLog log = queue_.front();
    queue_.pop();
    return log;
  }

  // Marks execution as finished.
  void MarkFinished() { finished_ = true; }

  // Checks if execution is finished.
  bool IsFinished() const { return finished_; }

  // Marks stats as sent.
  void MarkStatsSent() { stats_sent_ = true; }

  // Checks if stats have been sent.
  bool AreStatsSent() const { return stats_sent_; }

  // Gets the writing flag.
  bool IsWriting() const { return writing_; }

  // Sets the writing flag.
  void SetWriting(bool writing) { writing_ = writing; }

  // Sets resource statistics.
  void SetStats(const SandboxedProcess::ResourceStats& stats) {
    stats_ = stats;
  }

  // Gets resource statistics.
  const SandboxedProcess::ResourceStats& GetStats() const { return stats_; }

  // Sets cache hit flag.
  void SetCacheHit(bool cache_hit) { cache_hit_ = cache_hit; }

  // Gets cache hit flag.
  bool IsCacheHit() const { return cache_hit_; }

 private:
  std::queue<ExecutionLog> queue_;
  bool finished_;
  bool writing_;
  bool stats_sent_;
  bool cache_hit_;
  SandboxedProcess::ResourceStats stats_;
};

// Handles rejection when too many sandboxes are active.
class RejectReactor : public ServerWriteReactor<ExecutionLog> {
 public:
  RejectReactor() {
    Finish(Status(StatusCode::RESOURCE_EXHAUSTED,
                  "Too many active sandboxes"));
  }

  void OnDone() override { delete this; }
};

// Handles code execution with streaming output and caching support.
class ExecuteReactor : public ServerWriteReactor<ExecutionLog> {
 public:
  ExecuteReactor(const CodeRequest* request, std::atomic<int>& counter)
      : request_(request), counter_(counter) {
    StartExecution();
  }

  void OnWriteDone(bool ok) override {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.SetWriting(false);
    if (!ok) {
      return;
    }
    MaybeWriteNext();
  }

  void OnDone() override {
    if (worker_thread_.joinable()) {
      worker_thread_.join();
    }
    counter_.fetch_sub(1);
    delete this;
  }

  void OnCancel() override {
    // In a real system, we'd kill the process here.
  }

 private:
  // Starts the execution in a background thread.
  void StartExecution() {
    worker_thread_ = std::thread([this]() { ExecuteInBackground(); });
  }

  // Executes code in the background thread.
  void ExecuteInBackground() {
    auto result = SandboxedProcess::CompileAndRunStreaming(
        request_->code(),
        [this](const std::string& stdout_chunk,
               const std::string& stderr_chunk) {
          HandleExecutionOutput(stdout_chunk, stderr_chunk);
        });

    HandleExecutionComplete(result);
  }

  // Handles output chunks from execution.
  void HandleExecutionOutput(const std::string& stdout_chunk,
                              const std::string& stderr_chunk) {
    std::lock_guard<std::mutex> lock(mutex_);
    ExecutionLog log;
    log.set_stdout_chunk(stdout_chunk);
    log.set_stderr_chunk(stderr_chunk);
    state_.QueueLog(log);
    MaybeWriteNext();
  }

  // Handles completion of execution.
  void HandleExecutionComplete(const SandboxedProcess::Result& result) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.SetStats(result.stats);
    state_.SetCacheHit(result.cache_hit);
    state_.MarkFinished();
    MaybeWriteNext();
  }

  // Attempts to write the next log entry if possible.
  void MaybeWriteNext() {
    if (state_.IsWriting()) {
      return;
    }

    if (state_.HasPendingLogs()) {
      WriteNextLog();
    } else if (state_.IsFinished() && !state_.AreStatsSent()) {
      WriteStatistics();
    } else if (state_.IsFinished() && state_.AreStatsSent()) {
      Finish(Status::OK);
    }
  }

  // Writes the next log entry from the queue.
  void WriteNextLog() {
    state_.SetWriting(true);
    current_log_ = state_.GetNextLog();
    StartWrite(&current_log_);
  }

  // Writes resource statistics as the final log.
  void WriteStatistics() {
    state_.SetWriting(true);
    state_.MarkStatsSent();
    ExecutionLog stats_log;
    stats_log.set_peak_memory_bytes(state_.GetStats().peak_memory_bytes);
    stats_log.set_execution_time_ms(
        static_cast<float>(state_.GetStats().elapsed_time_ms));
    stats_log.set_cache_hit(state_.IsCacheHit());
    current_log_ = stats_log;
    StartWrite(&current_log_);
  }

  const CodeRequest* request_;
  std::thread worker_thread_;
  std::mutex mutex_;
  ExecutionReactorState state_;
  ExecutionLog current_log_;
  std::atomic<int>& counter_;
};

}  // namespace dcodex

// Implements the gRPC CodeExecutor service.
class CodeExecutorServiceImpl final : public CodeExecutor::CallbackService {
 public:
  CodeExecutorServiceImpl() : active_sandboxes_(0) {}

  ServerWriteReactor<ExecutionLog>* Execute(CallbackServerContext* context,
                                            const CodeRequest* request) override {
    return HandleExecuteRequest(request);
  }

 private:
  // Handles an incoming execute request.
  ServerWriteReactor<ExecutionLog>* HandleExecuteRequest(
      const CodeRequest* request) {
    if (active_sandboxes_.fetch_add(1) >= 10) {
      active_sandboxes_.fetch_sub(1);
      return new dcodex::RejectReactor();
    }
    return new dcodex::ExecuteReactor(request, active_sandboxes_);
  }

  std::atomic<int> active_sandboxes_;
};

// Starts the gRPC server and waits for connections.
void RunServer() {
  std::string server_address("0.0.0.0:50051");
  CodeExecutorServiceImpl service;

  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;
  server->Wait();
}

// Entry point for the application.
int main(int argc, char** argv) {
  RunServer();
  return 0;
}