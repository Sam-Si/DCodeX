// Structs in C
#include <stdio.h>
#include <string.h>

// Define a Point struct
struct Point {
    int x;
    int y;
};

// Define a Person struct
typedef struct {
    char name[50];
    int age;
    float height;
} Person;

// Function to create a point
struct Point create_point(int x, int y) {
    struct Point p = {x, y};
    return p;
}

// Function to calculate distance from origin
float distance_from_origin(struct Point *p) {
    return (p->x * p->x + p->y * p->y);  // Simplified (no sqrt)
}

int main() {
    printf("Structs Demo\n");
    printf("============\n\n");
    
    // Using Point struct
    struct Point p1 = {3, 4};
    struct Point p2 = create_point(5, 12);
    
    printf("Point 1: (%d, %d)\n", p1.x, p1.y);
    printf("Point 2: (%d, %d)\n", p2.x, p2.y);
    printf("Distance squared from origin (p1): %.0f\n\n", 
           distance_from_origin(&p1));
    
    // Using Person struct
    Person person;
    strcpy(person.name, "Alice");
    person.age = 30;
    person.height = 5.7f;
    
    printf("Person Info:\n");
    printf("  Name: %s\n", person.name);
    printf("  Age: %d\n", person.age);
    printf("  Height: %.1f ft\n", person.height);
    
    return 0;
}
