#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"

#include "pthread.h"

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

int sys_procdump(void) {
    procdump();
    return 1;
}

// 변수 타입 이거 맞는지 확인.
int sys_thread_create(void) {
    thread_t *thread;
    void *(*start_routine)(void*);
    void *arg;
    if(argptr(0, ((char**)&thread), sizeof(*thread)) < 0 || argptr(1, ((char**)&start_routine), sizeof(*start_routine)) < 0 || argptr(2, (char**)&arg, sizeof(*arg)) < 0)
        return -1;
    return thread_create(thread, start_routine, arg);
}

int sys_thread_exit(void) {
    void *retval;
    if(argptr(0, ((char**)&retval), sizeof(*retval)) < 0)
        return -1;
    thread_exit(retval);
    return 1;
}

int sys_thread_join(void) {
    thread_t thread; // thread identifier.
    void **retval;
    if(argint(0, &thread) < 0 || argptr(1, ((char**)&retval), sizeof(*retval)) < 0)
        return -1;
    return thread_join(thread, retval);
}