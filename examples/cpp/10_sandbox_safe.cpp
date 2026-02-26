// Standard C++ Sandbox-Safe Program
// Uses only standard library headers
// This program stays well within sandbox resource limits

#include <iostream>
#include <vector>
#include <string>

int main() {
    std::cout << "Sandbox-Safe Program" << std::endl;
    std::cout << "====================" << std::endl;
    
    // Small memory allocation (well within limits)
    std::vector<int> data(1000);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<int>(i * i);
    }
    
    std::cout << "Created vector with " << data.size() << " elements" << std::endl;
    std::cout << "Memory used: ~" << (data.size() * sizeof(int) / 1024) << " KB" << std::endl;
    
    // Quick computation
    long long sum = 0;
    for (int val : data) {
        sum += val;
    }
    std::cout << "Sum of squares: " << sum << std::endl;
    
    // String operations
    std::string message = "This program runs safely within sandbox limits!";
    std::cout << "\nMessage: " << message << std::endl;
    std::cout << "Message length: " << message.length() << " characters" << std::endl;
    
    // Simple loop
    std::cout << "\nCounting to 10:" << std::endl;
    for (int i = 1; i <= 10; ++i) {
        std::cout << i << " ";
    }
    std::cout << std::endl;
    
    std::cout << "\nProgram completed successfully!" << std::endl;
    
    return 0;
}