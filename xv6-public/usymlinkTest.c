/*
Author: Terry
yoonsb@hanyang.ac.kr

Date: 2023/06/07

This file is used to test the symlink function.
*/

#include "types.h"
#include "stat.h"
#include "user.h"

void strcat(char* dest, char* src) {
    while (*dest)
        dest++;
    while ((*dest++ = *src++))
        ;
}

void itoa(int n, char* s) {
    int i, sign;
    if ((sign = n) < 0)
        n = -n;
    i = 0;
    do {
        s[i++] = n % 10 + '0';
    } while ((n /= 10) > 0);
    if (sign < 0)
        s[i++] = '-';
    s[i++] = '\0';
}

void atoi_n(char* s, int* n) {
    int i, sign;
    if ((sign = *s) == '-')
        s++;
    for (*n = 0, i = 0; s[i] != '\0'; i++)
        *n = *n * 10 + s[i] - '0';
    if (sign == '-')
        *n = -*n;
}

int main(int argc, char* argv[]) {
    if(argc != 3) {
        printf(2, "Usage: usymlinkTest [target] [depth]\n");
        exit();
    }

    int depth;
    atoi_n(argv[2], &depth);

    int i;
    char old_target[100];
    strcat(&old_target[0], argv[1]);
    for(i = 0; i < depth; i++) {
        char new_target[100];
        strcat(&new_target[0], "sym_");
        strcat(&new_target[0], argv[1]);
        strcat(&new_target[0], "_");
        char depth_str[100];
        itoa(i, &depth_str[0]);
        strcat(&new_target[0], &depth_str[0]);
        if(symlink(old_target, new_target) < 0) {
            printf(2, "symlink %s %s: failed\n", old_target, new_target);
            exit();
        }
        int j;
        for(j = 0; j < 100; j++) {
            old_target[j] = new_target[j];
        }
    }
    exit();
    return -987654321;//unreachable
}