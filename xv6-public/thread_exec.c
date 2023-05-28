#include "types.h"
#include "stat.h"
#include "user.h"

#define NUM_THREAD 5

void *thread_main(void *arg)
{
  int val = (int)arg;
  printf(1, "Thread %d start\n", val);
  if (arg == 0) {
    sleep(100);
    char *pname = "/hello_thread";
    char *args[2] = {pname, 0};
    printf(1, "Executing...\n");
    exec(pname, args); // args가 할당해제 되는 느낌..
    /*
    가설)
    * string literal 은 object file의 read-only data section 에 저장되어 있어서
    * pname pointer가 할당 해제되어도 해당 literal의 주소는 남아있음. pname copy 해놓으면 됨
    * args[]가 string literal에 대한 주소를 담기에, exec전에 할당 해제되면 문제 발생
    */
  }
  else {
    sleep(200);
  }
  
  printf(1, "This code shouldn't be executed!!\n");
  exit();
  return 0;
}

thread_t thread[NUM_THREAD];

int main(int argc, char *argv[])
{
  int i;
  printf(1, "Thread exec test start\n");
  for (i = 0; i < NUM_THREAD; i++) {
    thread_create(&thread[i], thread_main, (void *)i);
  }
  sleep(200);
  printf(1, "This code shouldn't be executed!!\n");
  exit();
}
