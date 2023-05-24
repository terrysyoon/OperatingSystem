#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

#include "pthread.h"
//#include "stdio.h"

int
sbrk(int n);

struct {
  struct spinlock lock;
  struct proc proc[NPROC];

  //struct parentproc parentproc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

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

  cprintf("alloc proc!\n");

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
  if((p->kstack = kalloc()) == 0){ // thread도 kernel stack은 따로 쓰니 유지하는게 맞을 듯
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
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  p->tcb.threadtype = T_MAIN; //첫 user process, 당연히 main thread. fork/userinit의 경우에 해당

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n) //only reference: sbrk
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

  // sbrk 대응
  struct proc *p;
  //acquire(&ptable.lock); //lock 필요 없나?
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->tcb.pgid == curproc->tcb.pgid) {
      p->sz = sz;
    }
  }
  //release(&ptable.lock);
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
// thread가 fork call 시에는 그 thread만 새 process로 복제
// 같은 process의 다른 thread의 stack 원래는 복제하지 말아야 할 것 같지만, 일단은 스킵
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  cprintf("fork called\n");
  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){ //새 process의 page table도 여기서 만들어짐
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }

/*
  // debug
  np->tcb.pgid = 0;
  np->tcb.tid = 0;
  np->tcb.threadtype = T_THREAD; //UNUSED가 맞을 것 같은데 왜 enum 타입 추가 시 에러 발생?
  np->memorylimit = 0;
  np->stackBeginAddress = 0;
  np->stackEndAddress = 0;
  np->stackSize = 0;
  //stackToFree = p->stack;
  np->stack = 0;
  // ~debug
*/

/* BS
  // thread 복제~ thread create 참고
  
  * thread 여러 개 동시 생성 시 실패하는 경우:
  * 1. allocproc()에서 실패 : unused 부족 or kalloc() 실패
  * 2. stack 할당 실패
  

  // 실패 시:
  kfree(np->kstack);
  freevm(np->pgdir);  //insert: page directory 해제
  np->kstack = 0;
  np->state = UNUSED;
  return -1;
  // ~ thread 복제
*/

  //Project 2 추가
  np->tid = np->pid; // main thread의 tid는 pid와 같다.
  np->tcb.pgid = np->pid; // 새 process group 만들기
  np->tcb.tid = np->pid; // main thread의 tid는 pid와 같다.
  np->tcb.threadtype = T_MAIN; // main thread
  np->memorylimit = curproc->memorylimit;
  np->stackBeginAddress = curproc->stackBeginAddress; // 이 두줄 꼭 필요함!
  np->stackEndAddress = curproc->stackEndAddress;
  np->stackSize = curproc->stackSize;

  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;


  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]); // file reference count 올리고 parameter 그대로 반환
  np->cwd = idup(curproc->cwd); // 마찬가지로 reference 올리기

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;
  //np->threadtype = T_MAIN;

  release(&ptable.lock);
  cprintf("fork end: %d\n",pid);
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
  cprintf("exit called %d\n",curproc->pid);
  if(curproc == initproc)
    panic("init exiting");

  if(curproc->tcb.threadtype == T_THREAD){ // subthread인 경우
    cprintf("exit: subthread cannot call exit()\n");
    return;
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
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
// Wait은 자식 '프로세스'가 종료될 때까지 기다리는 함수이다. thread는 예외
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
      if(p->parent != curproc || p->tcb.threadtype == T_THREAD) // 자식이 아니거나, thread인 경우
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


        //p->memorylimit = 0; // 유일하게 초기화 안되던 부분
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
      //cprintf("scheduler: %d\n",p->pid);

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
// thread 대응
int
kill(int pid)
{
  struct proc *p;
  int cnt = 0;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->tcb.pgid == pid){
      cnt++;
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      //release(&ptable.lock);
      //return 0;
    }
  }
  release(&ptable.lock);
  return ((cnt)?0:-1);
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
    
    //uint pages = (PGROUNDUP(p->stackSize));
    cprintf("%s %d %s %d %d %d %d\n", p->name, p->pid, state, p->stackSize, p->sz, p->memorylimit, p->tcb.threadtype);
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

// Thread 관련 커널코드
// 최대한 proc.c 코드 활용, 복붙해서 조금만 바꾸기


