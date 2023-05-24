// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

/*
struct pthread {
  int tid;
  enum procstate state;
  
};*/

// 아주 최소한으로 공유할 부모 프로세스 상태
struct parentproc {
  int pid;
  int next_tid;
  enum procstate state;
  /*
  enum threadstate threadstate;
  enum threadtype threadtype;
  int killed;
  uint memorylimit;
  uint stackBeginAddress;
  uint stackEndAddress;
  uint stackSize;
  char name[16];
  */
};

// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes) 따로. user address의 크기, 즉 data, heap, stack 포함.
  pde_t* pgdir;                // Page table 따로. 스택 부분 갈아끼워야 함
  char *kstack;                // Bottom of kernel stack for this process. 따로.
  enum procstate state;        // Process state 따로. main thread가 sleep이더라도 상관 없이 돌 수 있다.
  int pid;                     // thread id. 따로. main thread는 pid와 pgid가 같다.
  struct proc *parent;         // Parent process. main thread는 자신을 fork한 다른 process의 thread, subthread는 자신을 create한 thread 가리키도록
  struct trapframe *tf;        // Trap frame for current syscall 따로.
  struct context *context;     // swtch() here to run process 따로.
  void *chan;                  // If non-zero, sleeping on chan 따로. 
  int killed;                  // If non-zero, have been killed 따로. 
  struct file *ofile[NOFILE];  // Open files 공유 필요. __________
  struct inode *cwd;           // Current directory 공유 필요.  _____________
  char name[16];               // Process name (debugging)

  //ELE3021 Project#2
  thread_t tid; // thread id
  tcb_t tcb; // thread control block. create 때 이거 포인터 주면 커널 영역이라 안될텐데..?
  uint memorylimit; // Memory limit, in bytes. 0 stands for limitless, which is the initial state.
  uint stackBeginAddress; // Begin address of the stack = DATA 영역 끝 + 1 (guard page의 시작 logical address)
  uint stackEndAddress; // stack 영역 끝
  uint stackSize;
  void* stack; // subthread일 때만 사용. main thread는 null

  //enum threadtype threadtype; // T_MAIN or T_THREAD
  //enum threadstate threadstate; // T_UNUSED, T_EMBRYO, T_SLEEPING, T_RUNNABLE, T_RUNNING, T_ZOMBIE.. 로 할까 싶었으나 state 굳이 따로 쓸 필요 없음.

  //int threadnum; // number of threads. 1 if only the main thread exist


  //thread 생성 시 모든 page table entry 복제하고, stackBeginAddress ~ stackEndAdress 사이 주소만 새로 할당해서 매핑 
};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
