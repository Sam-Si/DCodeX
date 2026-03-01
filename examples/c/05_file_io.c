// File I/O in C
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    const char *filename = "/tmp/test_file.txt";
    
    printf("File I/O Demo\n");
    printf("=============\n\n");
    
    // Write to file
    FILE *fp = fopen(filename, "w");
    if (fp == NULL) {
        printf("Error opening file for writing!\n");
        return 1;
    }
    
    fprintf(fp, "Hello from C!\n");
    fprintf(fp, "This is line 2.\n");
    fprintf(fp, "Numbers: %d, %d, %d\n", 1, 2, 3);
    fclose(fp);
    printf("Written to file: %s\n\n", filename);
    
    // Read from file
    fp = fopen(filename, "r");
    if (fp == NULL) {
        printf("Error opening file for reading!\n");
        return 1;
    }
    
    printf("Reading from file:\n");
    char line[256];
    int line_num = 1;
    while (fgets(line, sizeof(line), fp) != NULL) {
        printf("  Line %d: %s", line_num++, line);
    }
    fclose(fp);
    
    return 0;
}