int thread_create(thread_t *thread, void *(*start_routine)(void *), void *arg) { //fork 후 stack만 다시 할당. context는 start_routine으로 변경 필요(PC)
    
    struct proc *curproc = myproc();
    struct proc *np;
    //int i, tid;
    int i;

    void *stack; // stack 할당
    
    // Design: main thread와 stack 크기는 동일하도록
    // To-Do: 가드페이지 flag 설정
    /*
    if((stack = malloc((uint)PGSIZE) * (curproc->stackSize + 1)) == 0) {
        printf(1, "Failed to allocate a stack\n");
        return -1;
    }
    */

    //여기서부터 복제, thread 관련 코드
    
    //kstack, pid, tf, context은 여기서 할당
    if((np = allocproc()) == 0) {
        cprintf("thread_create: allocproc() failed!\n");
        return -1;
    }

/*     // Copy process state from proc.
    if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){ //새 process의 page table도 여기서 만들어짐
      kfree(np->kstack);
      np->kstack = 0;
      np->state = UNUSED;
      return -1;
    } */

    // uvm 복사 대신에 pt 복사, stack 교체
    np->pgdir = curproc->pgdir;
    // malloc 대신에 sbrk 사용!!
    np->stackEndAddress = curproc->sz; //stack grows down
    np->stackBeginAddress = sbrk(PGSIZE * (curproc->stackSize + 1));
    //가드페이지 설정
    clearpteu(np->pgdir, (char*)(curproc->sz - (curproc->stackSize)*PGSIZE));

    stack = (void*)np->stackBeginAddress;
    if((uint)stack % PGSIZE) { 
        cprintf("thread_create: Stack not aligned!\n");
        // 정상적인 상황이라면 여기 올 일이 없다.
    }
    np->stack = stack; // 접근은 sp로, 이 포인터는 할당 해제 용. 마지막 페이지는 가드

    np->sz = curproc->sz;
    np->parent = curproc;
    *np->tf = *curproc->tf;

    //Project 2 추가
    np->tcb.pgid = curproc->tcb.pgid; // 부모와 같은 process group
    np->tcb.tid = np->pid; // subthread
    np->tcb.threadtype = T_THREAD; // sub thread
    np->memorylimit = curproc->memorylimit; // setmemorylimit하면 같은 pgid인 thread 모두 갱신하게 해야겠네
    np->stackSize = curproc->stackSize;

    // Clear %eax so that fork returns 0 in the child.
    np->tf->eax = 0;

    // 새 stack 채우기
    /* 
    URGENT TO-DO
    0: stack grows downwards, make 'stack' point the last word allocated for a stack.
    1. setmemlimit() must update 'memorylimit' field of all threads in the same process group,
    as sbrk might be called in any thread, and it affects all threads in the same process group.
    2. consider what datapath must be employed to implement retval of thread_join
    3. Update sz of all threads in the same process group when sbrk is called in any thread.
    
    Note: it is the linux pthread_create specification to have at most one argument for start_routine.
    Conventionally start routines take a single argument that is a pointer to a struct containing multiple and complex arguments.
    */  
    *(uint*)(stack-8) = 0xffffffff; // fake return PC
    *(uint*)(stack-4) = (uint)arg; // argument
    // PC, SP 설정. 여기는 exec 참고해서 다시 하기
    np->tf->eip = (uint)start_routine;
    np->tf->esp = (uint)(stack - 8); // argument가 하나인 경우에만 대응하나?

    for(i = 0; i < NOFILE; i++)
      if(curproc->ofile[i])
        np->ofile[i] = filedup(curproc->ofile[i]); // file reference count 올리고 parameter 그대로 반환
    np->cwd = idup(curproc->cwd); // 마찬가지로 reference 올리기

    safestrcpy(np->name, curproc->name, sizeof(curproc->name));

    //np->tcb = np->pid;

    acquire(&ptable.lock);

    np->state = RUNNABLE;
    //np->threadtype = T_MAIN;

    release(&ptable.lock);

    return 0;
} 

