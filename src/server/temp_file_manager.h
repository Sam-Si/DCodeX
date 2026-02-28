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

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace dcodex::internal {

class TempFileManager {
 public:
  static std::string WriteTempFile(absl::string_view extension,
                                   absl::string_view content) {
    char template_str[] = "/tmp/dcodex_XXXXXX";
    int fd_val = mkstemp(template_str);
    if (fd_val == -1) return "";
    close(fd_val);

    std::string path = absl::StrCat(template_str, extension);
    std::ofstream out(path);
    out << content;
    return path;
  }

  class Guard {
   public:
    explicit Guard(std::filesystem::path path) : path_(std::move(path)) {}
    ~Guard() {
      if (!path_.empty()) {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
      }
    }
    void Release() { path_.clear(); }

   private:
    std::filesystem::path path_;
  };
};

}  // namespace dcodex::internal

#endif  // SRC_SERVER_TEMP_FILE_MANAGER_H_
