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
#include <mutex>
#include <queue>
#include <thread>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
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

// =============================================================================
// ROOT CAUSE ANALYSIS: Mutex Corruption
// =============================================================================
//
// The original implementation had a critical flaw: gRPC reactor callbacks
// (OnWriteDone, OnDone) can be invoked SYNCHRONOUSLY from within StartWrite()
// or Finish() calls. This creates a re-entrant call chain that leads to:
//
// 1. Thread holds mutex -> calls StartWrite() -> gRPC calls OnWriteDone()
//    synchronously -> OnWriteDone tries to acquire mutex -> CORRUPTION
//
// 2. The loop in ProcessNextWrite() creates complex re-entrant scenarios
//
// 3. absl::Mutex is NOT reentrant - attempting to acquire it twice from the
//    same thread causes the "both reader and writer lock held" error
//
// =============================================================================
// THE FIX: Three-Thread Model with Lock-Free State Machine
// =============================================================================
//
// We use a three-thread model with atomic state to eliminate re-entrancy:
//
// 1. gRPC Thread: Only calls reactor callbacks (OnWriteDone, OnDone, OnCancel)
//    - These callbacks ONLY update atomic state and notify the processor
//    - NEVER call StartWrite/Finish from callbacks
//
// 2. Worker Thread: Executes the actual code
//    - Produces output chunks and final result
//    - Queues data and notifies the processor
//
// 3. Processor Thread: Dedicated thread for gRPC operations
//    - Runs in a loop, checking state and calling StartWrite/Finish
//    - Only thread that calls StartWrite/Finish
//    - No re-entrancy possible since it's a single thread with no callbacks
//
// State transitions use std::atomic to avoid mutex corruption:
//
// IDLE -> WRITING -> WAITING_FOR_CALLBACK -> WRITING -> ... -> FINISHED
//
// =============================================================================

// State machine for the reactor.
// Uses atomic operations to avoid mutex corruption.
enum class ReactorState {
  kIdle,                    // No operation in progress
  kWriting,                 // StartWrite() was called, waiting for OnWriteDone
  kPendingWrite,            // Data available, need to call StartWrite
  kPendingFinish,           // Ready to call Finish
  kFinished,                // Finish() was called
};

// Thread-safe queue for execution logs.
// Uses a mutex internally but with minimal contention.
class ThreadSafeLogQueue {
 public:
  void Push(const ExecutionLog& log) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(log);
  }

  bool Pop(ExecutionLog& log) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
      return false;
    }
    log = queue_.front();
    queue_.pop();
    return true;
  }

  bool Empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
  }

 private:
  mutable std::mutex mutex_;
  std::queue<ExecutionLog> queue_;
};

// Handles rejection when too many sandboxes are active.
class RejectReactor : public ServerWriteReactor<ExecutionLog> {
 public:
  RejectReactor() {
    Finish(Status(StatusCode::RESOURCE_EXHAUSTED, "Too many active sandboxes"));
  }

  void OnDone() override { delete this; }
};

// Handles code execution with streaming output.
// Uses a dedicated processor thread to avoid re-entrant gRPC calls.
class ExecuteReactor : public ServerWriteReactor<ExecutionLog> {
 public:
  ExecuteReactor(const CodeRequest* request, std::atomic<int>& counter)
      : request_(request), counter_(counter), state_(ReactorState::kIdle) {
    counter_.fetch_add(1);
    StartProcessorThread();
  }

  void OnWriteDone(bool ok) override {
    // CRITICAL: This callback is invoked by gRPC thread.
    // We MUST NOT call StartWrite/Finish here to avoid re-entrancy.
    // Only update state and signal the processor thread.

    if (!ok) {
      // Write failed - signal processor to finish
      write_failed_.store(true);
    }

    // Transition from kWriting to kIdle to allow next operation
    ReactorState expected = ReactorState::kWriting;
    state_.compare_exchange_strong(expected, ReactorState::kIdle);

    // Wake up processor thread
    NotifyProcessor();
  }

  void OnDone() override {
    // Signal processor to stop
    reactor_done_.store(true);
    NotifyProcessor();

    if (worker_thread_.joinable()) {
      worker_thread_.join();
    }
    if (processor_thread_.joinable()) {
      processor_thread_.join();
    }
    counter_.fetch_sub(1);
    delete this;
  }

  void OnCancel() override {
    cancelled_.store(true);
    NotifyProcessor();
  }

 private:
  // Starts the processor thread that handles all gRPC operations.
  void StartProcessorThread() {
    processor_thread_ = std::thread([this]() { ProcessorLoop(); });
  }

  // Processor thread main loop.
  // This is the ONLY thread that calls StartWrite() and Finish().
  void ProcessorLoop() {
    // First, start the worker thread that will execute the code
    StartWorkerThread();

    // Process until done
    while (!reactor_done_.load() && !cancelled_.load()) {
      // Wait for work or notification
      WaitForNotification();

      if (reactor_done_.load() || cancelled_.load()) {
        break;
      }

      ProcessState();
    }

    // Ensure we finish if not already done
    if (state_.load() != ReactorState::kFinished) {
      Finish(Status::OK);
      state_.store(ReactorState::kFinished);
    }
  }

