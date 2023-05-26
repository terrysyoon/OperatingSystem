#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

//int sbrk(int n);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->memorylimit = 0; // Default memory limit: unlimited. TBS.

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  //proj2~
  //p->tcb.procsz = p->sz;
  //~proj2
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  // Proj#2 fields~
  p->memorylimit = 0;
  p->stackSize = 1;

  p->tcb.pgid = p->pid;
  p->tcb.threadtype = T_MAIN;
  p->tcb.parentProc = p;
  // ~Proj#2 fields

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}


int
growproc_thread(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->tcb.parentProc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->tcb.parentProc->sz = sz;
  curproc->sz = sz;

  struct proc *p;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->tcb.pgid == curproc->tcb.pgid){
      p->sz = sz;
    }
  }
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  // proj2~
  //np->tcb.procsz = np->sz;
  // ~proj2
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // proj#2 fields~
  np->memorylimit = curproc->memorylimit;
  np->stackSize = curproc->stackSize;

  np->tcb.pgid = np->pid;
  np->tcb.threadtype = T_MAIN;
  np->tcb.parentProc = np;
  // ~proj#2 fields

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  if(curproc->tcb.threadtype == T_THREAD){
    kill(curproc->tcb.parentProc->pid);
    //return; exit은 return 하면 안되지
    acquire(&ptable.lock);
    curproc->state = SLEEPING;
    sched();
    release(&ptable.lock);
  }

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      if(p->tcb.threadtype == T_MAIN) {
        p->parent = initproc;
        if(p->state == ZOMBIE)
          wakeup1(initproc);
      }
      else if(p->tcb.threadtype == T_THREAD){
        //void *dummyRetval;
        release(&ptable.lock);
        kill(p->pid); //not kill_parentProc!
        thread_join(p->pid, 0);
        acquire(&ptable.lock);
      }
      else{
        panic("exit: threadtype error");
      }
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

int kill_parentProc(int tid) {
  struct proc *p;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->pid == tid) {
      p->tcb.parentProc->killed = 1;
      if(p->tcb.parentProc->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

int
setmemorylimit(int pid, int limit) {
  struct proc* p;

  if(limit < 0) {
    return -1;
  }
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid) { // found the target.
      if((limit == 0) || p->sz <= limit) { // limit 설정 가능. limit 0은 unlimited, special case. 조건이 이게 맞는지는 확인 해보기.
        p->memorylimit = limit;
        //cprintf("set!\n");
        release(&ptable.lock);
        return 0; // op success
      }
      else { // 명세: 이미 할당된 메모리가 더 큰 경우 에러
        release(&ptable.lock);
        return -1; // op fail
      }
    }
  }
  release(&ptable.lock);
  return -1; // failed to find the target.
}

void
pmanagerList(void) {
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };

  struct proc *p;
  char *state;

  //acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;

    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    
    uint pages = (PGROUNDUP(p->stackSize));
    cprintf("%s %d %s %d %d %d\n", p->name, p->pid, state, pages, p->sz, p->memorylimit);
  /*
    uint pc[10];
    int i;
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }*/
    cprintf("\n");
  }
  //release(&ptable.lock);
}

int thread_create(thread_t *thread, void *(*start_routine)(void *), void *arg) { //fork 후 stack만 다시 할당. context는 start_routine으로 변경 필요(PC)
  struct proc *curproc = myproc();
  struct proc *np; 

  int i;
  //void* stack;

  uint sz, sp, ustack[4];

  if((np = allocproc()) == 0) {
    cprintf("thread_create: allocproc() failed!\n");
    return -1;
  }
  *thread = np->pid; // thread id

  np->pgdir = curproc->pgdir;
  //Project 2 추가
  np->memorylimit = curproc->memorylimit; // setmemorylimit하면 같은 pgid인 thread 모두 갱신하게 해야겠네
  np->stackSize = curproc->stackSize;

  np->tcb.pgid = curproc->tcb.pgid; // 부모와 같은 process group
  np->tcb.threadtype = T_THREAD; // sub thread
  np->tcb.parentProc = curproc->tcb.parentProc;
  /*
  // malloc 대신에 sbrk 사용!!
  np->tcb.stackEndAddress = curproc->tcb.procsz; //stack grows down
  np->tcb.stackBeginAddress = sbrk(PGSIZE * (curproc->stackSize + 1)); //procsz 뒤에 붙이기. 하나는 guard

  cprintf("stackBeginAddress: %d\n", np->tcb.stackBeginAddress);
  cprintf("stackEndAddress: %d\n", np->tcb.stackEndAddress);

  //가드페이지 설정
  //clearpteu(np->pgdir, (char*)(curproc->sz - (curproc->stackSize)*PGSIZE));
*/

  sz = np->tcb.parentProc->sz;
  sz = PGROUNDUP(sz);
  //sp = sz;
  //cprintf("stacksize: %d sz: %d->",np->stackSize, sz);
  if((sz = allocuvm(np->pgdir, sz, sz + PGSIZE*(np->stackSize +1))) == 0) {
    cprintf("thread_create: allocuvm() failed!\n");
    return -1;
  }
  //cprintf("%d\n",sz);
  //clearpteu(np->pgdir, (char*)(sz - PGSIZE*(np->stackSize + 1)));
  np->tcb.parentProc->sz = sz;
  np->sz = sz; // May26th 2:33PM 이걸 빼먹노...
  sp = sz;

  //stack = (void*)np->tcb.stackBeginAddress;
  if(sp % PGSIZE) { 
      cprintf("thread_create: Stack not aligned!\n");
      // 정상적인 상황이라면 여기 올 일이 없다.
  }

  //np->tcb.procsz = curproc->tcb.procsz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;
/*
  *(uint*)(stack-4) = 0xfffffff0; // fake return PC
  *(uint*)(stack-8) = (uint)arg; // argument
*/
/*
  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = (uint)arg; // argument
*/

  //ustack[0] = 0xffffffff;  // fake return PC
  //ustack[1] = (uint)arg; // argument
  //ustack[2] = (uint)arg;  // fake return PC
  //ustack[3] = (uint)arg; // argument

  /*
  ustack[0] = (uint)arg;
  sp -= 4;
  */
  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = (uint)arg; // argument
  sp -= 8;
  if(copyout(np->pgdir, sp, ustack, 8) < 0) {
    cprintf("thread_create: copyout failed!\n");
    return -1;
  }
  //cprintf("addr: %d val: %d\n", sp+4, *(uint*)(sp+4));
  //cprintf("addr: %d val: %d\n", sp, *(uint*)sp);


  // PC, SP 설정. 여기는 exec 참고해서 다시 하기
  //cprintf("routine: %d\n", (uint)start_routine);
  np->tf->eip = (uint)start_routine;
  np->tf->esp = sp; 

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]); // file reference count 올리고 parameter 그대로 반환
  np->cwd = idup(curproc->cwd); // 마찬가지로 reference 올리기

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  *thread = np->pid; // thread id 반환

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);
  return 0;
}

