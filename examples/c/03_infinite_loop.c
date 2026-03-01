// Bounded Loop Example in C
// Demonstrates a CPU-intensive bounded loop for testing
#include <stdio.h>
#include <stdint.h>

int main(void) {
    printf("Starting bounded CPU-intensive loop (max 100000000 iterations)...\n");
    
    volatile uint64_t counter = 0;
    const uint64_t kMaxIterations = 100000000ULL;
    
    for (uint64_t i = 0; i < kMaxIterations; ++i) {
        // Busy wait to consume CPU
        ++counter;
    }
    
    printf("Completed %llu iterations.\n", (unsigned long long)counter);
    return 0;
}
