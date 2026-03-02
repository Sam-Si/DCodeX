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

#ifndef SRC_ENGINE_OUTPUT_FILTER_H_
#define SRC_ENGINE_OUTPUT_FILTER_H_

#include <string>
#include <vector>
#include <regex>

#include "absl/strings/string_view.h"

namespace dcodex {

// Interface for output filtering strategies.
class OutputFilter {
 public:
  virtual ~OutputFilter() = default;

  // Processes a chunk of output.
  // Returns the processed (possibly modified or empty) chunk.
  virtual std::string Process(absl::string_view chunk) const = 0;

  // Returns true if the chunk should be completely suppressed.
  virtual bool ShouldSuppress(absl::string_view chunk) const = 0;
};

// Simple filter that redacts sensitive information using regex.
class RedactionFilter final : public OutputFilter {
 public:
  void AddRule(const std::string& pattern, const std::string& replacement) {
    rules_.emplace_back(std::regex(pattern), replacement);
  }

  std::string Process(absl::string_view chunk) const override {
    std::string result(chunk);
    for (const auto& [re, replacement] : rules_) {
      result = std::regex_replace(result, re, replacement);
    }
    return result;
  }

  bool ShouldSuppress(absl::string_view chunk) const override {
    return false;
  }

 private:
  std::vector<std::pair<std::regex, std::string>> rules_;
};

// Default filter that does nothing (pass-through).
class PassThroughFilter final : public OutputFilter {
 public:
  std::string Process(absl::string_view chunk) const override {
    return std::string(chunk);
  }
  bool ShouldSuppress(absl::string_view chunk) const override {
    return false;
  }
};

namespace internal {
// Registry for the global output filter.
inline const OutputFilter& GetOutputFilter() {
  static PassThroughFilter filter;
  return filter;
}
}  // namespace internal

}  // namespace dcodex

#endif  // SRC_ENGINE_OUTPUT_FILTER_H_
