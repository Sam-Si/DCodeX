// Standard C++ Bounded Computation Example
// This program runs a bounded computation with periodic I/O to simulate real workload
// The loop is capped at 100000000 iterations for safety

#include <cstdint>
#include <chrono>
#include <iostream>
#include <thread>

int main() {
  constexpr uint64_t kMaxIterations = 100000000ULL;
  constexpr int kInnerLoopCount = 1000000;
  
  std::cout << "Processing (max " << kMaxIterations << " iterations)..." << std::endl;

  volatile uint64_t counter = 0;
  uint64_t iteration = 0;

  // Bounded computation with periodic sleeps
  while (iteration < kMaxIterations) {
    // Simulate computational work
    for (volatile int i = 0; i < kInnerLoopCount; ++i) {
      ++counter;
    }

    // Periodic sleep to simulate realistic workload
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ++iteration;
  }

  std::cout << "Completed " << iteration << " iterations." << std::endl;
  return 0;
}