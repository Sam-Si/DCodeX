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

#ifndef SRC_ENGINE_PROCESS_TIMEOUT_MANAGER_H_
#define SRC_ENGINE_PROCESS_TIMEOUT_MANAGER_H_

#include <grpcpp/alarm.h>
#include <sys/types.h>

#include <functional>
#include <memory>

#include "absl/time/time.h"

namespace dcodex {

// -----------------------------------------------------------------------------
// ProcessTimeoutManager: gRPC Alarm-based process timeout
//
// Replaces the fork-based watcher process with gRPC Alarm for efficient
// timeout handling. This provides several benefits:
//
// 1. Resource Efficiency: No extra process forked for each timeout
// 2. Integration: Works seamlessly with gRPC's async completion queue
// 3. Cancellation: Supports graceful cancellation via callback
// 4. Thread Safety: Uses std::mutex for thread-safe state access
//
// Benefits of gRPC Alarm over fork-based approach:
// - Eliminates process table pollution (no zombie processes)
// - Reduces memory overhead (no duplicate address space)
// - Better integration with gRPC server lifecycle
// - Cleaner shutdown semantics
// -----------------------------------------------------------------------------
class ProcessTimeoutManager {
 public:
  // Callback type for timeout notification
  using TimeoutCallback = std::function<void()>;

  // Constructs a timeout manager for the given process.
  // The callback will be invoked when the timeout expires.
  ProcessTimeoutManager(pid_t pid, absl::Duration timeout,
                        TimeoutCallback callback);

  // Destructor ensures alarm is cancelled.
  ~ProcessTimeoutManager();

  // Starts the timeout alarm. Must be called after construction.
  void Start();

  // Cancels the pending timeout (idempotent).
  void Cancel();

  // Checks if the timeout has been triggered.
  [[nodiscard]] bool IsTriggered() const;

  // Checks if the timeout was cancelled before triggering.
  [[nodiscard]] bool IsCancelled() const;

 private:
  void OnAlarmTriggered(bool ok);

  absl::Duration timeout_;
  TimeoutCallback callback_;
  std::shared_ptr<struct ProcessTimeoutState> state_;
};

}  // namespace dcodex

#endif  // SRC_ENGINE_PROCESS_TIMEOUT_MANAGER_H_
