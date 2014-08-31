#include <stdio.h>
#include <time.h>

int main(){
    time_t time_a = 0;
    struct tm *tm;
    tm = localtime (&time_a);
}
