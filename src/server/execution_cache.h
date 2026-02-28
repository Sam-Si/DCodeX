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

#include <cstdint>
#include <memory>

#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
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

// Thread-safe LRU cache with TTL support for code execution results.
class ExecutionCache {
 public:
  // Default TTL: 1 hour, max entries: 1000.
  explicit ExecutionCache(absl::Duration ttl = absl::Hours(1),
                          size_t max_entries = 1000);

  // Gets cached result if available and not expired.
  // Returns nullptr if not found or expired.
  std::shared_ptr<const CachedResult> Get(absl::string_view code_hash);

  // Stores result in cache.
  void Put(absl::string_view code_hash, const CachedResult& result);

  // Computes hash of code using absl::Hash.
  // Returns error status if hash computation fails.
  static absl::StatusOr<std::string> ComputeHash(absl::string_view code);

  // Clears all expired entries.
  void CleanupExpired();

  // Clears entire cache.
  void Clear();

  // Gets cache statistics.
  size_t Size() const;

 private:
  mutable absl::Mutex mutex_;
  absl::flat_hash_map<std::string, std::shared_ptr<CachedResult>> cache_
      ABSL_GUARDED_BY(mutex_);
  const absl::Duration ttl_;
  const size_t max_entries_;

  // LRU tracking using inlined vector for better cache locality.
  absl::InlinedVector<std::string, 16> access_order_ ABSL_GUARDED_BY(mutex_);

  void EvictIfNeeded() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  bool IsExpired(const CachedResult& result) const;
};

}  // namespace dcodex

#endif  // SRC_SERVER_EXECUTION_CACHE_H_
