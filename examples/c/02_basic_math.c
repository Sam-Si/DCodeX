// Basic Math Operations in C
#include <stdio.h>

int add(int a, int b) { return a + b; }
int subtract(int a, int b) { return a - b; }
int multiply(int a, int b) { return a * b; }
int divide(int a, int b) { return b != 0 ? a / b : 0; }

int main() {
    int x = 10, y = 3;
    
    printf("Basic Math Operations\n");
    printf("=====================\n");
    printf("x = %d, y = %d\n\n", x, y);
    printf("Addition:       %d + %d = %d\n", x, y, add(x, y));
    printf("Subtraction:    %d - %d = %d\n", x, y, subtract(x, y));
    printf("Multiplication: %d * %d = %d\n", x, y, multiply(x, y));
    printf("Division:       %d / %d = %d\n", x, y, divide(x, y));
    printf("Modulo:         %d %% %d = %d\n", x, y, x % y);
    
    return 0;
}
