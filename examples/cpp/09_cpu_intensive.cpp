// Standard C++ CPU-Intensive Computation
// Uses only standard library headers

#include <iostream>
#include <cmath>
#include <chrono>

int main() {
    std::cout << "CPU-Intensive Computation" << std::endl;
    std::cout << "=========================" << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Heavy computation: Calculate sum of square roots
    const int iterations = 10000000;
    double result = 0.0;
    
    std::cout << "Calculating sum of square roots from 1 to " << iterations << "..." << std::endl;
    
    for (int i = 1; i <= iterations; ++i) {
        result += std::sqrt(static_cast<double>(i));
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Result: " << result << std::endl;
    std::cout << "Computation completed in " << duration.count() << " ms" << std::endl;
    
    // Additional computation: Matrix multiplication simulation
    std::cout << "\nSimulating matrix operations..." << std::endl;
    const int size = 500;
    double sum = 0.0;
    
    for (int i = 0; i < size; ++i) {
        for (int j = 0; j < size; ++j) {
            sum += std::sin(i) * std::cos(j);
        }
    }
    
    std::cout << "Matrix operation result: " << sum << std::endl;
    
    return 0;
}