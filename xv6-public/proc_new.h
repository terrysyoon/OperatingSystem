#define NUM_QUEUES (3)
#define SCHEDULER_LOCK_PASSWORD (2021078059)
//#include "spinlock.h"
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

// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)

  // MLFQ fields
  int q_number; //0~2, Indicates its queue group. L0~L2
  int priority;                // Process priority
  int q_ticks;                 // Ticks in current queue
  int n_run;                   // Number of times process has run
  int ctime;                   // Creation time
  int etime;                   // End time
  int rtime;                   // Running time
  int iotime;                  // I/O time
  int q[NUM_QUEUES];           // Ticks in each queue
  int q_ticks_total;           // Total ticks in all queues

  // Queue fields
  struct proc *next;           // Next process in queue
  struct proc *prev;
};

struct queue {
  int level; // 0~2, L0~L2, Meaning 0 is the highest priority queue, 2 is the lowest priority queue.
  int size; // The number of processes in this queue
  int max_size; // The maximum number of processes allowed in this queue. Since the queue is implemented in a linked list, this should be meaningless but jic.
  int quantom; // Maximum number of ticks allowed in this queue

  struct proc *head;
  struct proc *tail;
};

struct MLFQ_TICK{
  struct spinlock lock;
  uint global_tick;
};


// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
