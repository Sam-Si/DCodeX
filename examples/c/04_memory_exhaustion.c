#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    printf("Allocating memory to test limits...\n");
    size_t chunk_size = 1024 * 1024 * 100; // 100MB
    while (1) {
        void* ptr = malloc(chunk_size);
        if (ptr == NULL) {
            printf("Allocation failed!\n");
            return 1;
        }
        memset(ptr, 0, chunk_size);
        printf("Allocated 100MB...\n");
    }
    return 0;
}
