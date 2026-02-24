// Standard C++ Memory Exhaustion Example
// This program tries to allocate more memory than the sandbox allows
// The sandbox has a 50 MB memory limit

#include <iostream>
#include <vector>
#include <cstddef>

int main() {
    std::cout << "Memory Exhaustion Test" << std::endl;
    std::cout << "======================" << std::endl;
    std::cout << "Sandbox memory limit: 50 MB" << std::endl;
    std::cout << "Attempting to allocate large amounts of memory..." << std::endl;
    
    std::vector<std::vector<char>> allocations;
    const std::size_t chunk_size = 10 * 1024 * 1024;  // 10 MB per chunk
    
    try {
        for (int i = 1; ; ++i) {
            std::cout << "Allocating chunk " << i << " (" << chunk_size / (1024 * 1024) << " MB)..." << std::endl;
            allocations.emplace_back(chunk_size, 'X');
            std::cout << "Total allocated: " << (i * chunk_size / (1024 * 1024)) << " MB" << std::endl;
        }
    } catch (const std::bad_alloc& e) {
        std::cerr << "Memory allocation failed: " << e.what() << std::endl;
    }
    
    // This line may or may not be reached depending on how the sandbox handles OOM
    std::cout << "Program reached end (may have been terminated by sandbox)" << std::endl;
    
    return 0;
}