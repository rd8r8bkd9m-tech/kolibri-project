#include <stdio.h>
#include <string.h>

int main() {
    double a, b, c;
    int parsed = sscanf("2x+3=7", "%lfx+%lf=%lf", &a, &b, &c);
    printf("parsed: %d, a=%f, b=%f, c=%f\n", parsed, a, b, c);
    
    char op_char;
    const char *lower = "сколько будет 7 * 8";
    const char *ptr = lower;
    while (*ptr && !(*ptr >= '0' && *ptr <= '9')) ptr++;
    parsed = sscanf(ptr, "%lf %c %lf", &a, &op_char, &b);
    printf("arithmetic parsed: %d, a=%f, op=%c, b=%f\n", parsed, a, op_char, b);
    return 0;
}
