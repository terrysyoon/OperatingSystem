#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

// Simple logging that allows concurrent FS system calls.
//
// A log transaction contains the updates of multiple FS system
// calls. The logging system only commits when there are
// no FS system calls active. Thus there is never
// any reasoning required about whether a commit might
// write an uncommitted system call's updates to disk.
//
// A system call should call begin_op()/end_op() to mark
// its start and end. Usually begin_op() just increments
// the count of in-progress FS system calls and returns.
// But if it thinks the log is close to running out, it
// sleeps until the last outstanding end_op() commits.
//
// The log is a physical re-do log containing disk blocks.
// The on-disk log format:
//   header block, containing block #s for block A, B, C, ...
//   block A
//   block B
//   block C
//   ...
// Log appends are synchronous.

// Contents of the header block, used for both the on-disk header block
// and to keep track in memory of logged block# before commit.
struct logheader {
  int n;
  int block[LOGSIZE];
};

struct log {
  struct spinlock lock;
  int start;
  int size;
  int outstanding; // how many FS sys calls are executing.
  int committing;  // in commit(), please wait.
  int dev;
  struct logheader lh;
};
struct log log;

static void recover_from_log(void);
static void commit();

void
initlog(int dev)
{
  if (sizeof(struct logheader) >= BSIZE)
    panic("initlog: too big logheader");

  struct superblock sb;
  initlock(&log.lock, "log");
  readsb(dev, &sb);
  log.start = sb.logstart;
  log.size = sb.nlog;
  log.dev = dev;
  recover_from_log();
}

// Copy committed blocks from log to their home location
static void
install_trans(void)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *lbuf = bread(log.dev, log.start+tail+1); // read log block
    struct buf *dbuf = bread(log.dev, log.lh.block[tail]); // read dst
    memmove(dbuf->data, lbuf->data, BSIZE);  // copy block to dst
    bwrite(dbuf);  // write dst to disk
    brelse(lbuf);
    brelse(dbuf);
  }
}

// Read the log header from disk into the in-memory log header
static void
read_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *lh = (struct logheader *) (buf->data);
  int i;
  log.lh.n = lh->n;
  for (i = 0; i < log.lh.n; i++) {
    log.lh.block[i] = lh->block[i];
  }
  brelse(buf);
}

// Write in-memory log header to disk.
// This is the true point at which the
// current transaction commits.
static void
write_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *hb = (struct logheader *) (buf->data);
  int i;
  hb->n = log.lh.n;
  for (i = 0; i < log.lh.n; i++) {
    hb->block[i] = log.lh.block[i];
  }
  bwrite(buf);
  brelse(buf);
}

static void
recover_from_log(void)
{
  read_head();
  install_trans(); // if committed, copy from log to disk
  log.lh.n = 0;
  write_head(); // clear the log
}

// called at the start of each FS system call.
void
begin_op(void)
{
  int held_lock = holding(&log.lock);
  if(!held_lock) {
    acquire(&log.lock);
  }

  //acquire(&log.lock);
  while(1){
    if(log.committing){
      cprintf("begin_op: committing, sleep\n");
      sleep(&log, &log.lock);
    } 
    /*
    else if(log.lh.n + (log.outstanding+1)*MAXOPBLOCKS > LOGSIZE){ // original code. 얘가 작업하다가 버퍼 넘칠 수 있으면, 아예 시작 전에 기다림
      // this op might exhaust log space; wait for commit.
      sleep(&log, &log.lock);
    } */

    
    // Discarded change.
    //log.lh.n은 log_write()로 begin_op() ~ end_op() 사이에도 갱신 됨. end_op에서 flush 안하더라도 outstanding 깎을 수 있는 이유.
    else if(log.lh.n + (log.outstanding+1)*MAXOPBLOCKS > LOGSIZE){ // Proj3: 얘가 작업하다가 버퍼 넘칠 수 있으면, 시작 전에 commit
      //cprintf("Forced Sync!");
      sync(); // Proj3: commit
      // 만약 이번 sync가 실패하더라도, while loop에서 다시 sync()를 호출함. 성공할 때 까지 계속 호출.
    }
    

    else { // 버퍼 충분하면, 작업 시작
      log.outstanding += 1;
      //release(&log.lock);
      if(!held_lock) {
        release(&log.lock);
      }
      break;
    }
  }
}