void thread_exit(void *retval){
  struct proc *curproc = myproc();
  struct proc *p;
  //int fd;

  //cprintf("thread_exit>  pid: %d retval: %p\n",curproc->pid, retval);

  if(curproc->tcb.threadtype == T_MAIN) {
    // exit();
    cprintf("thread_exit>  pid: %d Mainthread cannot exit\n");
  }
/*
  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;
*/
  acquire(&ptable.lock);

  curproc->tcb.retval = retval;

  // 이 thread를 위해 thread_create한 아이.
  wakeup1(curproc->parent);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      if(p->tcb.threadtype == T_MAIN) {
        p->parent = initproc;
        if(p->state == ZOMBIE)
          wakeup1(initproc);
      }
      else if(p->tcb.threadtype == T_THREAD){
        //void *dummyRetval;
        release(&ptable.lock);
        kill(p->pid); //not kill_parentProc!
        thread_join(p->pid, 0);
      }
      else{
        panic("thread_exit: threadtype error");
      }
    }
  }

  curproc->state = ZOMBIE;
  //release(&ptable.lock);
  //cprintf("calling sched\n");
  sched();
  panic("zombie exit");
  return;
}

/// wait과 비슷하게
int thread_join(thread_t thread, void **retval){
  struct proc *p;
  int haveThread;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for(;;) {
    haveThread = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if(p->pid != thread)
        continue;
      if(p->tcb.threadtype != T_THREAD) {
        cprintf("thread_join> pid: %d is not a thread\n", thread);
        release(&ptable.lock);
        return -1;
      }
      if(p->tcb.parentProc != curproc) {
        cprintf("thread_join> pid: %d is not a child of pid: %d\n", thread, curproc->pid);
        release(&ptable.lock);
        return -1;
      }
      haveThread = 1;
      if(p->state == ZOMBIE) {
        if(retval)
          *retval = p->tcb.retval;
        kfree(p->kstack);
        p->kstack = 0;
        //pgdir는 할당해제 금지
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;

        p->memorylimit = 0;
        p->stackSize = 1;
        p->tcb.pgid = 0;
        p->tcb.threadtype = T_MAIN;
        p->tcb.parentProc = 0;
        p->tcb.retval = 0;
        release(&ptable.lock);
        return 0;
      }
    }
    if(!haveThread || curproc->killed) {
      cprintf("thread_join> pid: %d does not exist\n", thread);
      release(&ptable.lock);
      cprintf("thread_join> released lock\n", thread);
      return -1;
    }

    sleep(curproc, &ptable.lock);
  }
  return 0;
}

//May 23rd

// 기본적으로 kill과 동일하지만 newMain은 제외, threadtype의 변경 수반
// 또한, 시간이 걸리더라도 thread 모두 종료된 것 까지 확인
int
exec_remove_thread(struct proc *newMain) {
  return 0;
}

void killHandler() {
  struct proc* curproc = myproc();
  if(curproc->tcb.threadtype == T_THREAD) {
    thread_exit(0);
  }
  else if(curproc->tcb.threadtype == T_MAIN){
    exit();
  } else {
    panic("killHandler> not a thread or main");
  }
  return;
}

// replica of sys_sbrk
/*
int
sbrk(int n)
{
  //cprintf("sbrk called!\n");
  int addr;

  if(growproc_thread(n) < 0)
    return -1;
  //cprintf("done!\n");
  addr = myproc()->sz;
  return addr;
}*/