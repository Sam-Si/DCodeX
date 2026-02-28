#include <stdio.h>

int main() {
    printf("Flooding output to test truncation...\n");
    for (int i = 0; i < 100000; i++) {
        printf("Line %d: This is a lot of output that should be truncated by the sandbox eventually.\n", i);
    }
    return 0;
}
