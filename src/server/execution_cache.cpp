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

#include "src/server/execution_cache.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace dcodex {

namespace {

// FNV-1a hash constants.
constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

// Converts 64-bit hash to hex string.
std::string HashToHex(uint64_t hash) {
  std::stringstream ss;
  ss << std::hex << std::setfill('0') << std::setw(16) << hash;
  return ss.str();
}

// Simple FNV-1a hash function - fast and good distribution.
uint64_t Fnv1aHash(const std::string& data) {
  uint64_t hash = kFnvOffsetBasis;
  for (unsigned char c : data) {
    hash ^= static_cast<uint64_t>(c);
    hash *= kFnvPrime;
  }
  return hash;
}

}  // namespace

ExecutionCache::ExecutionCache(std::chrono::seconds ttl, size_t max_entries)
    : ttl_(ttl), max_entries_(max_entries) {}

std::shared_ptr<const CachedResult> ExecutionCache::Get(
    const std::string& code_hash) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);

  auto it = cache_.find(code_hash);
  if (it == cache_.end()) {
    return nullptr;
  }

  // Checks if expired.
  if (IsExpired(*it->second)) {
    return nullptr;
  }

  // Updates access order for LRU (promote to front).
  auto order_it =
      std::find(access_order_.begin(), access_order_.end(), code_hash);
  if (order_it != access_order_.end()) {
    access_order_.erase(order_it);
  }
  access_order_.push_back(code_hash);

  return it->second;
}

void ExecutionCache::Put(const std::string& code_hash,
                         const CachedResult& result) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  // Removes old entry if exists (to update access order).
  auto it = cache_.find(code_hash);
  if (it != cache_.end()) {
    auto order_it =
        std::find(access_order_.begin(), access_order_.end(), code_hash);
    if (order_it != access_order_.end()) {
      access_order_.erase(order_it);
    }
  }

  // Evicts if needed before inserting.
  EvictIfNeeded();

  // Inserts new entry.
  auto cached = std::make_shared<CachedResult>(result);
  cached->timestamp = std::chrono::steady_clock::now();
  cache_[code_hash] = cached;
  access_order_.push_back(code_hash);
}

std::string ExecutionCache::ComputeHash(const std::string& code) {
  uint64_t hash = Fnv1aHash(code);
  return HashToHex(hash);
}

void ExecutionCache::CleanupExpired() {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  auto now = std::chrono::steady_clock::now();
  std::vector<std::string> to_remove;

  for (const auto& [hash, result] : cache_) {
    if (now - result->timestamp > ttl_) {
      to_remove.push_back(hash);
    }
  }

  for (const auto& hash : to_remove) {
    cache_.erase(hash);
    auto order_it =
        std::find(access_order_.begin(), access_order_.end(), hash);
    if (order_it != access_order_.end()) {
      access_order_.erase(order_it);
    }
  }
}

void ExecutionCache::Clear() {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  cache_.clear();
  access_order_.clear();
}

size_t ExecutionCache::Size() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return cache_.size();
}

void ExecutionCache::EvictIfNeeded() {
  while (cache_.size() >= max_entries_ && !access_order_.empty()) {
    const std::string& oldest = access_order_.front();
    cache_.erase(oldest);
    access_order_.erase(access_order_.begin());
  }
}

bool ExecutionCache::IsExpired(const CachedResult& result) const {
  auto now = std::chrono::steady_clock::now();
  return (now - result.timestamp) > ttl_;
}

}  // namespace dcodex
