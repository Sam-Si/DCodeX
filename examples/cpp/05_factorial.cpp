// Standard C++ Factorial Calculator
// Uses only standard library headers

#include <iostream>

unsigned long long factorial(int n) {
    if (n <= 1) return 1;
    unsigned long long result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

int main() {
    std::cout << "Factorial Calculator" << std::endl;
    std::cout << "=====================" << std::endl;
    
    std::cout << "Factorials from 0 to 20:" << std::endl;
    for (int i = 0; i <= 20; ++i) {
        std::cout << i << "! = " << factorial(i) << std::endl;
    }
    
    return 0;
}