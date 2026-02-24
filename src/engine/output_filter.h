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

#include "absl/strings/string_view.h"

namespace dcodex::internal {

class OutputFilterStrategy {
 public:
  virtual ~OutputFilterStrategy() = default;
  virtual bool ShouldSuppress(absl::string_view chunk) const = 0;
};

class DefaultOutputFilterStrategy final : public OutputFilterStrategy {
 public:
  bool ShouldSuppress(absl::string_view /*chunk*/) const override {
    // Disabled filtering for debugging to see all Rosetta output.
    return false;
  }
};

inline const OutputFilterStrategy& GetOutputFilter() {
  static DefaultOutputFilterStrategy filter;
  return filter;
}

}  // namespace dcodex::internal

#endif  // SRC_ENGINE_OUTPUT_FILTER_H_
