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

#ifndef SRC_ENGINE_LANGUAGE_REGISTRY_H_
#define SRC_ENGINE_LANGUAGE_REGISTRY_H_

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/engine/execution_strategy.h"

namespace dcodex {

// Registry for execution strategies.
// Allows for dynamic registration of new languages.
class LanguageRegistry {
 public:
  using StrategyFactory = std::function<std::unique_ptr<ExecutionStrategy>(
      std::shared_ptr<CacheInterface>)>;

  static LanguageRegistry& Get() {
    static LanguageRegistry registry;
    return registry;
  }

  // Registers a strategy factory for a given extension or name.
  void Register(absl::string_view name, StrategyFactory factory) {
    factories_[std::string(name)] = std::move(factory);
  }

  // Creates a strategy for a given filename or extension.
  absl::StatusOr<std::unique_ptr<ExecutionStrategy>> CreateStrategy(
      absl::string_view filename_or_extension,
      std::shared_ptr<CacheInterface> cache) const {
    // Try exact match first
    auto it = factories_.find(std::string(filename_or_extension));
    if (it != factories_.end()) {
      return it->second(cache);
    }

    // Try extension match
    size_t last_dot = filename_or_extension.find_last_of('.');
    if (last_dot != absl::string_view::npos) {
      auto ext_it = factories_.find(std::string(filename_or_extension.substr(last_dot)));
      if (ext_it != factories_.end()) {
        return ext_it->second(cache);
      }
    }

    return absl::NotFoundError(absl::StrCat("No strategy registered for: ", filename_or_extension));
  }

 private:
  LanguageRegistry() = default;
  std::unordered_map<std::string, StrategyFactory> factories_;
};

}  // namespace dcodex

#endif  // SRC_ENGINE_LANGUAGE_REGISTRY_H_