// called at the end of each FS system call.
// commits if this was the last outstanding operation.
void
end_op(void)
{
  //int do_commit = 0;
  int held_lock = holding(&log.lock);
  if(!held_lock) {
    acquire(&log.lock);
  }
  //acquire(&log.lock);
  log.outstanding -= 1;
  /*
  if(log.committing) // commit 중이라면 여기까지 올 일이 없다. lock이 뭔가 잘못된 것.
    panic("log.committing");
  if(log.outstanding == 0){
    do_commit = 1;
    log.committing = 1;
  } else { // 아직 작업중인 file 존재 시, 걔네들 다 작업 끝날 때까지는 flush 불가. 그 작업들 마저 이어서 하도록. 다만, 애초에 그 작업이 시작할 때 buffer 다 찰 것 같으면, begin_op()에서 기다리도록 구현되어 있기 때문에 이번에 flush 못한다고 문제되지 않음.
    // begin_op() may be waiting for log space,
    // and decrementing log.outstanding has decreased
    // the amount of reserved space.
    wakeup(&log);
  }
  release(&log.lock);

  if(do_commit){
    // call commit w/o holding locks, since not allowed
    // to sleep with locks.
    commit();
    acquire(&log.lock);
    log.committing = 0;
    wakeup(&log);
    release(&log.lock);
  }*/
  if(!held_lock) {
    release(&log.lock);
  }
  //release(&log.lock);
}

// Copy modified blocks from cache to log.
static void
write_log(void)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *to = bread(log.dev, log.start+tail+1); // log block
    struct buf *from = bread(log.dev, log.lh.block[tail]); // cache block
    memmove(to->data, from->data, BSIZE);
    bwrite(to);  // write the log
    brelse(from);
    brelse(to);
  }
}

static void
commit()
{
  if (log.lh.n > 0) {
    write_log();     // Write modified blocks from cache to log
    write_head();    // Write header to disk -- the real commit
    install_trans(); // Now install writes to home locations
    log.lh.n = 0;
    write_head();    // Erase the transaction from the log
  }
}

// Caller has modified b->data and is done with the buffer.
// Record the block number and pin in the cache with B_DIRTY.
// commit()/write_log() will do the disk write.
//
// log_write() replaces bwrite(); a typical use is:
//   bp = bread(...)
//   modify bp->data[]
//   log_write(bp)
//   brelse(bp)
void
log_write(struct buf *b)
{
  int i;

  if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
    panic("too big a transaction");
  if (log.outstanding < 1)
    panic("log_write outside of trans");

  acquire(&log.lock);
  for (i = 0; i < log.lh.n; i++) {
    if (log.lh.block[i] == b->blockno)   // log absorbtion
      break;
  }
  log.lh.block[i] = b->blockno;
  if (i == log.lh.n)
    log.lh.n++;
  b->flags |= B_DIRTY; // prevent eviction
/*
  if(log.lh.n == LOGSIZE-3) {
    end_op();
    while(log.lh.n == LOGSIZE-3) {
      if(sync() < 0) {
              cprintf("log_write: sync waiting\n");
        continue;
        //sleep(&log, &log.lock);
      }

    }
    begin_op();
  }*/
  release(&log.lock);

  //begin_op()에서 sync 호출은 명세 위반.
  //여기서 buffer 쓰고, 이번에 쓴 buffer가 마지막 남은 것 이었으면 flush 하도록
  //transaction 중 buffer가 다 차면, endop() 

}

//proj 3
//flush write buffer, return -1 if error, or the number of flushed blocks on success.
int sync() {
  int do_commit = 0;

  int held_lock = holding(&log.lock);
  if(!held_lock) {
    acquire(&log.lock);
  }
  //acquire(&log.lock);
  
  if(log.committing) // commit 중이라면 여기까지 올 일이 없다. lock이 뭔가 잘못된 것.
    panic("sync: log.committing");
  if(log.outstanding == 0){
    do_commit = 1;
    log.committing = 1;
  } else { // 아직 작업중인 file 존재 시, 걔네들 다 작업 끝날 때까지는 flush 불가. 그 작업들 마저 이어서 하도록. 다만, 애초에 그 작업이 시작할 때 buffer 다 찰 것 같으면, begin_op()에서 기다리도록 구현되어 있기 때문에 이번에 flush 못한다고 문제되지 않음.
    // 이번 sync는 실패. 다시 호출 시 재시도.
    // begin_op() may be waiting for log space,
    // and decrementing log.outstanding has decreased
    // the amount of reserved space.
    wakeup(&log);
    if(!held_lock) {
      release(&log.lock);
    }
    //release(&log.lock);
    cprintf("sync: log.outstanding != 0\n");
    return -1;
  }

  int to_flush = log.lh.n;
  release(&log.lock);

  if(do_commit){
    // call commit w/o holding locks, since not allowed
    // to sleep with locks.
    commit();
    acquire(&log.lock);
    log.committing = 0;
    wakeup(&log);
    if(!held_lock) {
      release(&log.lock);
    }
    //release(&log.lock);
    return to_flush;
  }

  // No control flow should reach here.
  panic("sync: control flow should not reach here.");
  return -98765431; //Dummy return.
}
