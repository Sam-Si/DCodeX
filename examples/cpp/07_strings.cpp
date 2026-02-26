// Standard C++ String Operations
// Uses only standard library headers

#include <iostream>
#include <string>
#include <algorithm>
#include <cctype>

int main() {
    std::cout << "String Operations Demo" << std::endl;
    std::cout << "======================" << std::endl;
    
    std::string str = "Hello, DCodeX World!";
    
    std::cout << "Original string: " << str << std::endl;
    std::cout << "Length: " << str.length() << std::endl;
    std::cout << "First character: " << str[0] << std::endl;
    std::cout << "Last character: " << str.back() << std::endl;
    
    // Substring
    std::cout << "\nSubstring (7, 6): " << str.substr(7, 6) << std::endl;
    
    // Find
    size_t pos = str.find("World");
    if (pos != std::string::npos) {
        std::cout << "'World' found at position: " << pos << std::endl;
    }
    
    // Replace
    std::string replaced = str;
    replaced.replace(pos, 5, "Universe");
    std::cout << "After replace: " << replaced << std::endl;
    
    // Case conversion
    std::string upper = str;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    std::cout << "Uppercase: " << upper << std::endl;
    
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    std::cout << "Lowercase: " << lower << std::endl;
    
    // Concatenation
    std::string greeting = "Welcome ";
    greeting += str;
    std::cout << "\nConcatenated: " << greeting << std::endl;
    
    return 0;
}