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
#include <memory>
#include <queue>
#include <thread>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "proto/sandbox.grpc.pb.h"
#include "src/server/sandbox.h"

ABSL_FLAG(uint16_t, port, 50051, "Server port for the service");
ABSL_FLAG(int, max_concurrent_sandboxes, 10,
          "Maximum number of concurrent sandboxes allowed");

using grpc::CallbackServerContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerWriteReactor;
using grpc::Status;
using grpc::StatusCode;

namespace dcodex {

namespace {

// Helper class to manage execution reactor state and output buffering.
class ExecutionReactorState {
 public:
  ExecutionReactorState() = default;

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
  void SetStats(const ResourceStats& stats) { stats_ = stats; }

  // Gets resource statistics.
  const ResourceStats& GetStats() const { return stats_; }

  // Sets cache hit flag.
  void SetCacheHit(bool cache_hit) { cache_hit_ = cache_hit; }

  // Gets cache hit flag.
  bool IsCacheHit() const { return cache_hit_; }

  // Sets wall-clock timeout flag.
  void SetWallClockTimeout(bool timed_out) { wall_clock_timeout_ = timed_out; }

  // Gets wall-clock timeout flag.
  bool IsWallClockTimeout() const { return wall_clock_timeout_; }

  // Sets output truncated flag.
  void SetOutputTruncated(bool truncated) { output_truncated_ = truncated; }

  // Gets output truncated flag.
  bool IsOutputTruncated() const { return output_truncated_; }

 private:
  std::queue<ExecutionLog> queue_;
  bool finished_ = false;
  bool writing_ = false;
  bool stats_sent_ = false;
  bool cache_hit_ = false;
  bool wall_clock_timeout_ = false;
  bool output_truncated_ = false;
  ResourceStats stats_;
};

// Handles rejection when too many sandboxes are active.
class RejectReactor : public ServerWriteReactor<ExecutionLog> {
 public:
  RejectReactor() {
    Finish(Status(StatusCode::RESOURCE_EXHAUSTED, "Too many active sandboxes"));
  }

  void OnDone() override { delete this; }
};

// Handles code execution with streaming output and caching support.
class ExecuteReactor : public ServerWriteReactor<ExecutionLog> {
 public:
  ExecuteReactor(const CodeRequest* request, std::atomic<int>& counter)
      : request_(request), counter_(counter) {
    counter_.fetch_add(1);
    StartExecution();
  }

  void OnWriteDone(bool ok) override {
    absl::MutexLock lock(&mutex_);
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
        request_->stdin_data(),
        [this](absl::string_view stdout_chunk, absl::string_view stderr_chunk) {
          HandleExecutionOutput(stdout_chunk, stderr_chunk);
        });

    HandleExecutionComplete(result);
  }

  // Handles output chunks from execution.
  void HandleExecutionOutput(absl::string_view stdout_chunk,
                             absl::string_view stderr_chunk) {
    absl::MutexLock lock(&mutex_);
    ExecutionLog log;
    if (!stdout_chunk.empty()) {
      log.set_stdout_chunk(std::string(stdout_chunk));
    }
    if (!stderr_chunk.empty()) {
      log.set_stderr_chunk(std::string(stderr_chunk));
    }
    state_.QueueLog(log);
    MaybeWriteNext();
  }

  // Handles completion of execution.
  void HandleExecutionComplete(const ExecutionResult& result) {
    absl::MutexLock lock(&mutex_);
    state_.SetStats(result.stats);
    state_.SetCacheHit(result.cache_hit);
    state_.SetWallClockTimeout(result.wall_clock_timeout);
    state_.SetOutputTruncated(result.output_truncated);
    state_.MarkFinished();
    // If the process failed, stream a stderr chunk so the client sees a
    // clear human-readable message.
    if (!result.success && !result.error_message.empty()) {
      ExecutionLog error_log;
      error_log.set_stderr_chunk("ERROR: " + result.error_message + "\n");
      state_.QueueLog(error_log);
    }
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
    stats_log.set_wall_clock_timeout(state_.IsWallClockTimeout());
    stats_log.set_output_truncated(state_.IsOutputTruncated());
    current_log_ = stats_log;
    StartWrite(&current_log_);
  }

  const CodeRequest* request_;
  std::thread worker_thread_;
  absl::Mutex mutex_;
  ExecutionReactorState state_;
  ExecutionLog current_log_;
  std::atomic<int>& counter_;
};

// Implements the gRPC CodeExecutor service.
class CodeExecutorServiceImpl final : public CodeExecutor::CallbackService {
 public:
  CodeExecutorServiceImpl() : active_sandboxes_(0) {}

  ServerWriteReactor<ExecutionLog>* Execute(
      CallbackServerContext* context, const CodeRequest* request) override {
    if (active_sandboxes_.load() >=
        absl::GetFlag(FLAGS_max_concurrent_sandboxes)) {
      return new RejectReactor();
    }
    return new ExecuteReactor(request, active_sandboxes_);
  }

 private:
  std::atomic<int> active_sandboxes_;
};

}  // namespace

// Starts the gRPC server and waits for connections.
absl::Status RunServer() {
  std::string server_address =
      absl::StrCat("0.0.0.0:", absl::GetFlag(FLAGS_port));
  CodeExecutorServiceImpl service;

  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  if (!server) {
    return absl::InternalError("Failed to start gRPC server");
  }
  LOG(INFO) << "Server listening on " << server_address;
  server->Wait();
  return absl::OkStatus();
}

}  // namespace dcodex

// Entry point for the application.
int main(int argc, char** argv) {
  absl::InitializeLog();
  absl::ParseCommandLine(argc, argv);
  absl::Status status = dcodex::RunServer();
  if (!status.ok()) {
    LOG(ERROR) << "Server failed: " << status.message();
    return 1;
  }
  return 0;
}