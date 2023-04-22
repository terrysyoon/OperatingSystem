#include "types.h" 
#include "defs.h"
#include "stat.h" 
#include "user.h"

extern void procdump();

int
main(int argc, char *argv[]) {
    printf(1, "Procdump\n");
    procdump();
    exit();
};