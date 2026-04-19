#include <thread>
#include <vector>
#include <iostream>
#include "gtest/gtest.h"

// This test contains a deliberate data race to verify that ThreadSanitizer
// is correctly configured and detecting concurrency issues.
TEST(TSanCanaryTest, DeliberateDataRace) {
  int shared_counter = 0;
  
  std::vector<std::thread> threads;
  for (int i = 0; i < 2; ++i) {
    threads.emplace_back([&shared_counter]() {
      for (int j = 0; j < 100000; ++j) {
        // DATA RACE: Multiple threads incrementing a shared variable
        // without any synchronization (mutex or atomic).
        shared_counter++;
      }
    });
  }
  
  for (auto& t : threads) {
    t.join();
  }
  
  std::cout << "Final counter: " << shared_counter << std::endl;
}
