//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *curproc = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd] == 0){
      curproc->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

int
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0) // file descriptor 하나만 더 추가.
    return -1;
  filedup(f);
  return fd;
}

int
sys_read(void)
{
  struct file *f;
  int n;
  char *p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0) // argfd: 이 프로세스에 fd로 열린 파일 있는지도 확인
    return -1;
  return fileread(f, p, n);
}

int
sys_write(void)
{
  struct file *f;
  int n;
  char *p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0){
    cprintf("sysfile.c: sys_write: argptr failed\n");
    return -1;
  }
  return filewrite(f, p, n);
}

int
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

int
sys_fstat(void)
{
  struct file *f;
  struct stat *st;

  if(argfd(0, 0, &f) < 0 || argptr(1, (void*)&st, sizeof(*st)) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
int
sys_link(void)
{
  char name[DIRSIZ], *new, *old;
  struct inode *dp, *ip;

  if(argstr(0, &old) < 0 || argstr(1, &new) < 0)
    return -1;

  begin_op();
  if((ip = namei(old,0)) == 0){ //이거 0 맞나
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){ // 디렉토리에 대해서는 link를 할 수 없다.
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name, 0)) == 0) //이거 0 맞나
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}



// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

//PAGEBREAK!
int
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de; // directory entry
  char name[DIRSIZ], *path;
  uint off;

  if(argstr(0, &path) < 0)
    return -1;

  cprintf("unlink path: %s\n", path);

  begin_op();
  if((dp = nameiparent(path, name, 0)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0) // directory에서 이름으로 찾기. 0이면 지울 파일이 없다.
    goto bad;
  ilock(ip);

  cprintf("found file to unlink: %d isSymlink %d\n", ip->inum, ip->isSymlink);
  if(ip->isSymlink){ // symbolic link면
  /*
    ip->nlink--;
    iupdate(ip);
    iunlockput(ip);
    iunlockput(dp);
    end_op();
    return 0;*/
    memset(&(ip->addrs), 0, sizeof(ip->addrs)); // symbolic link면 data block을 0으로 채워서 지운다.
  }

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){ // 안 빈 폴더는 지울 수 없다.
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de)) // directory에서 해당 directory entry애 해당하는 부분만 0 fill
    panic("unlink: writei");
  if(ip->type == T_DIR){ //지운 파일이 폴더면. 부모 link 감소. symbolic link에서는 감소하지 말아야?
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name, 1)) == 0)
    return 0;
  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp); // 부모 폴더는 쓸일 없고
    ilock(ip);
    if(type == T_FILE && ip->type == T_FILE) // 파일이면서 이미 파일이 있으면
      return ip;
    if(type == T_SYMLINK && ip->type == T_SYMLINK) // 심볼릭 링크이면서 이미 심볼릭 링크가 있으면
      return ip;
    iunlockput(ip); // 폴더를 만드려는데 해당 이름의 폴더/파일이 이미 있으면, 혹은 파일을 만드려는데 해당 이름의 폴더가 있으면
    return 0; //에러
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0) // 부모 폴더에 자식 이름으로 link
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

int
sys_open(void)
{
  char *path;
  int fd, omode;
  struct file *f;
  struct inode *ip;

  if(argstr(0, &path) < 0 || argint(1, &omode) < 0 )
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path,1)) == 0){ //
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  end_op();

  f->type = FD_INODE;
  f->ip = ip;
  f->off = 0;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
  return fd;
}

int
sys_openSymlinkFile(void)
{
  char *path;
  int fd, omode;
  struct file *f;
  struct inode *ip;

  int findRealfile;

  if(argstr(0, &path) < 0 || argint(1, &omode) < 0 || argint(2, &findRealfile))
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path,findRealfile)) == 0){ //
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  end_op();

  f->type = FD_INODE;
  f->ip = ip;
  f->off = 0;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
  return fd;
}

