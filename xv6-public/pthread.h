#ifndef __PTHREAD_H__
#define __PTHREAD_H__

enum threadtype_t {T_MAIN, T_THREAD};

typedef struct {
  int pgid;
  enum threadtype_t threadtype;

  //uint memorylimit; // Memory limit, in bytes. 0 stands for limitless, which is the initial state.
  uint stackBeginAddress; // Begin address of the stack = DATA 영역 끝 + 1 (guard page의 시작 logical address)
  uint stackEndAddress; // stack 영역 끝
  uint stackSize;
  //void* stack; // subthread일 때만 사용. 
}tcb_t;

typedef int thread_t;

#endif