  // Processes the current state and performs appropriate action.
  // This is called from the processor thread only.
  void ProcessState() {
    // Use a loop to handle multiple operations if needed
    int iterations = 0;
    const int max_iterations = 100;  // Prevent infinite loops

    while (!reactor_done_.load() && !cancelled_.load() &&
           iterations++ < max_iterations) {
      ReactorState current = state_.load();

      switch (current) {
        case ReactorState::kIdle: {
          // Check if we have data to write
          ExecutionLog log;
          if (log_queue_.Pop(log)) {
            // Have data - transition to writing
            current_log_ = log;
            if (state_.compare_exchange_strong(current, ReactorState::kWriting)) {
              StartWrite(&current_log_);
              return;  // Wait for OnWriteDone callback
            }
          } else if (execution_finished_.load() && log_queue_.Empty()) {
            // No more data and execution finished - send stats and finish
            if (!stats_sent_.load()) {
              SendStatsAndFinish();
              return;
            } else {
              // Already sent stats, just finish
              if (state_.compare_exchange_strong(current, ReactorState::kFinished)) {
                Finish(Status::OK);
                return;
              }
            }
          } else {
            // Nothing to do - exit loop
            return;
          }
          break;
        }

        case ReactorState::kWriting:
          // Waiting for OnWriteDone callback - nothing to do
          return;

        case ReactorState::kFinished:
          // Already finished
          return;

        default:
          // Unknown state - reset to idle
          state_.store(ReactorState::kIdle);
          break;
      }
    }
  }

  // Sends final stats message and finishes the RPC.
  void SendStatsAndFinish() {
    ExecutionLog stats_log;
    stats_log.set_peak_memory_bytes(final_stats_.peak_memory_bytes);
    stats_log.set_execution_time_ms(
        static_cast<float>(final_stats_.elapsed_time_ms));
    stats_log.set_cache_hit(cache_hit_.load());
    stats_log.set_wall_clock_timeout(wall_clock_timeout_.load());
    stats_log.set_output_truncated(output_truncated_.load());

    current_log_ = stats_log;
    stats_sent_.store(true);

    ReactorState current = state_.load();
    if (state_.compare_exchange_strong(current, ReactorState::kWriting)) {
      StartWrite(&current_log_);
      // Finish will be called after this write completes
    }
  }

  // Starts the worker thread that executes the code.
  void StartWorkerThread() {
    worker_thread_ = std::thread([this]() { ExecuteInBackground(); });
  }

  // Executes code in the background thread.
  void ExecuteInBackground() {
    absl::StatusOr<ExecutionResult> result =
        SandboxedProcess::CompileAndRunStreaming(
            request_->language(),
            request_->code(),
            request_->stdin_data(),
            [this](absl::string_view stdout_chunk, absl::string_view stderr_chunk) {
              QueueOutputChunk(stdout_chunk, stderr_chunk);
            });

    if (result.ok()) {
      QueueCompletion(*result);
    } else {
      ExecutionResult error_result;
      error_result.success = false;
      error_result.error_message = std::string(result.status().message());
      QueueCompletion(error_result);
    }
  }

  // Queues an output chunk from execution (called from worker thread).
  void QueueOutputChunk(absl::string_view stdout_chunk,
                        absl::string_view stderr_chunk) {
    if (stdout_chunk.empty() && stderr_chunk.empty()) {
      return;
    }

    ExecutionLog log;
    if (!stdout_chunk.empty()) {
      log.set_stdout_chunk(std::string(stdout_chunk));
    }
    if (!stderr_chunk.empty()) {
      log.set_stderr_chunk(std::string(stderr_chunk));
    }

    log_queue_.Push(log);
    NotifyProcessor();
  }

  // Queues completion result (called from worker thread).
  void QueueCompletion(const ExecutionResult& result) {
    // Store final stats
    final_stats_ = result.stats;
    cache_hit_.store(result.cache_hit);
    wall_clock_timeout_.store(result.wall_clock_timeout);
    output_truncated_.store(result.output_truncated);

    // Queue error message if any
    if (!result.success) {
      std::string error_msg;
      if (!result.error_message.empty()) {
        error_msg = "ERROR: " + result.error_message + "\n";
      }
      if (!result.backend_trace.empty()) {
        error_msg += result.backend_trace + "\n";
      }
      if (!error_msg.empty()) {
        ExecutionLog error_log;
        error_log.set_stderr_chunk(error_msg);
        log_queue_.Push(error_log);
      }
    }

    // Mark execution as finished
    execution_finished_.store(true);
    NotifyProcessor();
  }

  // Notifies the processor thread that there's work to do.
  void NotifyProcessor() {
    // Use a simple atomic flag for notification
    // The processor thread polls this flag
    notification_pending_.store(true, std::memory_order_release);
  }

  // Waits for a notification from another thread.
  void WaitForNotification() {
    // Simple spin-wait with yield
    // This is efficient for short waits
    while (!notification_pending_.load(std::memory_order_acquire)) {
      if (reactor_done_.load() || cancelled_.load()) {
        return;
      }
      std::this_thread::yield();
    }
    notification_pending_.store(false, std::memory_order_release);
  }

  // Member variables
  const CodeRequest* request_;
  std::thread worker_thread_;
  std::thread processor_thread_;

  // Atomic state - no mutex needed
  std::atomic<ReactorState> state_;
  std::atomic<bool> execution_finished_{false};
  std::atomic<bool> stats_sent_{false};
  std::atomic<bool> cache_hit_{false};
  std::atomic<bool> wall_clock_timeout_{false};
  std::atomic<bool> output_truncated_{false};
  std::atomic<bool> reactor_done_{false};
  std::atomic<bool> cancelled_{false};
  std::atomic<bool> write_failed_{false};
  std::atomic<bool> notification_pending_{false};

  // Thread-safe queue for logs
  ThreadSafeLogQueue log_queue_;

  // Current log being written (only accessed by processor thread)
  ExecutionLog current_log_;

  // Final stats (set by worker, read by processor)
  ResourceStats final_stats_;

  // Reference counter
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