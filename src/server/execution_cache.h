#pragma once

#include <openssl/evp.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dcodex {

// Cached execution result with metadata
struct CachedResult {
  std::string stdout_output;
  std::string stderr_output;
  int64_t peak_memory_bytes = 0;
  float execution_time_ms = 0.0f;
  bool success = false;
  std::string error_message;
  std::chrono::steady_clock::time_point timestamp;
};

// Thread-safe LRU cache with TTL support for code execution results
class ExecutionCache {
 public:
  // Default TTL: 1 hour, max entries: 1000
  explicit ExecutionCache(
      std::chrono::seconds ttl = std::chrono::hours(1),
      size_t max_entries = 1000);

  ~ExecutionCache() = default;

  // Disable copy and move
  ExecutionCache(const ExecutionCache&) = delete;
  ExecutionCache& operator=(const ExecutionCache&) = delete;

  // Get cached result if available and not expired
  // Returns nullptr if not found or expired
  std::shared_ptr<const CachedResult> Get(
      const std::string& code_hash) const;

  // Store result in cache
  void Put(const std::string& code_hash,
           const CachedResult& result);

  // Compute SHA256 hash of code
  static std::string ComputeHash(const std::string& code);

  // Clear all expired entries
  void CleanupExpired();

  // Clear entire cache
  void Clear();

  // Get cache statistics
  size_t Size() const;

 private:
  mutable std::shared_mutex mutex_;
  std::unordered_map<std::string, std::shared_ptr<CachedResult>> cache_;
  std::chrono::seconds ttl_;
  size_t max_entries_;

  // LRU tracking
  mutable std::vector<std::string> access_order_;

  void EvictIfNeeded();
  bool IsExpired(const CachedResult& result) const;
};

}  // namespace dcodex
