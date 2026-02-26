// Standard C++ Memory Exhaustion Example
// This program tries to allocate more memory than the sandbox allows
// The sandbox has a 50 MB memory limit

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

int main() {
  std::cout << "Memory allocation test" << std::endl;

  std::vector<std::vector<char>> allocations;
#if defined(__APPLE__) && defined(__aarch64__)
  const std::size_t chunk_size = 1 * 1024 * 1024;  // 1 MB per chunk for macOS M1
#else
  const std::size_t chunk_size = 2 * 1024 * 1024;  // 2 MB per chunk for other platforms
#endif

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