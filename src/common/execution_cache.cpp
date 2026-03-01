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

#include "src/common/execution_cache.h"

#include <list>

#include "absl/hash/hash.h"
#include "absl/strings/str_format.h"

namespace dcodex {

namespace {

/// Converts a hash value to a hexadecimal string.
[[nodiscard]] std::string HashToHex(size_t hash) {
  return absl::StrFormat("%016x", hash);
}

/// Checks if a cache entry has expired based on TTL.
[[nodiscard]] bool IsEntryExpired(const CachedResult& result,
                                  absl::Duration ttl) {
  return (absl::Now() - result.timestamp) > ttl;
}

}  // namespace

// ==============================================================================
// CacheInterface Implementation
// ==============================================================================

absl::StatusOr<std::string> CacheInterface::ComputeHash(
    absl::string_view code) {
  if (code.empty()) {
    return absl::InvalidArgumentError("Code cannot be empty");
  }
  const size_t hash = absl::Hash<absl::string_view>{}(code);
  return HashToHex(hash);
}

// ==============================================================================
// ExecutionCache Implementation
// ==============================================================================

ExecutionCache::ExecutionCache(absl::Duration ttl, size_t max_entries)
    : ttl_(ttl), max_entries_(max_entries) {}

std::shared_ptr<const CachedResult> ExecutionCache::Get(
    absl::string_view code_hash) {
  absl::MutexLock lock(&mutex_);

  const auto it = cache_.find(code_hash);
  if (it == cache_.end()) {
    return nullptr;
  }

  // Check if expired.
  if (IsEntryExpired(*it->second.result, ttl_)) {
    return nullptr;
  }

  // Promote to front of LRU list (most recently used) using splice for O(1).
  lru_list_.splice(lru_list_.begin(), lru_list_, it->second.lru_iterator);

  return it->second.result;
}

void ExecutionCache::Put(absl::string_view code_hash,
                         const CachedResult& result) {
  absl::MutexLock lock(&mutex_);

  // Remove old entry if exists.
  if (const auto it = cache_.find(code_hash); it != cache_.end()) {
    lru_list_.erase(it->second.lru_iterator);
    cache_.erase(it);
  }

  // Evict if needed before inserting.
  EvictIfNeeded();

  // Insert new entry at the front of the LRU list.
  lru_list_.push_front(std::string(code_hash));

  auto cached = std::make_shared<CachedResult>(result);
  cached->timestamp = absl::Now();

  cache_.emplace(std::string(code_hash),
                 CacheEntry{cached, lru_list_.begin()});
}

void ExecutionCache::CleanupExpired() {
  absl::MutexLock lock(&mutex_);

  // Collect keys to remove.
  std::list<std::string> to_remove;

  for (const auto& [hash, entry] : cache_) {
    if (IsEntryExpired(*entry.result, ttl_)) {
      to_remove.push_back(hash);
    }
  }

  for (const auto& hash : to_remove) {
    if (const auto it = cache_.find(hash); it != cache_.end()) {
      lru_list_.erase(it->second.lru_iterator);
      cache_.erase(it);
    }
  }
}

void ExecutionCache::Clear() {
  absl::MutexLock lock(&mutex_);
  cache_.clear();
  lru_list_.clear();
}

size_t ExecutionCache::Size() const {
  absl::MutexLock lock(&mutex_);
  return cache_.size();
}

void ExecutionCache::EvictIfNeeded() {
  while (cache_.size() >= max_entries_ && !lru_list_.empty()) {
    // Evict from back (least recently used).
    const std::string& oldest_key = lru_list_.back();
    cache_.erase(oldest_key);
    lru_list_.pop_back();
  }
}

bool ExecutionCache::IsExpired(const CachedResult& result) const {
  return IsEntryExpired(result, ttl_);
}

}  // namespace dcodex
