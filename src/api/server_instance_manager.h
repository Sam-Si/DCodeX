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

#ifndef SRC_API_SERVER_INSTANCE_MANAGER_H_
#define SRC_API_SERVER_INSTANCE_MANAGER_H_

#include <sys/types.h>

#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"

namespace dcodex {

// -----------------------------------------------------------------------------
// ServerInstanceManager: Thread-safe Singleton
// Guarantees only a single server instance can run on a machine at any time.
// Uses file-based locking with PID verification for process persistence.
// -----------------------------------------------------------------------------
class ServerInstanceManager {
 public:
  // Deleted copy and move operations for singleton semantics.
  ServerInstanceManager(const ServerInstanceManager&) = delete;
  ServerInstanceManager& operator=(const ServerInstanceManager&) = delete;
  ServerInstanceManager(ServerInstanceManager&&) = delete;
  ServerInstanceManager& operator=(ServerInstanceManager&&) = delete;

  // Returns the singleton instance.
  // Thread-safe: Uses absl::call_once for initialization.
  [[nodiscard]] static ServerInstanceManager& Instance();

  // Attempts to acquire the single-instance lock.
  // Returns OK if this process successfully acquired the lock.
  // Returns error status if another instance is already running.
  // Thread-safe: Uses mutex for internal synchronization.
  [[nodiscard]] absl::Status AcquireLock();

  // Releases the single-instance lock.
  // Should be called during graceful shutdown.
  // Thread-safe: Uses mutex for internal synchronization.
  void ReleaseLock();

  // Checks if this instance currently holds the lock.
  [[nodiscard]] bool IsLocked() const;

  // Returns the PID file path being used.
  [[nodiscard]] std::string GetPidFilePath() const;

 private:
  ServerInstanceManager();
  ~ServerInstanceManager();

  // Reads the PID from the lock file.
  // Returns -1 if file doesn't exist or is invalid.
  [[nodiscard]] pid_t ReadPidFromFile() const;

  // Writes the current process PID to the lock file.
  [[nodiscard]] absl::Status WritePidToFile() const;

  // Checks if a process with the given PID is still running.
  [[nodiscard]] bool IsProcessRunning(pid_t pid) const;

  // Terminates an existing server instance.
  // Returns true if the process was successfully terminated.
  [[nodiscard]] bool TerminateExistingProcess(pid_t pid);

  // Cleans up stale lock file (process no longer running).
  void CleanupStaleLock();

  mutable absl::Mutex mutex_;
  bool is_locked_ ABSL_GUARDED_BY(mutex_) = false;
  std::string pid_file_path_;
};

}  // namespace dcodex

#endif  // SRC_API_SERVER_INSTANCE_MANAGER_H_