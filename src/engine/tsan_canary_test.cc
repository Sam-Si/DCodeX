#include <gtest/gtest.h>
#include <thread>
#include <atomic>

TEST(TsanCanary, AtomicCheck) {
  std::atomic<int> counter{0};
  
  std::thread t1([&]() {
    for (int i = 0; i < 1000; ++i) {
      counter.fetch_add(1, std::memory_order_relaxed);
    }
  });
  
  std::thread t2([&]() {
    for (int i = 0; i < 1000; ++i) {
      counter.fetch_add(1, std::memory_order_relaxed);
    }
  });
  
  t1.join();
  t2.join();
  
  EXPECT_EQ(counter.load(), 2000);
}
