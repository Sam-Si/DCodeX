// Standard C++ Prime Number Generator
// Uses only standard library headers

#include <iostream>
#include <cmath>

bool isPrime(int n) {
    if (n <= 1) return false;
    if (n <= 3) return true;
    if (n % 2 == 0 || n % 3 == 0) return false;
    
    for (int i = 5; i * i <= n; i += 6) {
        if (n % i == 0 || n % (i + 2) == 0)
            return false;
    }
    return true;
}

int main() {
    std::cout << "Prime Number Generator" << std::endl;
    std::cout << "=======================" << std::endl;
    
    const int limit = 100;
    std::cout << "Prime numbers up to " << limit << ":" << std::endl;
    
    int count = 0;
    for (int i = 2; i <= limit; ++i) {
        if (isPrime(i)) {
            std::cout << i << " ";
            ++count;
            if (count % 10 == 0) std::cout << std::endl;
        }
    }
    
    std::cout << std::endl;
    std::cout << "Total primes found: " << count << std::endl;
    
    return 0;
}