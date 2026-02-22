#include "src/server/execution_cache.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace dcodex {

namespace {

// Convert bytes to hex string
std::string BytesToHex(const unsigned char* data, size_t len) {
  std::stringstream ss;
  ss << std::hex << std::setfill('0');
  for (size_t i = 0; i < len; ++i) {
    ss << std::setw(2) << static_cast<int>(data[i]);
  }
  return ss.str();
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

  // Check if expired
  if (IsExpired(*it->second)) {
    return nullptr;
  }

  // Update access order for LRU (promote to front)
  auto order_it = std::find(access_order_.begin(), access_order_.end(), code_hash);
  if (order_it != access_order_.end()) {
    access_order_.erase(order_it);
  }
  access_order_.push_back(code_hash);

  return it->second;
}

void ExecutionCache::Put(const std::string& code_hash,
                         const CachedResult& result) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  // Remove old entry if exists (to update access order)
  auto it = cache_.find(code_hash);
  if (it != cache_.end()) {
    auto order_it = std::find(access_order_.begin(), access_order_.end(), code_hash);
    if (order_it != access_order_.end()) {
      access_order_.erase(order_it);
    }
  }

  // Evict if needed before inserting
  EvictIfNeeded();

  // Insert new entry
  auto cached = std::make_shared<CachedResult>(result);
  cached->timestamp = std::chrono::steady_clock::now();
  cache_[code_hash] = cached;
  access_order_.push_back(code_hash);
}

std::string ExecutionCache::ComputeHash(const std::string& code) {
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (!ctx) {
    return "";
  }

  if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
    EVP_MD_CTX_free(ctx);
    return "";
  }

  if (EVP_DigestUpdate(ctx, code.data(), code.size()) != 1) {
    EVP_MD_CTX_free(ctx);
    return "";
  }

  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len = 0;

  if (EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1) {
    EVP_MD_CTX_free(ctx);
    return "";
  }

  EVP_MD_CTX_free(ctx);

  return BytesToHex(hash, hash_len);
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
    auto order_it = std::find(access_order_.begin(), access_order_.end(), hash);
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
