// Standard C++ Time Limit Exceeded Example
// This program runs a long computation with periodic I/O to simulate real workload
// The sandbox has a 2-second CPU time limit

#include <chrono>
#include <iostream>
#include <thread>

int main() {
  std::cout << "Processing..." << std::endl;

  volatile long counter = 0;

  // Long computation with periodic sleeps - will exceed CPU time limit
  while (true) {
    // Simulate computational work
    for (volatile int i = 0; i < 1000000; ++i) {
      counter++;
    }

    // Periodic sleep to simulate realistic workload
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  return 0;
}