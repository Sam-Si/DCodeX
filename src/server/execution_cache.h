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

#ifndef SRC_SERVER_EXECUTION_CACHE_H_
#define SRC_SERVER_EXECUTION_CACHE_H_

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dcodex {

// Cached execution result with metadata.
struct CachedResult {
  std::string stdout_output;
  std::string stderr_output;
  int64_t peak_memory_bytes = 0;
  float execution_time_ms = 0.0f;
  bool success = false;
  std::string error_message;
  std::chrono::steady_clock::time_point timestamp;
};

// Thread-safe LRU cache with TTL support for code execution results.
class ExecutionCache {
 public:
  // Default TTL: 1 hour, max entries: 1000.
  explicit ExecutionCache(std::chrono::seconds ttl = std::chrono::hours(1),
                         size_t max_entries = 1000);

  ~ExecutionCache() = default;

  // Disable copy and move.
  ExecutionCache(const ExecutionCache&) = delete;
  ExecutionCache& operator=(const ExecutionCache&) = delete;

  // Gets cached result if available and not expired.
  // Returns nullptr if not found or expired.
  std::shared_ptr<const CachedResult> Get(
      const std::string& code_hash) const;

  // Stores result in cache.
  void Put(const std::string& code_hash, const CachedResult& result);

  // Computes hash of code using FNV-1a algorithm.
  static std::string ComputeHash(const std::string& code);

  // Clears all expired entries.
  void CleanupExpired();

  // Clears entire cache.
  void Clear();

  // Gets cache statistics.
  size_t Size() const;

 private:
  mutable std::shared_mutex mutex_;
  std::unordered_map<std::string, std::shared_ptr<CachedResult>> cache_;
  std::chrono::seconds ttl_;
  size_t max_entries_;

  // LRU tracking.
  mutable std::vector<std::string> access_order_;

  void EvictIfNeeded();
  bool IsExpired(const CachedResult& result) const;
};

}  // namespace dcodex

#endif  // SRC_SERVER_EXECUTION_CACHE_H_
