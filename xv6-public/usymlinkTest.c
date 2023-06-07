/*
Author: Terry
yoonsb@hanyang.ac.kr

Date: 2023/06/07

This file is used to test the symlink function.
*/

void strcat(char* dest, const char* src) {
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

void atoi(char* s, int* n) {
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
    atoi(argv[2], &depth);

    int i;
    char* old_target = (char*)malloc(100);
    strcat(old_target, argv[1]);
    for(i = 0; i < depth; i++) {
        char* new_target = (char*)malloc(100);
        strcat(new_target, "sym_");
        strcat(new_target, argv[1]);
        strcat(new_target, "_");
        char* depth_str = (char*)malloc(100);
        itoa(i, depth_str);
        strcat(new_target, depth_str);
        if(symlink(old_target, new_target) < 0) {
            printf(2, "symlink %s %s: failed\n", argv[1], new_target);
            exit();
        }
        free(old_target);
        old_target = new_target;
        free(depth_str);
        depth_str = 0;
    }
    exit();
    return -987654321;//unreachable
}