int
sys_mkdir(void)
{
  char *path;
  struct inode *ip;

  begin_op();
  if(argstr(0, &path) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

int
sys_mknod(void)
{
  struct inode *ip;
  char *path;
  int major, minor;

  begin_op();
  if((argstr(0, &path)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEV, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

int
sys_chdir(void)
{
  char *path;
  struct inode *ip;
  struct proc *curproc = myproc();
  
  begin_op();
  if(argstr(0, &path) < 0 || (ip = namei(path, 1)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(curproc->cwd);
  end_op();
  curproc->cwd = ip;
  return 0;
}

int
sys_exec(void)
{
  char *path, *argv[MAXARG];
  int i;
  uint uargv, uarg;

  if(argstr(0, &path) < 0 || argint(1, (int*)&uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv))
      return -1;
    if(fetchint(uargv+4*i, (int*)&uarg) < 0)
      return -1;
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    if(fetchstr(uarg, &argv[i]) < 0)
      return -1;
  }
  return exec(path, argv);
}

int
sys_pipe(void)
{
  int *fd;
  struct file *rf, *wf;
  int fd0, fd1;

  if(argptr(0, (void*)&fd, 2*sizeof(fd[0])) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      myproc()->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  fd[0] = fd0;
  fd[1] = fd1;
  return 0;
}

int sys_symlink(void)
{
  char *linkTo, *linkPath;
  struct inode *ip; // symlink의 inode
  //struct file * f; // symlink의 file

  if (argstr(0, &linkTo) < 0 || argstr(1, &linkPath) < 0)
    return -1;

  if(strlen(linkTo) > sizeof(ip->addrs)) // symlink의 target이 너무 길면
  {
    cprintf("symlink: too long linkTo\n");
    cprintf("%d > %d\n", strlen(linkTo), sizeof(ip->addrs));
    return -1;
  }

  begin_op();
  if ((ip = namei(linkPath,0)) != 0) // 이미 symlink의 이름이 사용중이면
  {
    end_op();
    cprintf("symlink: name in use\n");
    return -1;
  }
  cprintf("allocating inode..");
  if((ip = create(linkPath, T_SYMLINK, 0, 0)) == 0){ // symlink inode 생성
    //실패 시
    end_op();
    cprintf("symlink: create fail\n");
    return -1;
  } 
  cprintf("done!");
  ip->isSymlink = 1;
  //end_op(); 6월 1일 주석. end_op 두번하지 말라고.
  
  /* Symlink는 nlink와 관계 없고, DIR로 link 가능.
  if (ip->type == T_DIR)
  {
    iunlockput(ip);
    end_op();
    return -1;
  }*/
  /*ip->nlink++;
  iupdate(ip);
  iunlock(ip);*/


/*
  if ((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if (dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0)
  {
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;*/
  /*
  cprintf("allocating file..");
  if ((f = filealloc()) == 0)
  {
    if(f) {
      fileclose(f); //열리다 말았으면 닫기.
    }
    iunlockput(ip);
    cprintf("symlink: file alloc fail\n");
    return -1;
  }
  cprintf("done!");
  */
  cprintf("symlink: linking to %s ...", linkTo);
  safestrcpy((char*)ip->addrs, linkTo, sizeof(ip->addrs)); // symlink의 target을 저장
  iupdate(ip);
  cprintf("done!\n");
  end_op();
  iunlockput(ip); //let inode to be recycled.
/*
  f->readable = 1;
  f->writable = 0; //readonly
  f->ip = ip;
  f->off = 0;

  f->type = FD_INODE;
*/
  return 0;
}

// symlinnk pathname에 저장된 symlink의 linkTo를 path에 저장
int sys_lookSymlink(void)
{
  char *symlinkPath;
  char *path;
  int n;

  if(argstr(0, &symlinkPath) < 0 || argstr(1, &path) < 0 || argint(2, &n)) {
    cprintf("sys_lookSymlink: parse error\n");
    return -1;
  }
  else {
    return lookSymlink(symlinkPath, path, n);
  }
}

int lookSymlink(char* symlinkPath, char* path, int n) {
  struct inode *ip;
  if((ip = namei(symlinkPath,0)) == 0) {
    cprintf("lookSymlink: no file found with %s", symlinkPath);
    return -1;
  }
  ilock(ip);
  if(ip->isSymlink) {
    safestrcpy(path, (char*)ip->addrs, n);
    iunlockput(ip);
    return 0;
  }

  cprintf("lookSymlink: not a symlink! %s\n", symlinkPath);
  iunlockput(ip);
  return -1;
}


//proj 3
//flush write buffer, return -1 if error, or the number of flushed blocks on success.
int sys_sync(void)
{
  return sync();
}