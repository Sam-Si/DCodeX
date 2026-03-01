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

#ifndef SRC_SERVER_TEMP_FILE_MANAGER_H_
#define SRC_SERVER_TEMP_FILE_MANAGER_H_

#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace dcodex::internal {

// ==============================================================================
// TempFileManager: Temporary File Utilities
// ==============================================================================

/// Utility class for creating and managing temporary files.
class TempFileManager {
 public:
  /// Creates a temporary file with the given extension and writes content to it.
  /// Returns the path to the created file, or an error status on failure.
  [[nodiscard]] static absl::StatusOr<std::string> WriteTempFile(
      absl::string_view extension, absl::string_view content) {
    char template_str[] = "/tmp/dcodex_XXXXXX";
    const int fd_val = mkstemp(template_str);
    if (fd_val == -1) {
      return absl::ErrnoToStatus(errno, "Failed to create temporary file");
    }
    close(fd_val);

    const std::string path = absl::StrCat(template_str, extension);
    std::ofstream out(path);
    if (!out) {
      return absl::UnknownError(
          absl::StrCat("Failed to open temporary file for writing: ", path));
    }
    out << content;
    if (!out) {
      return absl::UnknownError(
          absl::StrCat("Failed to write content to temporary file: ", path));
    }
    return path;
  }

  // ==============================================================================
  // Guard: RAII Temporary File Cleanup
  // ==============================================================================

  /// RAII guard that deletes a file when it goes out of scope.
  class Guard {
   public:
    explicit Guard(std::filesystem::path path) : path_(std::move(path)) {}

    ~Guard() {
      if (!path_.empty()) {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
      }
    }

    // Non-copyable
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;

    // Movable
    Guard(Guard&& other) noexcept : path_(std::move(other.path_)) {
      other.path_.clear();
    }

    Guard& operator=(Guard&& other) noexcept {
      if (this != &other) {
        std::error_code ec;
        if (!path_.empty()) {
          std::filesystem::remove(path_, ec);
        }
        path_ = std::move(other.path_);
        other.path_.clear();
      }
      return *this;
    }

    /// Releases ownership of the file without deleting it.
    void Release() { path_.clear(); }

    /// Returns the path to the managed file.
    [[nodiscard]] const std::filesystem::path& GetPath() const noexcept {
      return path_;
    }

   private:
    std::filesystem::path path_;
  };
};

}  // namespace dcodex::internal

#endif  // SRC_SERVER_TEMP_FILE_MANAGER_H_
