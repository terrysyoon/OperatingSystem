#include "types.h" 
#include "stat.h" 
#include "user.h"


int
main(int argc, char *argv[]) {
    int ret_val;
    printf(1, "Procdump\n");
    ret_val = procdump();
    printf(1, "Return value : 0x%x\n", ret_val); 
    exit();
};