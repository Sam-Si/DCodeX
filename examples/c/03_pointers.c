// Pointers and Memory in C
#include <stdio.h>
#include <stdlib.h>

void swap(int *a, int *b) {
    int temp = *a;
    *a = *b;
    *b = temp;
}

int main() {
    // Basic pointer usage
    int x = 42;
    int *ptr = &x;
    
    printf("Pointers Demo\n");
    printf("=============\n");
    printf("Value of x: %d\n", x);
    printf("Address of x: %p\n", (void*)&x);
    printf("Value via pointer: %d\n", *ptr);
    printf("Pointer address: %p\n\n", (void*)ptr);
    
    // Pointer arithmetic
    int arr[] = {10, 20, 30, 40, 50};
    int *p = arr;
    
    printf("Array via pointer arithmetic:\n");
    for (int i = 0; i < 5; i++) {
        printf("  arr[%d] = %d (via ptr: %d)\n", i, arr[i], *(p + i));
    }
    printf("\n");
    
    // Swap using pointers
    int a = 5, b = 10;
    printf("Before swap: a=%d, b=%d\n", a, b);
    swap(&a, &b);
    printf("After swap:  a=%d, b=%d\n", a, b);
    
    return 0;
}
