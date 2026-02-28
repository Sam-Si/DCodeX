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

#include "absl/algorithm/container.h"
#include "absl/hash/hash.h"
#include "absl/strings/str_format.h"

namespace dcodex {

namespace {

// Converts hash to hex string.
std::string HashToHex(size_t hash) {
  return absl::StrFormat("%016x", hash);
}

}  // namespace

ExecutionCache::ExecutionCache(absl::Duration ttl, size_t max_entries)
    : ttl_(ttl), max_entries_(max_entries) {}

std::shared_ptr<const CachedResult> ExecutionCache::Get(
    absl::string_view code_hash) {
  absl::MutexLock lock(&mutex_);

  auto it = cache_.find(code_hash);
  if (it == cache_.end()) {
    return nullptr;
  }

  // Checks if expired.
  if (IsExpired(*it->second)) {
    return nullptr;
  }

  // Updates access order for LRU (promote to front).
  auto order_it = absl::c_find(access_order_, code_hash);
  if (order_it != access_order_.end()) {
    access_order_.erase(order_it);
  }
  access_order_.push_back(std::string(code_hash));

  return it->second;
}

void ExecutionCache::Put(absl::string_view code_hash,
                         const CachedResult& result) {
  absl::MutexLock lock(&mutex_);

  // Removes old entry if exists (to update access order).
  auto it = cache_.find(code_hash);
  if (it != cache_.end()) {
    auto order_it = absl::c_find(access_order_, code_hash);
    if (order_it != access_order_.end()) {
      access_order_.erase(order_it);
    }
  }

  // Evicts if needed before inserting.
  EvictIfNeeded();

  // Inserts new entry.
  auto cached = std::make_shared<CachedResult>(result);
  cached->timestamp = absl::Now();
  cache_[std::string(code_hash)] = cached;
  access_order_.push_back(std::string(code_hash));
}

absl::StatusOr<std::string> ExecutionCache::ComputeHash(
    absl::string_view code) {
  if (code.empty()) {
    return absl::InvalidArgumentError("Code cannot be empty");
  }
  size_t hash = absl::Hash<absl::string_view>{}(code);
  return HashToHex(hash);
}

void ExecutionCache::CleanupExpired() {
  absl::MutexLock lock(&mutex_);

  auto now = absl::Now();
  absl::InlinedVector<std::string, 16> to_remove;

  for (const auto& [hash, result] : cache_) {
    if (now - result->timestamp > ttl_) {
      to_remove.push_back(hash);
    }
  }

  for (const auto& hash : to_remove) {
    cache_.erase(hash);
    auto order_it = absl::c_find(access_order_, hash);
    if (order_it != access_order_.end()) {
      access_order_.erase(order_it);
    }
  }
}

void ExecutionCache::Clear() {
  absl::MutexLock lock(&mutex_);
  cache_.clear();
  access_order_.clear();
}

size_t ExecutionCache::Size() const {
  absl::MutexLock lock(&mutex_);
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
  auto now = absl::Now();
  return (now - result.timestamp) > ttl_;
}

}  // namespace dcodex
