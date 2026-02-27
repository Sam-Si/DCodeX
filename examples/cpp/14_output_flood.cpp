// Standard C++ Output Flood Example
// This program prints a very long string in an infinite loop to demonstrate
// the sandbox's 10 KB combined output size limit.
//
// Each iteration prints a ~500-character line, so the 10 KB cap is crossed
// after roughly 20 iterations.  The sandbox kills the process at that point,
// appends a truncation notice, and sets output_truncated = true in the result.
// The wall-clock timeout (5 s) is NOT what stops this program — the output
// limit fires first, well under 1 second.

#include <iostream>

int main() {
  std::cout << "Output Flood Demo — printing a large string in an infinite loop.\n";
  std::cout << "Sandbox output limit: 10 KB (combined stdout+stderr).\n";
  std::cout << "The sandbox will kill this process once the limit is exceeded.\n";
  std::cout << "---\n";

  // A single essay-style sentence repeated to form a ~500-character line.
  // This simulates a user pasting a long passage and printing it in a loop.
  const std::string long_line =
      "The quick brown fox jumps over the lazy dog near the riverbank while "
      "the sun sets slowly behind the distant mountains, casting long golden "
      "shadows across the meadow where children play and birds sing their "
      "evening songs, filling the warm summer air with a sense of peaceful "
      "tranquility that only nature can provide on such a beautiful day. "
      "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do "
      "eiusmod tempor incididunt ut labore et dolore magna aliqua, and the "
      "story continues without end, growing larger with every iteration.\n";
  // long_line is ~500 bytes.  After ~21 iterations the running total crosses
  // 10 KB and the sandbox terminates this process immediately.

  int iteration = 0;
  while (true) {
    ++iteration;
    std::cout << "[iter " << iteration << "] " << long_line;
  }

  // Unreachable — the sandbox kills us before we ever return.
  return 0;
}
