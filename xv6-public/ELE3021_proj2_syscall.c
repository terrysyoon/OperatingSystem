#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"

int sys_setmemorylimit(void) {
    int pid, limit;
    if (argint(0, &pid) < 0 || argint(1, &limit) < 0)
        return -1;
    return setmemorylimit(pid, limit);
}

int sys_pmanagerList(void) {
    pmanagerList();
    return 1;
}

int sys_exec2(void) {
    char *path;
    char **argv;
    int stacksize;
    if(argptr(0, &path, sizeof(*path)) < 0 || argptr(1, ((char**)&argv), sizeof(*argv)) < 0 || argint(2, &stacksize) < 0)
        return -1;
    return exec2(path, argv, stacksize);
}