// Standard C++ Memory Exhaustion Example
// This program tries to allocate more memory than the sandbox allows.
// The sandbox has a 25 MB memory limit (see SandboxLimits in src/server/sandbox.h).

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

namespace {

class MemoryProfileStrategy {
 public:
  virtual ~MemoryProfileStrategy() = default;
  virtual std::size_t ChunkSizeBytes() const = 0;
  virtual const char* ProfileName() const = 0;
};

class MacArmMemoryProfile final : public MemoryProfileStrategy {
 public:
  std::size_t ChunkSizeBytes() const override { return 1 * 1024 * 1024; }
  const char* ProfileName() const override { return "macOS arm64"; }
};

class DefaultMemoryProfile final : public MemoryProfileStrategy {
 public:
  std::size_t ChunkSizeBytes() const override { return 2 * 1024 * 1024; }
  const char* ProfileName() const override { return "default"; }
};

std::unique_ptr<MemoryProfileStrategy> MakeMemoryProfile() {
#if defined(__APPLE__) && defined(__aarch64__)
  return std::make_unique<MacArmMemoryProfile>();
#else
  return std::make_unique<DefaultMemoryProfile>();
#endif
}

}  // namespace

int main() {
  std::cout << "Memory allocation test" << std::endl;

  std::vector<std::vector<char>> allocations;
  auto profile = MakeMemoryProfile();
  const std::size_t chunk_size = profile->ChunkSizeBytes();

  std::cout << "Using memory profile: " << profile->ProfileName() << std::endl;

  int chunk_count = 0;

  try {
    while (true) {
      allocations.emplace_back(chunk_size, 'X');
      chunk_count++;
      std::cout << "Allocated " << (chunk_count * chunk_size / (1024 * 1024))
                << " MB" << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  } catch (const std::bad_alloc& e) {
    std::cerr << "Memory limit exceeded: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}