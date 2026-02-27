// Standard C++ Stdin Input Example
// Demonstrates reading from standard input (stdin) via std::cin.
// When submitted through DCodeX, the stdin_data field in the CodeRequest
// is piped directly into this program's stdin.
//
// Expected stdin format:
//   Line 1: a name (string)
//   Line 2: N (count of numbers)
//   Lines 3..N+2: one integer per line
//
// Example stdin_data:
//   "DCodeX\n5\n10\n20\n30\n40\n50\n"

#include <iostream>
#include <numeric>
#include <string>
#include <vector>

int main() {
    // Read name from first line of stdin.
    std::string name;
    std::getline(std::cin, name);

    std::cout << "Hello, " << name << "!" << std::endl;
    std::cout << "=========================" << std::endl;

    // Read how many numbers follow.
    int count = 0;
    std::cin >> count;

    if (count <= 0) {
        std::cout << "No numbers provided." << std::endl;
        return 0;
    }

    // Read each number.
    std::vector<int> numbers;
    numbers.reserve(count);
    for (int i = 0; i < count; ++i) {
        int n = 0;
        std::cin >> n;
        numbers.push_back(n);
    }

    // Print the numbers.
    std::cout << "You provided " << numbers.size() << " number(s):" << std::endl;
    for (int i = 0; i < static_cast<int>(numbers.size()); ++i) {
        std::cout << "  [" << (i + 1) << "] " << numbers[i] << std::endl;
    }

    // Compute and print statistics.
    int sum = std::accumulate(numbers.begin(), numbers.end(), 0);
    int min = *std::min_element(numbers.begin(), numbers.end());
    int max = *std::max_element(numbers.begin(), numbers.end());
    double avg = static_cast<double>(sum) / static_cast<double>(numbers.size());

    std::cout << std::endl;
    std::cout << "Statistics:" << std::endl;
    std::cout << "  Sum     : " << sum << std::endl;
    std::cout << "  Minimum : " << min << std::endl;
    std::cout << "  Maximum : " << max << std::endl;
    std::cout << "  Average : " << avg << std::endl;

    return 0;
}
