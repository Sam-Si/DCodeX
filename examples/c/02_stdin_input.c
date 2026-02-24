#include <stdio.h>

int main() {
    char name[100];
    int age;
    if (scanf("%99s %d", name, &age) == 2) {
        printf("Hello %s, you are %d years old.\n", name, age);
    } else {
        printf("Failed to read input.\n");
    }
    return 0;
}
