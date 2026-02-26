// Standard C++ Basic Math Operations
// Uses only standard library headers

#include <iostream>
#include <cmath>

int main() {
    std::cout << "Basic Math Operations" << std::endl;
    std::cout << "=====================" << std::endl;
    
    // Basic arithmetic
    int a = 15, b = 4;
    std::cout << "a = " << a << ", b = " << b << std::endl;
    std::cout << "Addition: " << (a + b) << std::endl;
    std::cout << "Subtraction: " << (a - b) << std::endl;
    std::cout << "Multiplication: " << (a * b) << std::endl;
    std::cout << "Division: " << (a / b) << std::endl;
    std::cout << "Modulo: " << (a % b) << std::endl;
    
    // Using cmath functions
    std::cout << "\nSquare root of 16: " << std::sqrt(16.0) << std::endl;
    std::cout << "Power (2^8): " << std::pow(2.0, 8.0) << std::endl;
    std::cout << "Absolute value of -42: " << std::abs(-42) << std::endl;
    
    return 0;
}