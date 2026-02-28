// Dynamic Memory Allocation in C
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    printf("Dynamic Memory Demo\n");
    printf("===================\n\n");
    
    // malloc - allocate uninitialized memory
    int *arr = (int*)malloc(5 * sizeof(int));
    if (arr == NULL) {
        printf("Memory allocation failed!\n");
        return 1;
    }
    
    printf("Allocated array (malloc):\n");
    for (int i = 0; i < 5; i++) {
        arr[i] = i * 10;
        printf("  arr[%d] = %d\n", i, arr[i]);
    }
    printf("\n");
    
    // calloc - allocate and zero-initialize
    int *zeros = (int*)calloc(5, sizeof(int));
    if (zeros == NULL) {
        free(arr);
        return 1;
    }
    
    printf("Zero-initialized array (calloc):\n");
    for (int i = 0; i < 5; i++) {
        printf("  zeros[%d] = %d\n", i, zeros[i]);
    }
    printf("\n");
    
    // realloc - resize memory
    arr = (int*)realloc(arr, 10 * sizeof(int));
    if (arr == NULL) {
        free(zeros);
        return 1;
    }
    
    printf("Resized array (realloc to 10 elements):\n");
    for (int i = 5; i < 10; i++) {
        arr[i] = i * 10;
    }
    for (int i = 0; i < 10; i++) {
        printf("  arr[%d] = %d\n", i, arr[i]);
    }
    
    // Free memory
    free(arr);
    free(zeros);
    printf("\nMemory freed successfully.\n");
    
    return 0;
}
