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

#ifndef SRC_COMMON_EXECUTION_CACHE_H_
#define SRC_COMMON_EXECUTION_CACHE_H_

#include <cstdint>
#include <list>
#include <memory>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

namespace dcodex {

// Cached execution result with metadata.
struct CachedResult {
  std::string stdout_output;
  std::string stderr_output;
  int64_t peak_memory_bytes = 0;
  float execution_time_ms = 0.0f;
  bool success = false;
  std::string error_message;
  absl::Time timestamp;
};

// =============================================================================
// CacheInterface: Abstract base class for cache implementations.
// Follows the Dependency Inversion Principle - high-level modules depend
// on this abstraction, not concrete implementations.
// =============================================================================
class CacheInterface {
 public:
  virtual ~CacheInterface() = default;

  // Gets cached result if available and not expired.
  // Returns nullptr if not found or expired.
  [[nodiscard]] virtual std::shared_ptr<const CachedResult> Get(
      absl::string_view code_hash) = 0;

  // Stores result in cache.
  virtual void Put(absl::string_view code_hash, const CachedResult& result) = 0;

  // Computes hash of code using absl::Hash.
  // Returns error status if hash computation fails.
  [[nodiscard]] static absl::StatusOr<std::string> ComputeHash(
      absl::string_view code);

  // Clears all expired entries.
  virtual void CleanupExpired() = 0;

  // Clears entire cache.
  virtual void Clear() = 0;

  // Cache statistics.
  struct CacheStats {
    size_t size;
    int64_t hits;
    int64_t misses;
  };

  // Gets cache statistics.
  [[nodiscard]] virtual CacheStats GetStats() const = 0;
};

// Thread-safe LRU cache with TTL support for code execution results.
class ExecutionCache : public CacheInterface {
 public:
  // Default TTL: 1 hour, max entries: 1000.
  explicit ExecutionCache(absl::Duration ttl = absl::Hours(1),
                          size_t max_entries = 1000);

  // Gets cached result if available and not expired.
  // Returns nullptr if not found or expired.
  [[nodiscard]] std::shared_ptr<const CachedResult> Get(
      absl::string_view code_hash) override;

  // Stores result in cache.
  void Put(absl::string_view code_hash, const CachedResult& result) override;

  // Clears all expired entries.
  void CleanupExpired() override;

  // Clears entire cache.
  void Clear() override;

  // Gets cache statistics.
  [[nodiscard]] CacheStats GetStats() const override;

 private:
  // LRU list type: stores hash keys. Front = most recently used.
  using LruList = std::list<std::string>;
  using LruIterator = LruList::iterator;

  // Cache entry containing the result and iterator to the LRU list node.
  struct CacheEntry {
    std::shared_ptr<CachedResult> result;
    LruIterator lru_iterator;
  };

  mutable absl::Mutex mutex_;
  absl::flat_hash_map<std::string, CacheEntry> cache_ ABSL_GUARDED_BY(mutex_);
  LruList lru_list_ ABSL_GUARDED_BY(mutex_);
  const absl::Duration ttl_;
  const size_t max_entries_;
  mutable int64_t hits_ ABSL_GUARDED_BY(mutex_) = 0;
  mutable int64_t misses_ ABSL_GUARDED_BY(mutex_) = 0;

  void EvictIfNeeded() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  [[nodiscard]] bool IsExpired(const CachedResult& result) const;
};

}  // namespace dcodex

#endif  // SRC_COMMON_EXECUTION_CACHE_H_
