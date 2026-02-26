// Standard C++ Arrays and Vectors Example
// Uses only standard library headers

#include <iostream>
#include <vector>
#include <algorithm>
#include <numeric>

int main() {
    std::cout << "Arrays and Vectors Demo" << std::endl;
    std::cout << "=======================" << std::endl;
    
    // Static array
    int arr[] = {64, 34, 25, 12, 22, 11, 90};
    int n = sizeof(arr) / sizeof(arr[0]);
    
    std::cout << "Original array: ";
    for (int i = 0; i < n; ++i) {
        std::cout << arr[i] << " ";
    }
    std::cout << std::endl;
    
    // Using std::vector
    std::vector<int> vec(arr, arr + n);
    
    std::cout << "Vector size: " << vec.size() << std::endl;
    std::cout << "Vector capacity: " << vec.capacity() << std::endl;
    
    // Sort the vector
    std::sort(vec.begin(), vec.end());
    std::cout << "Sorted vector: ";
    for (size_t i = 0; i < vec.size(); ++i) {
        std::cout << vec[i] << " ";
    }
    std::cout << std::endl;
    
    // Calculate sum
    int sum = std::accumulate(vec.begin(), vec.end(), 0);
    std::cout << "Sum of elements: " << sum << std::endl;
    
    // Find min and max
    auto minmax = std::minmax_element(vec.begin(), vec.end());
    std::cout << "Minimum: " << *minmax.first << std::endl;
    std::cout << "Maximum: " << *minmax.second << std::endl;
    
    return 0;
}