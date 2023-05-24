#ifndef __PTHREAD_H__
#define __PTHREAD_H__

//typedef struct proc thread_t;
//enum threadstate { T_UNUSED, T_EMBRYO, T_SLEEPING, T_RUNNABLE, T_RUNNING, T_ZOMBIE };
enum threadtype_t {T_MAIN, T_THREAD};

typedef struct {
  int pgid;
  int tid;
  enum threadtype_t threadtype;
}tcb_t;

typedef int thread_t;

#endif