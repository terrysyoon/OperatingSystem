#include "types.h"
#include "stat.h"
#include "user.h"

//char buf[512];
const char* ipsum = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Donec non dolor pretium, fringilla tellus vitae, efficitur justo. Vestibulum scelerisque, arcu at fermentum sagittis, ipsum tellus malesuada ex, id iaculis nisl turpis quis urna. Vivamus vitae dui tempor, consectetur arcu at, dignissim erat. Quisque ac sollicitudin dui, non dignissim sem. Etiam posuere justo diam. Aenean lacinia ligula at turpis auctor auctor. Maecenas eget augue arcu. Pellentesque lobortis tincidunt pulvinar. Suspendisse non est nec.";

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
    int fd, i;

    if(argc <= 1){
        printf(1, "Usage: uIndirectTest [blocks]\n");
        exit();
    }
    fd = open("uIndirectTestFile", 0x200);
    atoi_n(argv[1], &i);
    for(i = 1; i <= argc; i++){
        if(write(fd, ipsum, 512) < 0){ 
            printf(1, "uIndirectTest: Failed at block %d\n", i);
            exit();
        }
    }
    close(fd);
    printf(1, "Done! %d\n", i-1);
  
    exit();
}
