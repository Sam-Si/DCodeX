// Standard C++ Memory Allocation Demo
// Uses only standard library headers

#include <iostream>
#include <vector>
#include <memory>

int main() {
    std::cout << "Memory Allocation Demo" << std::endl;
    std::cout << "======================" << std::endl;
    
    // Stack allocation
    int stack_array[1000];
    std::cout << "Stack array of 1000 integers created" << std::endl;
    
    // Heap allocation with new/delete
    int* heap_array = new int[1000];
    std::cout << "Heap array of 1000 integers created" << std::endl;
    
    // Initialize heap array
    for (int i = 0; i < 1000; ++i) {
        heap_array[i] = i * i;
    }
    std::cout << "Heap array initialized with squares" << std::endl;
    
    // Calculate sum
    long long sum = 0;
    for (int i = 0; i < 1000; ++i) {
        sum += heap_array[i];
    }
    std::cout << "Sum of squares (0-999): " << sum << std::endl;
    
    delete[] heap_array;
    std::cout << "Heap memory freed" << std::endl;
    
    // Using std::vector (automatic memory management)
    std::cout << "\nUsing std::vector:" << std::endl;
    std::vector<int> vec;
    const int count = 100000;
    
    for (int i = 0; i < count; ++i) {
        vec.push_back(i);
    }
    
    std::cout << "Vector size: " << vec.size() << std::endl;
    std::cout << "Memory used: ~" << (vec.size() * sizeof(int) / 1024) << " KB" << std::endl;
    
    // Smart pointer example
    std::cout << "\nUsing smart pointers:" << std::endl;
    std::unique_ptr<int[]> smart_array = std::make_unique<int[]>(100);
    smart_array[0] = 42;
    smart_array[99] = 99;
    std::cout << "Smart pointer array created and initialized" << std::endl;
    std::cout << "First element: " << smart_array[0] << std::endl;
    std::cout << "Last element: " << smart_array[99] << std::endl;
    // Memory automatically freed when smart_array goes out of scope
    
    return 0;
}