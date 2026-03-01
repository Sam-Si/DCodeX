#include <stdio.h>

int main() {
    printf("Starting infinite loop to test timeout...\n");
    while (1) {
        // Busy wait to consume CPU
    }
    return 0;
}
