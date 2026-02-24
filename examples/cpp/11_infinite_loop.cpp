// Standard C++ Infinite Loop Example
// This program runs an infinite loop that will be terminated by the sandbox
// The sandbox has a 2-second CPU time limit

#include <iostream>

int main() {
    std::cout << "Starting infinite loop..." << std::endl;
    std::cout << "This program will be terminated by the sandbox timeout." << std::endl;
    std::cout << "Sandbox CPU limit: 2 seconds" << std::endl;
    
    volatile int counter = 0;
    
    // Infinite loop - will exceed CPU time limit
    while (true) {
        counter++;
        if (counter % 1000000 == 0) {
            std::cout << "Iteration: " << counter << std::endl;
        }
    }
    
    // This line will never be reached
    std::cout << "Program completed (should not see this)" << std::endl;
    
    return 0;
}