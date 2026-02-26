// Standard C++ Fibonacci Sequence
// Uses only standard library headers

#include <iostream>

int main() {
    std::cout << "Fibonacci Sequence Generator" << std::endl;
    std::cout << "=============================" << std::endl;
    
    const int n = 40;
    long long a = 0, b = 1;
    
    std::cout << "First " << n << " Fibonacci numbers:" << std::endl;
    std::cout << a;
    
    for (int i = 1; i < n; ++i) {
        std::cout << ", " << b;
        long long temp = a + b;
        a = b;
        b = temp;
    }
    
    std::cout << std::endl;
    std::cout << "Final number: " << b << std::endl;
    
    return 0;
}