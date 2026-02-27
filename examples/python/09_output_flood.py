#!/usr/bin/env python3
"""
Output Flood Example
This script prints a very long string in an infinite loop to demonstrate
the sandbox's 10 KB combined output size limit.

Each iteration prints a ~500-character line, so the 10 KB cap is crossed
after roughly 20 iterations.  The sandbox kills the process at that point,
appends a truncation notice, and sets output_truncated = true in the result.
The wall-clock timeout (5 s) is NOT what stops this program — the output
limit fires first, well under 1 second.
"""


def main():
    print("Output Flood Demo — printing a large string in an infinite loop.")
    print("Sandbox output limit: 10 KB (combined stdout+stderr).")
    print("The sandbox will kill this process once the limit is exceeded.")
    print("---")

    # A single essay-style sentence repeated to form a ~500-character line.
    # This simulates a user pasting a long passage and printing it in a loop.
    long_line = (
        "The quick brown fox jumps over the lazy dog near the riverbank while "
        "the sun sets slowly behind the distant mountains, casting long golden "
        "shadows across the meadow where children play and birds sing their "
        "evening songs, filling the warm summer air with a sense of peaceful "
        "tranquility that only nature can provide on such a beautiful day. "
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do "
        "eiusmod tempor incididunt ut labore et dolore magna aliqua, and the "
        "story continues without end, growing larger with every iteration."
    )
    # long_line is ~490 bytes.  After ~21 iterations the running total crosses
    # 10 KB and the sandbox terminates this process immediately.

    iteration = 0
    while True:
        iteration += 1
        # flush=True ensures each line is written to the pipe immediately so
        # the sandbox's byte counter advances on every iteration rather than
        # waiting for Python's internal stdout buffer to fill up.
        print(f"[iter {iteration}] {long_line}", flush=True)

    # Unreachable — the sandbox kills us before we ever return.


if __name__ == "__main__":
    main()
