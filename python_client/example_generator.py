# Copyright 2024 DCodeX Team
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Example code generation for DCodeX Python Client."""

from pathlib import Path


class ExampleGenerator:
    """Generates default example code files."""

    # Default example code templates
    _EXAMPLES: dict[str, str] = {
        "hello_world": """#include <iostream>

int main() {
    std::cout << "Hello, World!" << std::endl;
    for (int i = 0; i < 5; i++) {
        std::cout << "Count: " << i << std::endl;
    }
    return 0;
}
""",
        "fibonacci": """#include <iostream>
#include <cmath>

int main() {
    std::cout << "Computing fibonacci sequence..." << std::endl;
    long long a = 0, b = 1;
    std::cout << "Fibonacci: " << a;
    for (int i = 0; i < 40; i++) {
        std::cout << ", " << b;
        long long temp = a + b;
        a = b;
        b = temp;
    }
    std::cout << std::endl;
    return 0;
}
""",
        "squares": """#include <iostream>
int main() {
    std::cout << "Squares of 1-10:" << std::endl;
    for (int i = 1; i <= 10; i++) {
        std::cout << i << "² = " << (i * i) << std::endl;
    }
    return 0;
}
""",
        "cubes": """#include <iostream>
int main() {
    std::cout << "Cubes of 1-10:" << std::endl;
    for (int i = 1; i <= 10; i++) {
        std::cout << i << "³ = " << (i * i * i) << std::endl;
    }
    return 0;
}
""",
        "computation": """#include <iostream>
#include <cmath>

int main() {
    std::cout << "Starting heavy computation..." << std::endl;
    double result = 0.0;
    for (int i = 1; i <= 5000000; i++) {
        result += std::sqrt(static_cast<double>(i));
    }
    std::cout << "Sum of square roots: " << result << std::endl;
    return 0;
}
""",
        "memory_test": """#include <iostream>
#include <vector>

int main() {
    std::cout << "Testing memory allocation..." << std::endl;
    std::vector<int> data;
    const int count = 2000000;
    for (int i = 0; i < count; i++) {
        data.push_back(i);
    }
    std::cout << "Allocated " << (data.size() * sizeof(int) / (1024*1024)) << " MB" << std::endl;
    std::cout << "Memory test complete!" << std::endl;
    return 0;
}
""",
        "sandbox_safe": """#include <iostream>
#include <vector>

int main() {
    std::cout << "Running within sandbox limits..." << std::endl;
    std::vector<int> data(10000);
    for (size_t i = 0; i < data.size(); i++) {
        data[i] = i * i;
    }
    std::cout << "Processed " << data.size() << " elements safely." << std::endl;
    return 0;
}
""",
    }

    def create_examples_directory(self, directory: Path) -> None:
        """Create default example code files in the specified directory.

        Args:
            directory: Path to create the examples directory.
        """
        directory.mkdir(parents=True, exist_ok=True)

        for name, code in self._EXAMPLES.items():
            file_path = directory / f"{name}.cpp"
            if not file_path.exists():
                with open(file_path, "w", encoding="utf-8") as f:
                    f.write(code)
                print(f"Created: {file_path}")
