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

#include "src/api/server_instance_manager.h"

#include <signal.h>
#include <unistd.h>

#include <chrono>
#include <fstream>
#include <thread>

#include "absl/base/call_once.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"

namespace dcodex {

namespace {

// Singleton instance storage.
absl::once_flag g_instance_once_flag;
ServerInstanceManager* g_instance = nullptr;

// Default PID file location.
constexpr absl::string_view kDefaultPidFilePath = "/tmp/dcodex_server.pid";

}  // namespace

// -----------------------------------------------------------------------------
// Singleton Instance Access
// -----------------------------------------------------------------------------

ServerInstanceManager& ServerInstanceManager::Instance() {
  absl::call_once(g_instance_once_flag, []() {
    g_instance = new ServerInstanceManager();
  });
  return *g_instance;
}

// -----------------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------------

ServerInstanceManager::ServerInstanceManager()
    : pid_file_path_(std::string(kDefaultPidFilePath)) {}

ServerInstanceManager::~ServerInstanceManager() {
  // Release lock on destruction if held.
  ReleaseLock();
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

absl::Status ServerInstanceManager::AcquireLock() {
  absl::MutexLock lock(&mutex_);

  if (is_locked_) {
    return absl::OkStatus();  // Already holding the lock.
  }

  // Check for existing instance.
  const pid_t existing_pid = ReadPidFromFile();
  if (existing_pid > 0) {
    if (IsProcessRunning(existing_pid)) {
      // Another instance is running - attempt to terminate it.
      LOG(INFO) << "Existing server found with PID " << existing_pid
                << ". Attempting to terminate...";

      if (!TerminateExistingProcess(existing_pid)) {
        return absl::FailedPreconditionError(
            absl::StrFormat("Another server instance is already running with PID %d",
                           existing_pid));
      }
    } else {
      // Stale lock file from a process that's no longer running.
      LOG(INFO) << "Found stale lock file from terminated process PID "
                << existing_pid;
      CleanupStaleLock();
    }
  }

  // Write our PID to the lock file.
  const absl::Status write_status = WritePidToFile();
  if (!write_status.ok()) {
    return write_status;
  }

  is_locked_ = true;
  LOG(INFO) << "Acquired single-instance lock. PID: " << getpid();
  return absl::OkStatus();
}

void ServerInstanceManager::ReleaseLock() {
  absl::MutexLock lock(&mutex_);

  if (!is_locked_) {
    return;  // Not holding the lock.
  }

  // Remove the PID file.
  if (std::remove(pid_file_path_.c_str()) == 0) {
    LOG(INFO) << "Released single-instance lock.";
  } else {
    LOG(WARNING) << "Failed to remove PID file: " << pid_file_path_;
  }

  is_locked_ = false;
}

bool ServerInstanceManager::IsLocked() const {
  absl::MutexLock lock(&mutex_);
  return is_locked_;
}

std::string ServerInstanceManager::GetPidFilePath() const {
  absl::MutexLock lock(&mutex_);
  return pid_file_path_;
}

// -----------------------------------------------------------------------------
// Private Helpers
// -----------------------------------------------------------------------------

pid_t ServerInstanceManager::ReadPidFromFile() const {
  std::ifstream pid_file(pid_file_path_);
  if (!pid_file.is_open()) {
    return -1;  // File doesn't exist.
  }

  pid_t pid = -1;
  if (!(pid_file >> pid)) {
    return -1;  // Invalid content.
  }

  return pid;
}

absl::Status ServerInstanceManager::WritePidToFile() const {
  std::ofstream pid_file(pid_file_path_, std::ios::trunc);
  if (!pid_file.is_open()) {
    return absl::InternalError(
        absl::StrFormat("Failed to create PID file: %s", pid_file_path_));
  }

  pid_file << getpid();
  if (!pid_file) {
    return absl::InternalError(
        absl::StrFormat("Failed to write PID to file: %s", pid_file_path_));
  }

  return absl::OkStatus();
}

bool ServerInstanceManager::IsProcessRunning(pid_t pid) const {
  if (pid <= 0) {
    return false;
  }
  // kill(pid, 0) returns 0 if process exists, -1 with ESRCH if not.
  return kill(pid, 0) == 0;
}

bool ServerInstanceManager::TerminateExistingProcess(pid_t pid) {
  if (pid <= 0) {
    return false;
  }

  // First try SIGTERM for graceful shutdown.
  if (kill(pid, SIGTERM) != 0) {
    if (errno == ESRCH) {
      // Process already terminated.
      return true;
    }
    LOG(ERROR) << "Failed to send SIGTERM to PID " << pid << ": " << strerror(errno);
    return false;
  }

  // Wait for graceful termination.
  constexpr int kMaxWaitAttempts = 10;
  constexpr auto kWaitInterval = std::chrono::milliseconds(100);

  for (int i = 0; i < kMaxWaitAttempts; ++i) {
    if (!IsProcessRunning(pid)) {
      LOG(INFO) << "Process " << pid << " terminated gracefully.";
      return true;
    }
    std::this_thread::sleep_for(kWaitInterval);
  }

  // Process didn't terminate gracefully, use SIGKILL.
  LOG(WARNING) << "Process " << pid << " did not terminate with SIGTERM, using SIGKILL...";

  if (kill(pid, SIGKILL) != 0) {
    if (errno == ESRCH) {
      return true;  // Process terminated between check and kill.
    }
    LOG(ERROR) << "Failed to send SIGKILL to PID " << pid << ": " << strerror(errno);
    return false;
  }

  // Wait for SIGKILL to take effect.
  for (int i = 0; i < kMaxWaitAttempts; ++i) {
    if (!IsProcessRunning(pid)) {
      LOG(INFO) << "Process " << pid << " terminated with SIGKILL.";
      return true;
    }
    std::this_thread::sleep_for(kWaitInterval);
  }

  LOG(ERROR) << "Failed to terminate process " << pid;
  return false;
}

void ServerInstanceManager::CleanupStaleLock() {
  if (std::remove(pid_file_path_.c_str()) != 0) {
    LOG(WARNING) << "Failed to remove stale PID file: " << pid_file_path_;
  }
}

}  // namespace dcodex