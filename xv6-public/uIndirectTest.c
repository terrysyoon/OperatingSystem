#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

//char buf[512];
const char* ipsum = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed commodo elit a orci malesuada, et hendrerit nunc ullamcorper. Fusce auctor justo purus, eget scelerisque velit consequat at. Donec turpis mauris, imperdiet eget erat in, posuere accumsan dui. Integer ac nunc eget ipsum condimentum posuere et et neque. Suspendisse quis pretium sapien, vitae auctor est. Aliquam erat volutpat. Suspendisse potenti. Etiam pulvinar lacinia nulla, et auctor lorem efficitur a. Suspendisse dapibus bibendum est vitae euismod. Proin eu velit vitae massa ullamcorper ultricies eget id tellus. Nam volutpat nullam.";

void atoi_n(char* s, int* n) {
    int i, sign;
    if ((sign = *s) == '-')
        s++;
    for (*n = 0, i = 0; s[i] != '\0'; i++)
        *n = *n * 10 + s[i] - '0';
    if (sign == '-')
        *n = -*n;
}

int
main(int argc, char *argv[])
{
    int fd, i, a;

    if(argc <= 1){
        printf(1, "Usage: uIndirectTest [blocks]\n");
        exit();
    }
    fd = open("uIndirectTestFile", O_CREATE | O_RDWR);
    atoi_n(argv[1], &a);
    for(i = 1; i <= a; i++){
        if(write(fd, ipsum, 512) < 0){ 
            printf(1, "uIndirectTest: Failed at block %d\n", i);
            exit();
        }
    }
    close(fd);
    printf(1, "Done! %d\n", i-1);
  
    exit();
}
