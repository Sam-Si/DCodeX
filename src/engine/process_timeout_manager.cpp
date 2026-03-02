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

#include "src/engine/process_timeout_manager.h"

#include <mutex>
#include <memory>

#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace dcodex {

struct ProcessTimeoutState {
  ProcessTimeoutState(absl::Duration timeout, ProcessTimeoutManager::TimeoutCallback callback)
      : timeout(timeout), callback(std::move(callback)) {}

  absl::Duration timeout;
  ProcessTimeoutManager::TimeoutCallback callback;
  grpc::Alarm alarm;
  std::mutex mutex;
  bool started = false;
  bool triggered = false;
  bool cancelled = false;
};

ProcessTimeoutManager::ProcessTimeoutManager(pid_t pid, absl::Duration timeout,
                                             TimeoutCallback callback)
    : timeout_(timeout), callback_(std::move(callback)) {
  (void)pid;
  state_ = std::make_shared<ProcessTimeoutState>(timeout, std::move(callback_));
}

ProcessTimeoutManager::~ProcessTimeoutManager() { Cancel(); }

void ProcessTimeoutManager::Start() {
  std::lock_guard<std::mutex> lock(state_->mutex);
  if (state_->started || state_->cancelled) {
    return;
  }
  state_->started = true;

  auto deadline = absl::ToChronoTime(absl::Now() + state_->timeout);
  
  state_->alarm.Set(deadline, [state = state_](bool ok) {
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      if (state->cancelled || !ok) {
        return;
      }
      state->triggered = true;
    }
    if (state->callback) {
      state->callback();
    }
  });
}

void ProcessTimeoutManager::Cancel() {
  std::lock_guard<std::mutex> lock(state_->mutex);
  if (state_->cancelled || state_->triggered) {
    return;
  }
  state_->cancelled = true;
  state_->alarm.Cancel();
}

bool ProcessTimeoutManager::IsTriggered() const {
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->triggered;
}

bool ProcessTimeoutManager::IsCancelled() const {
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->cancelled;
}

void ProcessTimeoutManager::OnAlarmTriggered(bool ok) {
  (void)ok;
}

}  // namespace dcodex