void thread_exit(void *retval){ // exit과 비슷하게 구현. 메인쓰레드가 종료되는 경우 고려하여야. -> exit 하도록
  
  struct proc *curproc = myproc();
  struct proc *p;
  //int fd;

  if(curproc == initproc)
    panic("init exiting");

  if(curproc->tcb.threadtype == T_MAIN) { // Design: Main thread(process)는 thread_exit() 호출 불가
    //(*((int*)retval)) = 0;
    //exit();
    panic("thread_exit: Main thread cannot call thread_exit()");
  }

  // File 닫는 것은 exit에서만
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

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent); // thread_join 호출한 부모 깨우기

  // Pass abandoned children to init. 
  // Design: Orphan thread는 orphan process와 동일하게 처리. 굳이 main thread로 입양하지 않음
  // To-DO: initcode가 zombie thread 정리할 때, 새로 추가한 필드도 정리하도록 수정 필요
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE; // thread_join 대기. 

  // To-Do: retval 넣기
  // parent thread의 stack으로 값을 넣고 stack을 free해주어야.
  sched();
  panic("zombie exit");
}

int thread_join(thread_t thread, void **retval){
  
  struct proc *p;
  //void *stackToFree;

  int haveThread, pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for(;;){
    // 내가 기다리는 쓰레드가 존재하는지 확인, 없으면 -1 리턴, 있으면 기다렸다가 pid return Call하는 parent가 예외처리 해주어야. 
    haveThread = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc || p->pid != thread) // 부모 thread 만이 자식 thread join 가능. thread 찾더라도 부모 아니면 join 불가
        continue;

      if(p->tcb.threadtype == T_MAIN) { // Design: Main thread는 join 불가
        release(&ptable.lock);
        cprintf("thread_join: Main thread cannot be joined!\n");
        return -1; 
      }

      haveThread = 1;
      if(p->state == ZOMBIE){ // 실행 안 끝났으면 기다려야지... 여기서 retval 갱신해야 하는데 어떻게?
        // Target 찾음. 얘만 할당 해제하면 ok.
        // stack 만 해제해야. 나머지 영역은 main thread에서 해제
        pid = p->pid; // tid 반환해야.
        kfree(p->kstack);
        p->kstack = 0;
        //freevm(p->pgdir); // 이러면 main thread의 page table도 해제되는거다. stack만 해제하도록 수정 필요.
        if(deallocuvm(p->pgdir, p->stackBeginAddress, p->stackEndAddress) != p->stackEndAddress) {
          panic("thread_join: stack deallocation failed");
        }
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;

        // 새로 만든 proc field 초기화
        p->tcb.pgid = 0;
        p->tcb.tid = 0;
        p->tcb.threadtype = T_THREAD; //UNUSED가 맞을 것 같은데 왜 enum 타입 추가 시 에러 발생?
        p->memorylimit = 0;
        p->stackBeginAddress = 0;
        p->stackEndAddress = 0;
        p->stackSize = 0;
        //stackToFree = p->stack;
        p->stack = 0;

        // User stack 여기서 반환해야 하는데... 어떻게?
        // retval을 넣은 다음에야 user stack을 free할 수 있을 것 같은데...

        release(&ptable.lock);
        //free(stackToFree); 이건 malloc으로 잡은 stack일 때. 
        return pid; // tid 반환
      }
    }

    // No point waiting if we don't have any children.
    if(!haveThread || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//May 23rd

// 기본적으로 kill과 동일하지만 newMain은 제외, threadtype의 변경 수반
// 또한, 시간이 걸리더라도 thread 모두 종료된 것 까지 확인
int
exec_remove_thread(struct proc *newMain) {
  struct proc *p;
  int cnt = 0;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == newMain->pid) {
      continue;
    }
    if(p->tcb.pgid == newMain->tcb.pgid){
      cnt++;
      p->killed = 1;
      p->tcb.threadtype = T_THREAD;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;

      release(&ptable.lock);
      thread_join(p->tid, 0); // 얘는 lock이 없어야.
      acquire(&ptable.lock);
    }
  }
  newMain->tcb.threadtype = T_MAIN;
  newMain->tcb.pgid = newMain->pid;
  release(&ptable.lock);
  return (cnt>0)?cnt:-1;
}

void killHandler() {
  struct proc *curproc = myproc();
  if(curproc->tcb.threadtype == T_MAIN) {
    exit();
  }
  else if(curproc->tcb.threadtype == T_THREAD){
    thread_exit(0);
  }
  else {
    panic("killedHandler: threadtype error");
  }
}

// replica of sys_sbrk
int
sbrk(int n)
{
  int addr;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}