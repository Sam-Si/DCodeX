// Bounded Memory Allocation Example in C
// Demonstrates controlled memory allocation up to a bounded limit
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int main(void) {
    printf("Allocating memory with bounded limit (max 100000000 iterations)...\n");
    
    const size_t kChunkSize = 1024ULL * 1024ULL * 100ULL; // 100MB
    const uint64_t kMaxIterations = 100000000ULL;
    uint64_t iteration = 0;
    
    while (iteration < kMaxIterations) {
        void* ptr = malloc(kChunkSize);
        if (ptr == NULL) {
            printf("Allocation failed after %llu iterations!\n", (unsigned long long)iteration);
            return 1;
        }
        memset(ptr, 0, kChunkSize);
        printf("Allocated 100MB (iteration %llu)...\n", (unsigned long long)iteration);
        
        ++iteration;
    }
    
    printf("Reached iteration limit of %llu.\n", (unsigned long long)kMaxIterations);
    return 0;
}
