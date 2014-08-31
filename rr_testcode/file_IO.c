#include <stdio.h>

int main(){
    FILE* fptr = fopen("xd.tmp", "r");
    char str[256];
    fscanf(fptr, " %s", str);
    printf("file read: %s\n", str);
    fclose(fptr);
    return 0;
}
