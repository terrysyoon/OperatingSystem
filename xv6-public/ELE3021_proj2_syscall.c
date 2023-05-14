#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"

int sys_setmemorylimit(void) {
    int pid, limit;
    if (argint(0, &pid) < 0 || argint(1, &limit) < 0)
        return -1;
    setmemorylimit(pid, limit);
    return 1;
}

int sys_pmanager_list(void) {
    pmanager_list();
    return 1;
}